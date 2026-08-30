#include "correcao.h"
#include "encoder.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include "armazenamento.h"
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

// =====================================================================
//  Localizar-se ao ligar. Ver correcao.h.
// =====================================================================
static ResumoZero z;
static uint32_t zeroDesde = 0;
static bool     zeroComecou = false;
// Uma leitura chegou, mas era impossivel. Guardado para a mensagem
// distinguir "o encoder nao respondeu" de "o encoder respondeu besteira"
// -- sao dois defeitos diferentes, com conserto diferente.
static bool  zImplausivel[2]      = {false, false};
static float zGrausImplausivel[2] = {0.0f, 0.0f};

ResumoZero zeroResumo() { return z; }

static void dizerZero(const char* m) {
  strncpy(z.motivo, m, sizeof(z.motivo) - 1);
  z.motivo[sizeof(z.motivo) - 1] = '\0';
}

// Acerta a contagem de passos de uma junta para bater com o encoder.
// Nenhum pulso sai no fio: o eixo NAO se mexe, so a conta muda.
// ---------------------------------------------------------------------
// A leitura e FISICAMENTE possivel?
//
// O encoder e a unica testemunha de onde o braco esta, e escrever a
// leitura na contagem de passos e obedecer a essa testemunha sem
// conferir nada. Se ela mentir uma vez -- registrador errado, contagens
// por volta erradas, ruido que passou no CRC -- a mentira vira a posicao
// oficial da maquina, e a partir dali TODA protecao de curso se apoia num
// numero inventado.
//
// A conferencia nao e estatistica, e fisica: o braco nao pode estar fora
// do curso que o proprio operador mediu. A folga cobre o que sobra
// depois do limite (o batente fica um pouco alem, e da para empurrar o
// braco a mao ate ele). Alem disso nao e posicao, e defeito.
//
// Junta sem curso medido nao tem contra o que conferir -- e ali nada
// anda mesmo, porque todo posicionamento exige calibracao.
// ---------------------------------------------------------------------
static const float FOLGA_PLAUSIVEL_GRAUS = 10.0f;

// Teto que vale SEM calibracao nenhuma.
//
// A conferencia contra o curso so existe depois de calibrar, e maquina em
// comissionamento nunca esta calibrada -- entao ali qualquer numero
// passava. Com os dois encoders no barramento e um deles mal configurado
// (contagens por volta erradas, formato de 32 bits errado, registrador do
// vizinho), o angulo saia em dezenas de milhares de graus, era marcado
// CONFIAVEL, ia para a tela e o braco desenhado girava sem parar.
//
// Nao existe junta desta maquina em 170 mil graus, calibrada ou nao. Duas
// voltas completas e folga generosa para qualquer montagem real e ainda
// pega o lixo por ordens de grandeza -- que e o que esta guarda precisa
// fazer: separar leitura de numero, nao medir precisao.
static const float LIMITE_ABSURDO_GRAUS = 720.0f;

static bool leituraPlausivel(uint8_t k, float graus) {
  const Junta& j = (k == 1) ? J1 : J2;
  if (graus != graus) return false;                 // NaN nao e angulo
  // Vale sempre, inclusive sem calibracao. E a unica conferencia que
  // existe durante a montagem, que e justamente quando o encoder esta
  // mal configurado.
  if (graus < -LIMITE_ABSURDO_GRAUS || graus > LIMITE_ABSURDO_GRAUS) return false;
  if (!j.calibrada) return true;
  return graus >= j.grausMin - FOLGA_PLAUSIVEL_GRAUS &&
         graus <= j.grausMax + FOLGA_PLAUSIVEL_GRAUS;
}

bool leituraConfiavel(uint8_t junta) {
  if (junta != 1 && junta != 2) return false;
  if (configEncoder.reg[junta - 1] == 0) return false;
  const LeituraEncoder L = encoderLer(junta);
  if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) return false;
  return leituraPlausivel(junta, L.graus);
}

static bool localizar(uint8_t k) {
  Junta& j = (k == 1) ? J1 : J2;
  if (configEncoder.reg[k - 1] == 0) return false;   // junta nao ligada
  // Sem zero ensinado a referencia e um numero arbitrario, e acertar a
  // contagem por ela poria o braco em qualquer lugar.
  if (!configZero.ensinado[k - 1]) return false;

  const LeituraEncoder L = encoderLer(k);
  if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) return false;
  if (j.passosPorGrau <= 0.0f) return false;

  // Localizar-se por uma leitura impossivel e pior que nao se localizar:
  // a maquina passaria a operar com uma posicao inventada, e o "ir ao
  // zero" mandaria um curso inteiro de pulso contra o batente.
  if (!leituraPlausivel(k, L.graus)) {
    zImplausivel[k - 1] = true;
    zGrausImplausivel[k - 1] = L.graus;
    return false;
  }

  ajustarContagem(j, grausParaPassos(j, L.graus));
  z.graus[k - 1] = L.graus;
  return true;
}

void zeroAtualizar() {
  if (!configZero.sincronizar) { z.estado = ZERO_PRONTO; return; }
  // Nenhuma junta com zero ensinado: nao ha o que recuperar, e a maquina
  // se comporta exatamente como antes do encoder absoluto.
  if (!configZero.ensinado[0] && !configZero.ensinado[1]) {
    if (z.estado == ZERO_ESPERANDO) {
      z.estado = ZERO_PRONTO;
      dizerZero("zero nao ensinado: a maquina liga como antes");
    }
    return;
  }
  if (z.estado == ZERO_PRONTO || z.estado == ZERO_SEM_ENCODER) return;

  if (!zeroComecou) { zeroComecou = true; zeroDesde = millis(); }

  // ---- 1. localizar ----
  if (z.estado == ZERO_ESPERANDO) {
    // Nao se acerta contagem com o eixo andando: entre ler e escrever o
    // eixo teria andado mais, e a conta nasceria torta.
    if (motoresEmMovimento()) return;

    bool alguma = false;
    for (uint8_t k = 1; k <= 2; k++) {
      if (z.localizou[k - 1]) { alguma = true; continue; }
      if (localizar(k)) { z.localizou[k - 1] = true; alguma = true; }
    }

    // Junta nao ligada nao impede: uma bancada com um driver so tem de
    // conseguir ligar a maquina.
    bool faltaAlguma = false;
    for (uint8_t k = 1; k <= 2; k++)
      if (configEncoder.reg[k - 1] != 0 && configZero.ensinado[k - 1] &&
          !z.localizou[k - 1]) faltaAlguma = true;

    if (!faltaAlguma && alguma) {
      z.estado = ZERO_LOCALIZADO;
      dizerZero("posicao recuperada do encoder");
      definirMensagem("Posicao recuperada do encoder: %.2f / %.2f graus",
                      (double)z.graus[0], (double)z.graus[1]);
      return;
    }

    // Cinco segundos sem leitura: segue como antes do encoder. Uma
    // maquina que nao liga porque o encoder nao respondeu e pior que uma
    // maquina que liga sem saber onde esta.
    if ((uint32_t)(millis() - zeroDesde) > 5000) {
      z.estado = ZERO_SEM_ENCODER;
      // "Nao respondeu" e "respondeu besteira" pedem conserto diferente:
      // um e cabo, o outro e registrador ou contagens por volta.
      if (zImplausivel[0] || zImplausivel[1]) {
        const uint8_t k = zImplausivel[0] ? 1 : 2;
        dizerZero("leitura fora do curso: nao me localizei");
        definirMensagem("Junta %u: o encoder diz %.1f graus, fora do curso "
                        "%.0f a %.0f. Confira registrador e contagens por volta",
                        (unsigned)k, (double)zGrausImplausivel[k - 1],
                        (double)((k == 1) ? J1.grausMin : J2.grausMin),
                        (double)((k == 1) ? J1.grausMax : J2.grausMax));
      } else {
        dizerZero("sem encoder no boot: posicao nao recuperada");
        definirMensagem("Sem leitura do encoder ao ligar: refira a maquina a mao");
      }
    }
    return;
  }

  // ---- 2. ir para o zero ----
  if (z.estado == ZERO_LOCALIZADO) {
    if (!configZero.irParaZero) {
      z.estado = ZERO_PRONTO;
      dizerZero("localizado (ir ao zero desligado)");
      return;
    }
    // O INTERTRAVAMENTO: nada anda antes de o operador habilitar os
    // servos, que e uma acao explicita dele na tela.
    if (!servosLigados) return;
    if (soldaLigada()) return;
    if (modoAtual != MODO_MANUAL) return;
    if (motoresEmMovimento()) return;

    // Ja esta no zero? Nao mexe.
    const float g1 = passosParaGraus(J1, posicaoJ1());
    const float g2 = passosParaGraus(J2, posicaoJ2());
    if (fabsf(g1) <= configZero.toleranciaGraus &&
        fabsf(g2) <= configZero.toleranciaGraus) {
      z.estado = ZERO_PRONTO;
      dizerZero("ja estava no zero");
      return;
    }

    // O zero tem de caber no curso calibrado. Se nao couber, ir para la
    // seria furar a protecao -- e a protecao existe justamente porque
    // nao ha fim de curso.
    if ((J1.calibrada && (grausParaPassos(J1, 0.0f) < J1.passosMin ||
                          grausParaPassos(J1, 0.0f) > J1.passosMax)) ||
        (J2.calibrada && (grausParaPassos(J2, 0.0f) < J2.passosMin ||
                          grausParaPassos(J2, 0.0f) > J2.passosMax))) {
      z.estado = ZERO_PRONTO;
      dizerZero("o zero esta fora do curso: nao fui");
      definirMensagem("Nao fui ao zero: ele esta fora do curso calibrado");
      return;
    }

    z.estado = ZERO_INDO;
    dizerZero("indo para o zero");
    definirMensagem("Indo para 0 grau (o encoder disse onde o braco estava)");
    correcaoNovoMovimento();
    moverCoordenado(grausParaPassos(J1, 0.0f), grausParaPassos(J2, 0.0f),
                    velAuto);
    modoAtual = MODO_POSICIONANDO;
    return;
  }

  // ---- 3. chegou ----
  if (z.estado == ZERO_INDO) {
    if (motoresEmMovimento() || modoAtual == MODO_POSICIONANDO) return;
    z.estado = ZERO_PRONTO;
    dizerZero("no zero");
  }
}

// =====================================================================
//  Seguir o eixo movido a mao. Ver correcao.h.
// =====================================================================
// Avisa, no maximo uma vez a cada 10 s por junta: com o eixo solto a
// leitura ruim se repete 20 vezes por segundo, e uma tira de mensagem
// piscando esconde tudo o que importa.
static void avisarImplausivel(uint8_t k, float graus) {
  static uint32_t ultimo[2] = {0, 0};
  const uint32_t agora = millis();
  if (ultimo[k - 1] && (uint32_t)(agora - ultimo[k - 1]) < 10000) return;
  ultimo[k - 1] = agora;
  const Junta& j = (k == 1) ? J1 : J2;
  definirMensagem("Junta %u: encoder diz %.1f graus, fora do curso %.0f a %.0f. "
                  "Posicao nao acompanhada", (unsigned)k, (double)graus,
                  (double)j.grausMin, (double)j.grausMax);
}

// A contagem de passos parou de descrever o braco: reescreve pelo
// encoder. Ver DIVERGENCIA_MAXIMA_GRAUS em config.h para o porque.
//
// Roda com o servo LIGADO, que e onde o assentamento manda -- mas so
// acima do teto em que o assentamento ja desistiu. Abaixo dele nada
// muda: divergencia pequena continua sendo perda de passo, e continua
// sendo do assentamento.
// A maquina esta REALMENTE parada, e nao apenas entre dois pulsos.
//
// isRunning() do gerador de pulso responde "esta saindo pulso agora?",
// e entre mandar um destino e o primeiro pulso sair ela responde NAO.
// Nessa fresta a contagem era reescrita, o destino que acabara de ser
// calculado sobre a contagem antiga virava outro lugar, e o braco
// arrancava e parava -- que e exatamente o que se via na tela: "comeca a
// se mover, da uma atualizacao e para".
//
// Duas condicoes fecham a fresta: o modo tem de ser MANUAL (posicionar,
// executar e reproduzir sao movimento em curso, com ou sem pulso neste
// instante) e o gerador tem de estar quieto ha um tempo.
static const uint32_t QUIETO_MS = 300;

static bool bracoQuieto() {
  static uint32_t ultimoMovimentoMs = 0;
  if (motoresEmMovimento()) { ultimoMovimentoMs = millis(); return false; }
  if (modoAtual != MODO_MANUAL) { ultimoMovimentoMs = millis(); return false; }
  if (ultimoMovimentoMs == 0) return true;
  return (uint32_t)(millis() - ultimoMovimentoMs) > QUIETO_MS;
}

static void reancorarSeAContagemSePerdeu() {
  if (!bracoQuieto()) return;
  if (correcaoEmCurso()) return;

  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    Junta& j = (k == 1) ? J1 : J2;
    if (configEncoder.reg[i] == 0) continue;
    if (j.passosPorGrau <= 0.0f) continue;
    if (!leituraConfiavel(k)) continue;

    const LeituraEncoder L = encoderLer(k);
    const float conta = passosParaGraus(j, (k == 1) ? posicaoJ1() : posicaoJ2());
    const float dif   = L.graus - conta;
    if (fabsf(dif) < DIVERGENCIA_MAXIMA_GRAUS) continue;

    ajustarContagem(j, grausParaPassos(j, L.graus));
    // Dizer sempre: reescrever a posicao da maquina em silencio seria a
    // tela mudando de numero sem ninguem entender por que.
    definirMensagem("Junta %u: a contagem tinha se perdido (%.0f graus de "
                    "diferenca) e foi reancorada no encoder, que mede "
                    "%.2f graus", (unsigned)k, (double)dif, (double)L.graus);
    logEvento("contagem reancorada na junta %u: %.1f -> %.1f graus",
              (unsigned)k, (double)conta, (double)L.graus);
  }
}

void seguirEixoSolto() {
  // A contagem perdida se conserta com servo ligado ou desligado: e o
  // caso em que ela deixou de significar qualquer coisa.
  reancorarSeAContagemSePerdeu();

  if (!bracoQuieto()) return;
  if (correcaoEmCurso()) return;

  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    Junta& j = (k == 1) ? J1 : J2;

    // A regra e POR JUNTA, e nao pela maquina inteira.
    //
    // Era "servosLigados" -- que so e verdade com AS DUAS juntas
    // energizadas. Numa bancada com um driver so isso nunca acontece, e
    // o seguimento de eixo solto ficava permanentemente ligado sobre uma
    // junta COM torque: a contagem era reescrita pelo encoder a cada
    // ciclo, inclusive logo depois de um destino ter sido calculado a
    // partir dela. Com torque, divergencia e perda de passo, e quem
    // cuida disso e o assentamento -- nao esta funcao.
    if (j.habilitado) continue;

    // Sem zero ensinado a leitura nao tem do que ser medida.
    if (!configZero.ensinado[i]) continue;
    if (configEncoder.reg[i] == 0) continue;
    if (j.passosPorGrau <= 0.0f) continue;

    const LeituraEncoder L = encoderLer(k);
    if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) continue;
    // Mao nenhuma leva o braco para fora do curso que ele tem. Leitura
    // dali para fora nao e o eixo: e defeito, e obedecer a ela poria a
    // maquina inteira operando com uma posicao inventada.
    if (!leituraPlausivel(k, L.graus)) {
      avisarImplausivel(k, L.graus);
      continue;
    }

    const float conta = passosParaGraus(j, (k == 1) ? posicaoJ1() : posicaoJ2());
    const float dif = L.graus - conta;
    // Zona morta: encoder de 17 bits treme, e reescrever a contagem a
    // cada tremor encheria o barramento de nada. Dois decimos de grau e
    // menos que qualquer movimento de mao.
    if (fabsf(dif) < 0.2f) continue;

    ajustarContagem(j, grausParaPassos(j, L.graus));
  }
}

// =====================================================================
//  Travamento
// =====================================================================
static Travamento trav = {false, 0, 0};

Travamento correcaoTravamento() { return trav; }
void correcaoLimparTravamento() { trav.ativo = false; trav.junta = 0; }

// Quanto o eixo DEVERIA estar andando, em contagens do encoder por
// segundo, para a velocidade de pulso que esta saindo agora.
static float esperadoContagensPorSeg(uint8_t k) {
  const Junta& j = (k == 1) ? J1 : J2;
  const float hz = (k == 1) ? velocidadeJ1Hz() : velocidadeJ2Hz();

  // A ESCALA MEDIDA vem primeiro.
  //
  // Este vigia PARA o braco. Ele nao pode decidir isso a partir de dois
  // numeros de catalogo que ninguem conferiu: bastava o driver estar
  // configurado com outro numero de pulsos por volta para o esperado sair
  // varias vezes maior que o real, e ai um braco andando normalmente era
  // declarado travado meio segundo depois de arrancar -- "comeca a se
  // mover, aparece um aviso e para".
  //
  // Quando ha escala ensinada, ela e a regua certa: contagens por grau
  // medidas na propria maquina, do mesmo lado de onde vem a leitura que
  // vai ser comparada.
  const float cpg = configEncoder.contagensPorGrau[k - 1];
  if (j.passosPorGrau > 0.0f && (cpg > 0.0001f || cpg < -0.0001f))
    return fabsf(hz) / j.passosPorGrau * fabsf(cpg);

  // Sem escala ensinada, o caminho antigo: pulsos por volta do motor e
  // contagens por volta do encoder.
  const float cv = configEncoder.contagensPorVolta[k - 1];
  if (j.passosPorVolta == 0 || cv <= 0.0f) return 0.0f;
  // passos/s -> voltas do motor/s -> contagens/s
  return fabsf(hz) / (float)j.passosPorVolta * cv;
}

static void vigiarTravamento() {
  static uint32_t desde[2] = {0, 0};
  const uint32_t agora = millis();

  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    const Junta& j = (k == 1) ? J1 : J2;

    // Sem leitura, sem julgamento: um cabo solto no encoder nao pode
    // parar o braco no meio de um cordao.
    // Leitura que nao merece confianca nao pode PARAR o braco no meio de
    // um cordao. "Confiavel" aqui e o mesmo criterio de todo o resto:
    // valida, recente e fisicamente possivel.
    if (!leituraConfiavel(k)) { desde[i] = 0; continue; }
    const LeituraEncoder L = encoderLer(k);

    // DOIS CRITERIOS, E O SEGUNDO NAO PRECISA DE ESCALA NENHUMA.
    //
    // Com escala medida da para exigir proporcao: "o eixo entrega menos
    // de um quinto do que deveria". E preciso, e pega ate escorregao
    // parcial.
    //
    // Sem escala medida essa conta sai de dois numeros de catalogo, e um
    // deles errado transforma braco andando em braco travado -- foi o que
    // fazia a maquina parar do nada. Mas ha um sinal que independe de
    // escala: o gerador de pulso claramente correndo e o encoder
    // claramente PARADO. Eixo que gira produz contagem, seja qual for a
    // escala; entao aqui nao existe falso positivo por numero errado.
    const float cpg = configEncoder.contagensPorGrau[i];
    const bool reguaMedida = (cpg > 0.0001f || cpg < -0.0001f);
    const float hz = (k == 1) ? velocidadeJ1Hz() : velocidadeJ2Hz();

    if (reguaMedida) {
      const float esperado = esperadoContagensPorSeg(k);
      // Perto de zero a conta nao distingue eixo parado de eixo travado,
      // e nao precisa: eixo parado nao esta forcando contra nada.
      if (esperado < 200.0f) { desde[i] = 0; continue; }
      if (fabsf(L.velocidade) > esperado * 0.2f) { desde[i] = 0; continue; }
    } else {
      if (hz < TRAV_HZ_MINIMO) { desde[i] = 0; continue; }
      if (fabsf(L.velocidade) > TRAV_CONTAGENS_QUIETO) { desde[i] = 0; continue; }
    }

    if (!desde[i]) { desde[i] = agora; continue; }
    // Meio segundo dando pulso sem o eixo responder. A leitura vem a 20
    // Hz: menos que isso seria julgar com duas ou tres amostras.
    if ((uint32_t)(agora - desde[i]) > 500) {
      desde[i] = 0;
      {
        // Parar o eixo e a acao, nao o aviso: continuar forcando contra o
        // batente aquece o servo e torce a mecanica.
        //
        // Quem observa `trav.total` la no laco principal interrompe o
        // movimento automatico -- programa, trajetoria, posicionamento.
        // Contar sem parar seria o movimento morrendo sem ninguem ter
        // mandado.
        if (trav.ativo) continue;
        trav.ativo = true;
        trav.junta = k;
        trav.total++;
        jogZerar();
        pararSuave();
        definirMensagem("Junta %u travada: o comando anda e o eixo nao. "
                        "Encostou no batente?", (unsigned)k);
      }
    }
  }
}

// ---------------------------------------------------------------------
// Vigilancia. Nao mexe no motor: so conta e avisa.
// ---------------------------------------------------------------------
static uint32_t alertas = 0;

uint32_t correcaoAlertas() { return alertas; }

void correcaoVigiar() {
  if (!configCorrecao.vigiar) return;
  vigiarTravamento();

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
  zImplausivel[0] = zImplausivel[1] = false;
  zGrausImplausivel[0] = zGrausImplausivel[1] = 0.0f;
  memset(&r, 0, sizeof(r));
  esperaAte = 0;
  alvo1Original = alvo2Original = 0;
  alertas = 0;
  trav.ativo = false; trav.junta = 0; trav.total = 0;
  memset(&z, 0, sizeof(z));
  zeroDesde = 0; zeroComecou = false;
}
#endif
