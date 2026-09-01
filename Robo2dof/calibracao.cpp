#include "calibracao.h"
#include "estado.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include "encoder.h"
#include "correcao.h"   // aferirEngrenagem(): a conta e a mesma dos dois lados
#include <math.h>   // fabsf/lroundf das contas de curso

bool calibAtiva() { return estadoCalib != CAL_INATIVO; }

// =====================================================================
//  CALIBRAR SAO DOIS GESTOS
//
//   1. Toca em Calibrar -> a maquina leva os dois eixos ao ZERO e SOLTA
//      os motores.
//   2. O operador empurra o braco com a mao ate o extremo de um lado --
//      os dois eixos de uma vez -- e toca de novo.
//   3. A maquina liga os motores, volta os dois ao zero e solta outra vez.
//   4. Ele empurra para o outro extremo e toca. A maquina calcula, volta
//      ao zero uma ultima vez e fica com torque, pronta para trabalhar.
//
//  Por que soltar em vez de mandar de jog: batente se alcanca melhor com
//  a mao. Com o motor solto o operador SENTE o fim do curso; com torque
//  ele empurra o eixo contra o ferro e so descobre pelo barulho.
//
//  Por que os dois eixos juntos: com o braco solto, os dois estao soltos.
//  Fazer um de cada vez seria pedir quatro viagens onde cabem duas.
//
//  Do que foi marcado sai:
//    - o CURSO de cada junta (os dois extremos);
//    - a ESCALA DO ENCODER em contagens por grau -- entre os extremos ha
//      um tanto de contagens e um tanto de graus, e a divisao e a escala;
//    - os PULSOS POR VOLTA de cada driver, medidos de graca durante as
//      voltas ao zero, que sao movimentos com pulso contado e encoder
//      olhando.
//
//  O ZERO nao se move. Ele e o ponto ao qual o operador voltou duas vezes
//  e viu o braco parar -- deslocar isso no fim seria trocar debaixo dele
//  o unico ponto que ele conhece da maquina.
//
//  O que continua declarado, e so isto: a REDUCAO do redutor. Com um
//  sensor so, antes dele, nenhuma medida a revela -- e isso e fisica, nao
//  limitacao de software.
// =====================================================================
static long    marcaA[2] = {0, 0}, marcaB[2] = {0, 0};
static bool    temA[2]   = {false, false}, temB[2] = {false, false};
static int32_t encA[2]   = {0, 0}, encB[2] = {0, 0};
static bool    encAok[2] = {false, false}, encBok[2] = {false, false};

// Medida dos PULSOS POR VOLTA, colhida durante as voltas ao zero.
static long    voltaPasso0[2]  = {0, 0};
static int32_t voltaEnc0[2]    = {0, 0};
static bool    voltaValendo[2] = {false, false};

// true quando as marcas viraram curso: quem le e o passo final, que so
// leva o braco de volta ao zero se houve o que medir.
static bool medido = false;

// Ultima leitura vista pelo seguimento a mao (puxarPelaMao, la embaixo).
//
// FICA AQUI EM CIMA porque calibIniciar() precisa limpar. calibAtualizar()
// so e chamado no modo CALIBRANDO: fora dele estes dois guardavam a
// leitura da calibracao ANTERIOR, e no primeiro ciclo com o motor solto a
// diferenca entre aquele numero velho e o de agora entrava na contagem de
// uma vez -- dezenas de milhares de graus num salto.
static int32_t ultimoBruto[2] = {0, 0};
static bool    temUltimo[2]   = {false, false};

uint8_t calibEixoAtivo() {
  // Os dois eixos andam juntos agora: nao ha "eixo da vez". 3 = os dois.
  return (estadoCalib == CAL_INATIVO) ? 0 : 3;
}

// A primeira parada e a unica em que nada foi medido ainda: e ali, e so
// ali, que trocar o sentido de um eixo nao contradiz uma marca ja feita.
bool calibNaPrimeiraEtapa() { return estadoCalib == CAL_LADO_A; }

// ---------------------------------------------------------------------
// O HABILITA E ASSINCRONO, E ESPERAR POR ELE PRENDE O NUCLEO.
//
// Desde que o SON virou Modbus, ligar e desligar torque e um pedido que
// a tarefa do encoder atende no outro nucleo. Chamar encoderSonEsperar()
// aqui travaria o nucleo 1 justamente enquanto o outro precisa dele -- e
// o proprio encoder.h avisa isso. Entao a calibracao PEDE e volta; o
// calibAtualizar() olha o estado a cada ciclo e segue quando ele assenta.
// ---------------------------------------------------------------------
static bool     esperandoSon   = false;
static bool     esperandoLigar = false;
static uint32_t prazoSon       = 0;

static void pedirTorque(bool ligar) {
  servosHabilitar(ligar, 0);
  esperandoSon   = true;
  esperandoLigar = ligar;
  prazoSon       = millis() + SON_PRAZO_MS;
}

// 0 = ainda esperando, 1 = confirmou, -1 = desistiu.
static int8_t torqueAssentou() {
  if (!esperandoSon) return 1;
  const uint8_t e = encoderSonEstado();
  if (e == SON_OK)     { esperandoSon = false; return  1; }
  if (e == SON_FALHOU) { esperandoSon = false; return -1; }
  if ((int32_t)(millis() - prazoSon) >= 0) { esperandoSon = false; return -1; }
  return 0;
}

// Manda os dois eixos para o zero da contagem.
//
// So anda com torque de verdade: gerar pulso para um eixo solto encheria
// a contagem de passos que o braco nao deu, que e o oposto do que esta
// calibracao existe para fazer. E passa pelo mesmo portao de seguranca
// que todo o resto.
static bool irAoZero() {
  if (!movimentoSeguro) return false;
  if (!J1.habilitado && !J2.habilitado) return false;
  if (J1.habilitado && J1.motor) {
    J1.motor->setSpeedInHz(grausPorSegParaHz(J1, velAuto));
    J1.motor->moveTo(0);
  }
  if (J2.habilitado && J2.motor) {
    J2.motor->setSpeedInHz(grausPorSegParaHz(J2, velAuto));
    J2.motor->moveTo(0);
  }
  return true;
}

// ---------------------------------------------------------------------
// PULSOS POR VOLTA, DE GRACA.
//
// `passosPorGrau = passosPorVolta x reducao / 360`. Sao dois numeros
// digitados, e o mais errado dos dois costuma ser o primeiro: e um
// parametro do DRIVER, muda quando alguem troca o drive ou refaz uma
// configuracao, e nada na tela denuncia. O sintoma e o braco andar menos
// (ou mais) do que a tela diz.
//
// Este o encoder mede sozinho, e a volta ao zero e o movimento ideal:
// pulso contado de um lado, voltas do MOTOR do outro.
//
//     passosPorVolta = |pulsos| x contagensPorVolta / |contagens|
//
// A reducao cancela: os dois lados sao por volta do motor. O que este
// numero NAO da e a reducao -- para isso faltaria uma referencia do lado
// da JUNTA, e com um sensor so antes do redutor ela nao existe.
// ---------------------------------------------------------------------
static void marcarInicioDaVolta() {
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    const LeituraEncoder L = encoderLer(k);
    voltaPasso0[i]  = (k == 1) ? posicaoJ1() : posicaoJ2();
    voltaEnc0[i]    = L.bruto;
    voltaValendo[i] = L.valido && L.idadeMs <= ENC_IDADE_MAX_MS;
  }
}

// A CONTA MORA EM aferirEngrenagem() (correcao.h).
//
// Ela nasceu aqui, presa a esta viagem -- e por isso so acontecia numa
// calibracao guiada. Numa maquina que nunca calibrou, a regua do
// movimento continuava sendo os dois numeros digitados, e o braco passava
// do angulo pedido pelo mesmo fator para sempre. Agora a mesma medida e
// feita tambem no fim de qualquer movimento comum; a viagem ao zero
// continua sendo a melhor ocasiao dela, mas deixou de ser a unica.
static void medirPassosPorVolta() {
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    if (!voltaValendo[i]) continue;
    voltaValendo[i] = false;

    const LeituraEncoder L = encoderLer(k);
    if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) continue;

    const long dPasso = ((k == 1) ? posicaoJ1() : posicaoJ2()) - voltaPasso0[i];
    // Complemento de dois: a volta do contador de 32 bits sai sozinha.
    const int32_t dCont = (int32_t)((uint32_t)L.bruto - (uint32_t)voltaEnc0[i]);
    aferirEngrenagem(k, dPasso, dCont);
  }
  // Quem grava aqui e o fim da calibracao (salvarConfiguracoes logo
  // adiante), entao o aviso de "vale gravar" nao precisa ser atendido.
}

// ---------------------------------------------------------------------
void calibIniciar() {
  soldaDesligar();
  pararSuave();
  jogZerar();

  esperandoSon = false;
  medido       = false;
  for (uint8_t i = 0; i < 2; i++) {
    temA[i] = temB[i] = false;
    encAok[i] = encBok[i] = false;
    voltaValendo[i] = false;
    temUltimo[i] = false;
  }

  // Enquanto nao houver calibracao nova, a protecao de curso fica
  // desativada de proposito: e o operador que esta definindo os limites,
  // e proteger com os limites velhos o impediria de chegar nos novos.
  J1.calibrada = false;
  J2.calibrada = false;

  modoAtual   = MODO_CALIBRANDO;
  estadoCalib = CAL_INDO_A;
  pedirTorque(true);
  aplicarVelocidadeManual();
  definirMensagem("Levando o braco ao zero. Depois os motores soltam");
}

void calibCancelar() {
  pararSuave();
  jogZerar();
  esperandoSon = false;
  estadoCalib  = CAL_INATIVO;
  modoAtual    = MODO_MANUAL;

  carregarConfiguracoes();   // restaura a calibracao anterior

  // O zero nao se move nesta calibracao, entao nao ha origem a desfazer.
  aplicarVelocidadeManual();
  aplicarAceleracao();
  definirMensagem("Calibracao cancelada");
}

// ---------------------------------------------------------------------
void calibReferenciar() {
  pararSuave();
  jogZerar();
  zerarPosicoes();     // a contagem volta a zero, que le grausHome
  definirMensagem("Referenciado: junta 1 em %.1f, junta 2 em %.1f graus",
                  passosParaGraus(J1, 0), passosParaGraus(J2, 0));
}

void calibApagar() {
  pararSuave();
  jogZerar();
  soldaDesligar();

  Junta* js[2] = { &J1, &J2 };
  for (uint8_t i = 0; i < 2; i++) {
    js[i]->calibrada = false;
    js[i]->passosMin = 0;
    js[i]->passosMax = 0;
    js[i]->grausHome = 0.0f;
    temA[i] = temB[i] = false;
    encAok[i] = encBok[i] = false;
  }
  estadoCalib = CAL_INATIVO;
  if (modoAtual == MODO_CALIBRANDO) modoAtual = MODO_MANUAL;

  recalcularResolucao();
  salvarConfiguracoes();
  aplicarVelocidadeManual();
  aplicarAceleracao();

  Serial.println("[CAL] Curso apagado do NVS.");
  // Sem limites a maquina NAO para de funcionar: ela so deixa de ter
  // protecao de curso -- que, alias, nasce desligada.
  definirMensagem("Curso apagado. A maquina segue operando, sem limite");
}

// ---------------------------------------------------------------------
// Grava as marcas de um lado: onde CADA junta esta agora, em passos, e o
// que o encoder estava lendo no mesmo instante.
// ---------------------------------------------------------------------
static void gravarLado(bool ladoA) {
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    const long pos = (k == 1) ? posicaoJ1() : posicaoJ2();
    const LeituraEncoder L = encoderLer(k);
    const bool lendo = L.valido && L.idadeMs <= ENC_IDADE_MAX_MS;
    if (ladoA) { marcaA[i] = pos; temA[i] = true; encA[i] = L.bruto; encAok[i] = lendo; }
    else       { marcaB[i] = pos; temB[i] = true; encB[i] = L.bruto; encBok[i] = lendo; }
  }
}

// ---------------------------------------------------------------------
// Fecha uma junta a partir dos dois extremos dela.
// ---------------------------------------------------------------------
static bool fecharJunta(Junta& j, uint8_t k) {
  const uint8_t i = k - 1;
  if (!temA[i] || !temB[i]) return false;
  if (j.passosPorGrau <= 0.0f) return false;

  long lo = marcaA[i], hi = marcaB[i];
  if (lo > hi) { const long t = lo; lo = hi; hi = t; }

  const float curso = (float)(hi - lo) / j.passosPorGrau;
  if (curso < CURSO_MINIMO_GRAUS) return false;

  // O zero tem de caber dentro do curso: e dali que os limites sao
  // contados, e e onde o braco para toda vez que alguem manda ele para
  // casa. Se os dois extremos cairam do mesmo lado, o intervalo e
  // esticado ate incluir o zero em vez de deixar a maquina travada.
  if (lo > 0) lo = 0;
  if (hi < 0) hi = 0;

  j.passosMin = lo;
  j.passosMax = hi;
  j.grausHome = 0.0f;
  j.calibrada = true;

  // A ESCALA DO ENCODER SAI DE GRACA.
  //
  // Entre os dois extremos ha um tanto de contagens (o encoder viu) e um
  // tanto de graus (a contagem de passos, dividida pela resolucao). A
  // divisao das duas E a escala, com sinal: encoder que conta para tras
  // enquanto a junta avanca da escala negativa, e o angulo sai certo sem
  // chave de inversao nenhuma.
  if (encAok[i] && encBok[i]) {
    // Complemento de dois: a volta do contador de 32 bits sai sozinha.
    const int32_t dCont = (int32_t)((uint32_t)encB[i] - (uint32_t)encA[i]);
    const float dGraus  = (float)(marcaB[i] - marcaA[i]) / j.passosPorGrau;
    if (fabsf(dGraus) >= CURSO_MINIMO_GRAUS && labs((long)dCont) >= 50) {
      configEncoder.contagensPorGrau[i] = (float)dCont / dGraus;
      Serial.printf("[CAL] Junta %u: escala do encoder medida em %.2f "
                    "contagens por grau (%ld contagens em %.1f graus)\n",
                    (unsigned)k, (double)configEncoder.contagensPorGrau[i],
                    (long)dCont, (double)dGraus);
    }
  }
  return true;
}

static void concluir() {
  medido = false;
  const bool ok1 = fecharJunta(J1, 1);
  const bool ok2 = fecharJunta(J2, 2);

  if (!ok1 && !ok2) {
    estadoCalib = CAL_INATIVO;
    modoAtual   = MODO_MANUAL;
    aplicarVelocidadeManual();
    aplicarAceleracao();
    definirMensagem("Nada medido: o curso ficou menor que %.0f graus nas duas "
                    "juntas. Leve o braco ate os batentes de verdade",
                    (double)CURSO_MINIMO_GRAUS);
    return;
  }
  medido = true;

  recalcularResolucao();   // converte o curso medido para graus

  // O encoder passa a ler o mesmo angulo que a contagem: a referencia
  // dele e reancorada na posicao atual, ja com a escala nova.
  encoderPendente = configEncoder;
  encoderReconfigurar();
  encoderDefinirZero(1, passosParaGraus(J1, posicaoJ1()));
  encoderDefinirZero(2, passosParaGraus(J2, posicaoJ2()));

  salvarConfiguracoes();
  aplicarVelocidadeManual();
  aplicarAceleracao();

  // O que a calibracao mede NAO liga a protecao sozinha: o braco continua
  // livre, e o limite e coisa que o operador liga quando quiser.
  if (ok1 && ok2)
    definirMensagem("Curso medido: J1 %.1f a %.1f, J2 %.1f a %.1f graus. "
                    "Voltando ao zero",
                    J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax);
  else
    definirMensagem("Junta %u medida: %.1f a %.1f graus. A outra nao teve "
                    "curso suficiente e ficou sem limite. Voltando ao zero",
                    ok1 ? 1u : 2u,
                    ok1 ? J1.grausMin : J2.grausMin,
                    ok1 ? J1.grausMax : J2.grausMax);
}

// ---------------------------------------------------------------------
// Um toque so, que quer dizer coisas diferentes conforme onde se esta.
// Os dois numeros continuam na assinatura porque a fila de comandos os
// carrega, mas nenhum e usado: a calibracao nao pergunta nada.
void calibConfirmar(float, float) {
  switch (estadoCalib) {
    case CAL_LADO_A:
      jogZerar(); pararSuave();
      gravarLado(true);
      estadoCalib = CAL_VOLTANDO;
      pedirTorque(true);
      definirMensagem("Extremo gravado. Voltando ao zero");
      break;

    case CAL_LADO_B:
      jogZerar(); pararSuave();
      gravarLado(false);
      concluir();
      if (medido) {
        // Terceira e ultima viagem: o braco volta ao zero e FICA com
        // torque. Terminar a calibracao com a maquina largada no batente
        // e sem torque seria devolve-la pior do que se pegou.
        estadoCalib = CAL_CONCLUIDO;
        pedirTorque(true);
      }
      break;

    default:
      // Durante as idas ao zero o botao nao faz nada: quem esta andando e
      // a maquina, e interromper no meio deixaria a marca no lugar errado.
      break;
  }
}

// ---------------------------------------------------------------------
// CALIBRAR COM OS MOTORES SOLTOS
//
// O jeito mais seguro de chegar num batente e com o motor sem torque,
// empurrando o braco com a mao. So que ai o gerador de pulso nao anda, e
// a contagem -- que e o que a marca grava -- ficaria parada nos extremos.
//
// Entao, enquanto a calibracao esta aberta, cada junta SEM torque tem a
// contagem puxada pelo encoder. Nao e o seguimento de eixo solto de
// sempre: aquele escreve uma posicao ABSOLUTA e por isso exige o zero
// absoluto ja ensinado. Aqui basta o DELTA, e delta nao precisa de
// origem nenhuma.
//
// A conversao de contagem para passo dispensa a reducao: os dois numeros
// sao por volta do MOTOR, e o redutor cancela na divisao.
//
//     passos por contagem = passosPorVolta / contagensPorVolta
//
// Com a escala ja ensinada, usa-se ela, que e medida em vez de digitada.
// ---------------------------------------------------------------------
static void puxarPelaMao(uint8_t k) {
  const uint8_t i = k - 1;
  Junta& j = (k == 1) ? J1 : J2;

  if (j.habilitado) { temUltimo[i] = false; return; }
  if (configEncoder.reg[i] == 0) { temUltimo[i] = false; return; }

  const LeituraEncoder L = encoderLer(k);
  if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) { temUltimo[i] = false; return; }

  if (!temUltimo[i]) { ultimoBruto[i] = L.bruto; temUltimo[i] = true; return; }

  // Complemento de dois: a volta do contador de 32 bits sai sozinha.
  const int32_t d = (int32_t)((uint32_t)L.bruto - (uint32_t)ultimoBruto[i]);
  if (d == 0) return;
  ultimoBruto[i] = L.bruto;

  float passosPorContagem;
  const float cpg = configEncoder.contagensPorGrau[i];
  if (j.passosPorGrau > 0.0f && (cpg > 0.0001f || cpg < -0.0001f)) {
    passosPorContagem = j.passosPorGrau / cpg;
  } else {
    const float cv = configEncoder.contagensPorVolta[i];
    if (cv < 1.0f || j.passosPorVolta == 0) return;
    passosPorContagem = (float)j.passosPorVolta / cv;
  }

  const long passos = lroundf((float)d * passosPorContagem);
  if (passos == 0) return;
  const long agora = (k == 1) ? posicaoJ1() : posicaoJ2();
  ajustarContagem(j, agora + passos);
}

// ---------------------------------------------------------------------
// As tres viagens ao zero sao da MAQUINA. Cada uma tem tres tempos --
// esperar o torque confirmar, andar, e (nas duas primeiras) soltar de
// novo -- todos sem prender o nucleo, olhando o estado a cada ciclo.
// ---------------------------------------------------------------------
void calibAtualizar() {
  if (modoAtual != MODO_CALIBRANDO) { temUltimo[0] = temUltimo[1] = false; return; }

  const bool viajando = (estadoCalib == CAL_INDO_A ||
                         estadoCalib == CAL_VOLTANDO ||
                         estadoCalib == CAL_CONCLUIDO);
  if (viajando) {
    const bool primeira = (estadoCalib == CAL_INDO_A);
    const bool ultima   = (estadoCalib == CAL_CONCLUIDO);
    // Enquanto a MAQUINA dirige, a memoria do seguimento a mao nao vale:
    // guardar a leitura de antes da viagem e, ao soltar, descontar a
    // viagem inteira da contagem de uma vez.
    temUltimo[0] = temUltimo[1] = false;

    if (esperandoSon) {
      const int8_t r = torqueAssentou();
      if (r == 0) return;                     // ainda escrevendo no barramento

      if (esperandoLigar) {
        if (r == 1 && irAoZero()) { marcarInicioDaVolta(); return; }
        // Nao deu para ir sozinho: o operador leva com a mao.
        if (ultima) {
          estadoCalib = CAL_INATIVO;
          modoAtual   = MODO_MANUAL;
          definirMensagem("Curso gravado. O braco ficou no batente: leve-o de "
                          "volta quando puder");
          return;
        }
        estadoCalib = primeira ? CAL_LADO_A : CAL_LADO_B;
        definirMensagem("Sem torque confirmado no barramento. Leve o braco ao "
                        "%s extremo com a mao e toque",
                        primeira ? "primeiro" : "outro");
        return;
      }

      // Era o pedido de SOLTAR: assentou, segue para a parada manual.
      estadoCalib = primeira ? CAL_LADO_A : CAL_LADO_B;
      definirMensagem(primeira
          ? "No zero, motores soltos. Empurre o braco ate o extremo de UM "
            "lado e toque de novo"
          : "De volta ao zero, motores soltos. Agora o extremo do OUTRO "
            "lado, e toque");
      return;
    }

    if (motoresEmMovimento()) return;

    // Chegou ao zero. Mede os pulsos por volta com o que acabou de andar.
    medirPassosPorVolta();

    if (ultima) {
      // Fim: a maquina fica no zero, com torque, pronta.
      estadoCalib = CAL_INATIVO;
      modoAtual   = MODO_MANUAL;
      aplicarVelocidadeManual();
      aplicarAceleracao();
      definirMensagem("Calibrado. J1 %.1f a %.1f, J2 %.1f a %.1f graus -- "
                      "ligue o limite em Ajustes para ele valer",
                      J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax);
      return;
    }

    pedirTorque(false);   // solta para a mao do operador
    return;
  }

  if (motoresEmMovimento()) return;
  puxarPelaMao(1);
  puxarPelaMao(2);
}
