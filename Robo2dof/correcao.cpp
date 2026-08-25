#include "correcao.h"
#include "encoder.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include <string.h>
#include <math.h>

// Tudo aqui roda no CORE 1, junto com o motor. E a regra de ouro do
// projeto: quem manda no motor e um nucleo so.

static ResumoCorrecao r;
static uint32_t esperaAte = 0;
// Onde o movimento pediu para parar. O retoque anda ALEM disso para levar
// o eixo ao lugar certo; no fim a contagem volta para ca, senao o desvio
// nao some -- so muda de lugar.
static long alvo1Original = 0, alvo2Original = 0;

// Depois que o eixo para, a ultima leitura do encoder ainda e de antes
// da parada. O ciclo le a 20 Hz e o valor leva um tempo para assentar --
// retocar em cima de leitura de meio segundo atras e mover o braco
// baseado em onde ele estava.
static const uint32_t ESPERA_ASSENTAR_MS = 250;

static void dizer(const char* m) {
  strncpy(r.motivo, m, sizeof(r.motivo) - 1);
  r.motivo[sizeof(r.motivo) - 1] = '\0';
}

// ---------------------------------------------------------------------
// Quanto falta para o eixo chegar no ALVO, em graus, SE der para confiar.
//
// Repare que NAO se usa L.erro (comandado menos medido). O comandado sai
// da contagem de passos, e a contagem ANDA JUNTO com o retoque: medir
// contra ela daria sempre a mesma diferenca, o retoque nunca fecharia, e
// o eixo iria embora um pouco a cada tentativa. O alvo, ao contrario,
// esta parado -- e e nele que o operador quer o braco.
// ---------------------------------------------------------------------
static bool faltaPara(uint8_t junta, float alvoGraus, float& falta) {
  const Junta& j = (junta == 1) ? J1 : J2;
  if (!j.calibrada) return false;
  // Junta sem registrador nao esta ligada: nao ha o que corrigir.
  if (configEncoder.reg[junta - 1] == 0) return false;

  const LeituraEncoder L = encoderLer(junta);
  if (!L.valido) return false;
  if (L.idadeMs > ENC_IDADE_MAX_MS) return false;

  falta = alvoGraus - L.graus;   // graus da junta que faltam andar
  return true;
}

// ---------------------------------------------------------------------
void correcaoNovoMovimento() {
  r.estado     = CORR_PARADA;
  r.tentativas = 0;
  dizer("");
}

// ---------------------------------------------------------------------
void correcaoIniciar() {
  if (!configCorrecao.ativa) return;
  if (soldaLigada()) return;     // retoque no meio do cordao estraga o cordao

  r.estado     = CORR_ESPERANDO;
  r.tentativas = 0;
  r.erroInicial1 = r.erroInicial2 = 0.0f;
  r.erroFinal1   = r.erroFinal2   = 0.0f;
  alvo1Original = posicaoJ1();
  alvo2Original = posicaoJ2();
  esperaAte = millis() + ESPERA_ASSENTAR_MS;
  dizer("assentando");
}

void correcaoCancelar() {
  if (r.estado == CORR_ESPERANDO || r.estado == CORR_RETOCANDO) {
    r.estado = CORR_PARADA;
    dizer("cancelado");
  }
}

bool correcaoEmCurso() {
  return r.estado == CORR_ESPERANDO || r.estado == CORR_RETOCANDO;
}

ResumoCorrecao correcaoResumo() { return r; }

// ---------------------------------------------------------------------
void correcaoAtualizar() {
  if (!correcaoEmCurso()) return;

  // Regra 4: solda ligada cancela na hora, em qualquer fase.
  if (soldaLigada()) {
    r.estado = CORR_RECUSADA;
    dizer("solda ligada: nao se retoca no cordao");
    return;
  }
  // Regra 1: so com o eixo parado.
  if (motoresEmMovimento()) return;
  if ((int32_t)(millis() - esperaAte) < 0) return;

  const float alvoG1 = passosParaGraus(J1, alvo1Original);
  const float alvoG2 = passosParaGraus(J2, alvo2Original);

  float e1 = 0.0f, e2 = 0.0f;
  const bool t1 = faltaPara(1, alvoG1, e1);
  const bool t2 = faltaPara(2, alvoG2, e2);

  // Regra 2: sem leitura confiavel nao se mexe no motor.
  if (!t1 && !t2) {
    r.estado = CORR_RECUSADA;
    dizer("sem leitura do encoder: nada foi retocado");
    return;
  }

  if (r.tentativas == 0) { r.erroInicial1 = e1; r.erroInicial2 = e2; }
  r.erroFinal1 = e1;
  r.erroFinal2 = e2;

  const float m1 = fabsf(e1), m2 = fabsf(e2);
  const float tol = configCorrecao.toleranciaGraus;

  // Chegou.
  if ((!t1 || m1 <= tol) && (!t2 || m2 <= tol)) {
    // O eixo esta fisicamente no lugar certo. A contagem, porem, ficou
    // adiantada pelo tanto que o retoque andou -- e e ela que o proximo
    // movimento absoluto usa como ponto de partida. Sem devolver a
    // contagem ao alvo, o desvio nao some: ele so passa para o proximo
    // movimento, que e exatamente o incomodo que este modulo existe para
    // resolver.
    //
    // Nenhum pulso sai no fio aqui: o eixo NAO se mexe.
    if (r.tentativas > 0) {
      ajustarContagem(J1, alvo1Original);
      ajustarContagem(J2, alvo2Original);
    }
    r.estado = CORR_PRONTA;
    r.totalOk++;
    dizer("posicao conferida pelo encoder");
    return;
  }

  // Regra 5: erro grande nao se corrige, se denuncia.
  const float teto = configCorrecao.maxCorrecaoGraus;
  if ((t1 && m1 > teto) || (t2 && m2 > teto)) {
    r.estado = CORR_RECUSADA;
    dizer("erro grande demais: veja acoplamento e reducao");
    return;
  }

  // Regra 6: numero de tentativas limitado.
  if (r.tentativas >= configCorrecao.tentativas) {
    r.estado = CORR_DESISTIU;
    r.totalDesistiu++;
    dizer("nao fechou na tolerancia");
    return;
  }

  // O retoque. 'falta' e alvo menos medido: positivo quer dizer que o
  // eixo esta ATRAS do alvo e precisa avancar.
  // Graus de erro viram passos direto pela resolucao da junta. Nao se
  // usa grausParaPassos() aqui: aquela funcao converte um ANGULO
  // ABSOLUTO (descontando o zero da maquina), e o que se tem aqui e uma
  // DIFERENCA -- passar diferenca por ela somaria o zero duas vezes.
  long alvo1 = posicaoJ1();
  long alvo2 = posicaoJ2();
  if (t1 && m1 > tol) alvo1 += lroundf(e1 * J1.passosPorGrau);
  if (t2 && m2 > tol) alvo2 += lroundf(e2 * J2.passosPorGrau);

  // Regra 3: nunca para fora do curso calibrado. O retoque e ajuste
  // fino, nao excecao as protecoes.
  if (J1.calibrada) {
    if (alvo1 < J1.passosMin) alvo1 = J1.passosMin;
    if (alvo1 > J1.passosMax) alvo1 = J1.passosMax;
  }
  if (J2.calibrada) {
    if (alvo2 < J2.passosMin) alvo2 = J2.passosMin;
    if (alvo2 > J2.passosMax) alvo2 = J2.passosMax;
  }

  if (alvo1 == posicaoJ1() && alvo2 == posicaoJ2()) {
    // O retoque cairia fora do curso: nao ha o que fazer sem furar o
    // limite, e furar o limite nunca.
    r.estado = CORR_DESISTIU;
    r.totalDesistiu++;
    dizer("retoque cairia fora do curso calibrado");
    return;
  }

  // Devagar: retoque e ajuste fino, nao viagem. Um quarto da velocidade
  // normal ja e mais que suficiente para poucos decimos de grau, e
  // reduz o quanto o eixo passa do ponto.
  moverCoordenado(alvo1, alvo2, velNormal * 0.25f);
  r.tentativas++;
  r.estado = CORR_RETOCANDO;
  esperaAte = millis() + ESPERA_ASSENTAR_MS;
  dizer("retocando");
}

// ---------------------------------------------------------------------
// Vigilancia. Nao mexe no motor: so conta e avisa.
// ---------------------------------------------------------------------
static uint32_t alertas = 0;

uint32_t correcaoAlertas() { return alertas; }

void correcaoVigiar() {
  if (!configCorrecao.vigiar) return;

  // Enquanto o eixo anda, comandado e medido divergem de propria conta:
  // o encoder ve onde o eixo ESTA e o firmware conta onde ele MANDOU
  // estar, e entre os dois ha a rampa. Vigiar em movimento acusaria erro
  // que nao existe. So parado.
  if (motoresEmMovimento()) return;
  if (correcaoEmCurso()) return;

  static uint32_t desde[2] = {0, 0};
  static bool     avisado[2] = {false, false};
  const uint32_t agora = millis();

  for (uint8_t k = 1; k <= 2; k++) {
    // Aqui o alvo E o comandado: nao ha retoque em curso, entao a
    // contagem esta parada e as duas medidas coincidem.
    const float cmd = (k == 1) ? passosParaGraus(J1, posicaoJ1())
                               : passosParaGraus(J2, posicaoJ2());
    float e = 0.0f;
    if (!faltaPara(k, cmd, e) || fabsf(e) <= configCorrecao.alertaGraus) {
      desde[k - 1] = 0;
      avisado[k - 1] = false;
      continue;
    }
    if (!desde[k - 1]) { desde[k - 1] = agora; continue; }
    // Um segundo fora do limite, parado: nao e transiente de leitura.
    if (!avisado[k - 1] && (uint32_t)(agora - desde[k - 1]) > 1000) {
      avisado[k - 1] = true;
      alertas++;
      definirMensagem("Junta %u fora de posicao: %+.2f graus pelo encoder",
                      (unsigned)k, (double)e);
    }
  }
}

#ifdef ROBO2DOF_TESTE
void correcaoReiniciarTeste() {
  memset(&r, 0, sizeof(r));
  esperaAte = 0;
  alvo1Original = alvo2Original = 0;
  alertas = 0;
}
#endif
