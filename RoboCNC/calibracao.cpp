#include "calibracao.h"
#include "estado.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"

bool calibAtiva() { return estadoCalib != CAL_INATIVO; }

// Posicao dos contadores no instante em que o HOME foi gravado. Cancelar
// a calibracao depois disso precisa DESFAZER o zerarPosicoes(): senao os
// limites recuperados do NVS se referem ao zero antigo e passam a
// proteger a regiao errada, com erro igual a distancia entre os dois.
static long origemAntesDoZero1 = 0;
static long origemAntesDoZero2 = 0;
static bool origemFoiDeslocada = false;

uint8_t calibEixoAtivo() {
  switch (estadoCalib) {
    case CAL_J1_NEG: case CAL_J1_VOLTA_NEG:
    case CAL_J1_POS: case CAL_J1_VOLTA_POS: return 1;
    case CAL_J2_NEG: case CAL_J2_VOLTA_NEG:
    case CAL_J2_POS: case CAL_J2_VOLTA_POS: return 2;
    default: return 0;
  }
}

// ---------------------------------------------------------------------
void calibIniciar() {
  // O assistente pede que o operador leve o braco ate os limites: sem
  // torque nos drivers nao ha o que medir, so contador correndo solto.
  if (!servosLigados) {
    definirMensagem("Habilite os servos antes de calibrar");
    return;
  }

  soldaDesligar();
  pararSuave();
  jogZerar();

  origemFoiDeslocada = false;

  // Enquanto nao houver calibracao valida, a protecao de postura fica
  // desativada de proposito: e o operador que esta definindo os limites.
  J1.calibrada = false;
  J2.calibrada = false;

  modoAtual   = MODO_CALIBRANDO;
  estadoCalib = CAL_HOME;
  aplicarVelocidadeManual();
  definirMensagem("Calibracao: leve o braco ate a posicao de referencia");
}

void calibCancelar() {
  pararSuave();
  jogZerar();
  estadoCalib = CAL_INATIVO;
  modoAtual   = MODO_MANUAL;

  carregarConfiguracoes();   // restaura a calibracao anterior

  // Desfaz o zerarPosicoes() do CAL_HOME. Depois do zero, a posicao 0
  // corresponde ao ponto fisico que valia origemAntesDoZero: a posicao
  // atual na referencia antiga e origemAntesDoZero + posicao atual.
  if (origemFoiDeslocada) {
    if (J1.motor) J1.motor->setCurrentPosition(posicaoJ1() + origemAntesDoZero1);
    if (J2.motor) J2.motor->setCurrentPosition(posicaoJ2() + origemAntesDoZero2);
    origemFoiDeslocada = false;
    Serial.println("[CAL] Origem anterior restaurada.");
  }

  aplicarVelocidadeManual();
  aplicarAceleracao();
  definirMensagem("Calibracao cancelada");
}

// ---------------------------------------------------------------------
static void voltarParaZero(Junta& j) {
  if (!j.motor) return;
  j.motor->setSpeedInHz(velAuto);
  j.motor->moveTo(0);
}

// Sanidade do que foi medido. Se o operador percorreu uma etapa no
// sentido contrario (ou o pino DIR esta invertido), min e max saem
// trocados e o intervalo nao contem o zero - que e justamente onde o
// assistente deixa o braco. Sem esta checagem o robo trava assim que a
// calibracao termina.
static bool ajustarCurso(Junta& j, uint8_t numero) {
  if (j.passosMin > j.passosMax) {
    const long t = j.passosMin;
    j.passosMin = j.passosMax;
    j.passosMax = t;
    Serial.printf("[CAL] Junta %u: limites invertidos, corrigidos.\n",
                  (unsigned)numero);
  }
  if (j.passosMin > 0) j.passosMin = 0;
  if (j.passosMax < 0) j.passosMax = 0;

  // Em GRAUS, nao em passos. O criterio antigo (> 10 passos) aceitava
  // 0,4 grau na resolucao padrao - menos que os 2 x MARGEM_LIMITE_GRAUS
  // que posturaValida() desconta. O resultado era um intervalo util
  // negativo: nenhuma postura passava e os dois eixos travavam.
  if (j.passosPorGrau <= 0.0f) return false;
  const float curso = (float)(j.passosMax - j.passosMin) / j.passosPorGrau;
  return curso >= CURSO_MINIMO_GRAUS;
}

static void concluir() {
  const bool ok1 = ajustarCurso(J1, 1);
  const bool ok2 = ajustarCurso(J2, 2);

  if (!ok1 || !ok2) {
    estadoCalib = CAL_INATIVO;
    modoAtual   = MODO_MANUAL;
    J1.calibrada = false;
    J2.calibrada = false;
    aplicarVelocidadeManual();
    aplicarAceleracao();
    definirMensagem("Calibracao descartada: junta %s com curso menor que %.0f graus. Refaca movendo ate os limites reais.",
                    !ok1 ? "1" : "2", CURSO_MINIMO_GRAUS);
    return;
  }

  J1.calibrada = true;
  J2.calibrada = true;
  recalcularResolucao();   // converte o curso medido para graus
  salvarConfiguracoes();

  estadoCalib = CAL_INATIVO;
  modoAtual   = MODO_MANUAL;
  aplicarVelocidadeManual();
  aplicarAceleracao();

  definirMensagem("Calibrado: J1 %.1f a %.1f, J2 %.1f a %.1f graus",
                  J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax);
}

// ---------------------------------------------------------------------
void calibConfirmar() {
  switch (estadoCalib) {
    case CAL_HOME:
      jogZerar();
      pararSuave();
      origemAntesDoZero1 = posicaoJ1();
      origemAntesDoZero2 = posicaoJ2();
      origemFoiDeslocada = true;
      zerarPosicoes();
      J1.passosMin = J1.passosMax = 0;
      J2.passosMin = J2.passosMax = 0;
      estadoCalib = CAL_J1_NEG;
      definirMensagem("Referencia gravada. Leve a junta 1 ao limite negativo");
      break;

    case CAL_J1_NEG:
      jogZerar();
      J1.passosMin = posicaoJ1();
      voltarParaZero(J1);
      estadoCalib = CAL_J1_VOLTA_NEG;
      break;

    case CAL_J1_POS:
      jogZerar();
      J1.passosMax = posicaoJ1();
      voltarParaZero(J1);
      estadoCalib = CAL_J1_VOLTA_POS;
      break;

    case CAL_J2_NEG:
      jogZerar();
      J2.passosMin = posicaoJ2();
      voltarParaZero(J2);
      estadoCalib = CAL_J2_VOLTA_NEG;
      break;

    case CAL_J2_POS:
      jogZerar();
      J2.passosMax = posicaoJ2();
      voltarParaZero(J2);
      estadoCalib = CAL_J2_VOLTA_POS;
      break;

    case CAL_CONCLUIDO:
      // A partir daqui o zero novo e o oficial: nao ha mais o que desfazer.
      origemFoiDeslocada = false;
      concluir();
      break;

    default:
      break;   // estados de retorno automatico ignoram o botao
  }
}

// ---------------------------------------------------------------------
void calibAtualizar() {
  if (modoAtual != MODO_CALIBRANDO) return;

  switch (estadoCalib) {
    case CAL_J1_VOLTA_NEG:
      if (J1.motor && !J1.motor->isRunning()) {
        aplicarVelocidadeManual();
        estadoCalib = CAL_J1_POS;
        definirMensagem("Leve a junta 1 ao limite positivo");
      }
      break;

    case CAL_J1_VOLTA_POS:
      if (J1.motor && !J1.motor->isRunning()) {
        aplicarVelocidadeManual();
        estadoCalib = CAL_J2_NEG;
        definirMensagem("Junta 1 pronta. Leve a junta 2 ao limite negativo");
      }
      break;

    case CAL_J2_VOLTA_NEG:
      if (J2.motor && !J2.motor->isRunning()) {
        aplicarVelocidadeManual();
        estadoCalib = CAL_J2_POS;
        definirMensagem("Leve a junta 2 ao limite positivo");
      }
      break;

    case CAL_J2_VOLTA_POS:
      if (J2.motor && !J2.motor->isRunning()) {
        aplicarVelocidadeManual();
        estadoCalib = CAL_CONCLUIDO;
        definirMensagem("Curso medido. Confira os valores e conclua");
      }
      break;

    default:
      break;
  }
}
