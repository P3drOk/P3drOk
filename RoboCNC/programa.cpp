#include "programa.h"
#include "estado.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include <math.h>

static Ponto   pontos[MAX_PONTOS];
static uint8_t nPontos = 0;

enum FaseProg : uint8_t {
  FASE_PARADO,
  FASE_INDO_INICIO,
  FASE_ABRINDO_ARCO,
  FASE_DESLOCANDO,   // trecho sem solda: interpolacao nas juntas (rapido)
  FASE_SOLDANDO,     // trecho com solda: RETA no espaco cartesiano
  FASE_FECHANDO_ARCO
};

static FaseProg fase       = FASE_PARADO;
static bool     ensaio     = false;
static uint8_t  idx        = 0;
static uint32_t marcaTempo = 0;

// Estado da reta em curso
static float    xa, ya, xb, yb;
static uint32_t tSegIni, tSegTotal;
static uint32_t velSeg1, velSeg2;
static float    refT1, refT2;   // mantem o mesmo "cotovelo" ao longo da reta

// ---------------------------------------------------------------------
uint8_t      progQuantidade()  { return nPontos; }
const Ponto* progLista()       { return pontos; }
bool         progRodando()     { return fase != FASE_PARADO; }
bool         progEmEnsaio()    { return ensaio; }
uint8_t      progIndiceAtual() { return idx; }

uint8_t progProgresso() {
  if (fase == FASE_PARADO || nPontos < 2) return 0;
  const uint16_t p = (uint16_t)idx * 100 / (nPontos - 1);
  return p > 100 ? 100 : (uint8_t)p;
}

// ---------------------------------------------------------------------
static void pontoParaXY(const Ponto& p, float& x, float& y) {
  const float a1 = passosParaGraus(J1, p.p1);
  const float a2 = passosParaGraus(J2, p.p2);
  float xc, yc;
  cinematicaDireta(a1, a2, xc, yc, x, y);
}

// ---------------------------------------------------------------------
bool progAdicionarPonto(long p1, long p2, const char** motivo) {
  if (nPontos >= MAX_PONTOS) {
    if (motivo) *motivo = "limite de pontos do programa atingido";
    return false;
  }
  const char* m = nullptr;
  if (!posturaValidaPassos(p1, p2, &m)) {
    if (motivo) *motivo = m ? m : "postura invalida";
    return false;
  }
  pontos[nPontos].p1 = (int32_t)p1;
  pontos[nPontos].p2 = (int32_t)p2;
  pontos[nPontos].soldaAteProximo = 0;
  nPontos++;
  definirMensagem("Ponto %u gravado", (unsigned)nPontos);
  return true;
}

bool progRemoverPonto(uint8_t indice) {
  if (indice >= nPontos) return false;
  for (uint8_t i = indice; i + 1 < nPontos; i++) pontos[i] = pontos[i + 1];
  nPontos--;
  definirMensagem("Ponto %u removido", (unsigned)(indice + 1));
  return true;
}

void progDefinirSolda(uint8_t indice, bool ligar) {
  if (indice >= nPontos) return;
  pontos[indice].soldaAteProximo = ligar ? 1 : 0;
}

void progLimpar() {
  nPontos = 0;
  fase    = FASE_PARADO;
  definirMensagem("Programa apagado");
}

// ---------------------------------------------------------------------
// Confere se a RETA entre dois pontos e percorrivel do inicio ao fim.
// Sem isso o robo descobriria um ponto inalcancavel no meio do cordao,
// com o arco ja aberto.
// ---------------------------------------------------------------------
static bool retaPercorrivel(uint8_t i, const char** motivo) {
  float x0, y0, x1, y1;
  pontoParaXY(pontos[i], x0, y0);
  pontoParaXY(pontos[i + 1], x1, y1);

  const float dist = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
  const uint16_t n = (uint16_t)(dist / PASSO_INTERP_MM) + 2;

  float r1 = passosParaGraus(J1, pontos[i].p1);
  float r2 = passosParaGraus(J2, pontos[i].p2);

  for (uint16_t k = 0; k <= n; k++) {
    const float a = (float)k / (float)n;
    const float x = x0 + (x1 - x0) * a;
    const float y = y0 + (y1 - y0) * a;
    float t1, t2;
    const char* m = nullptr;
    if (!resolverXY(x, y, r1, r2, t1, t2, &m)) {
      if (motivo) *motivo = m ? m : "a reta do cordao sai da area util";
      return false;
    }
    r1 = t1; r2 = t2;
  }
  return true;
}

// ---------------------------------------------------------------------
bool progIniciar(bool modoEnsaio, const char** motivo) {
  if (nPontos < 2) {
    if (motivo) *motivo = "grave pelo menos 2 pontos";
    return false;
  }
  if (!J1.calibrada || !J2.calibrada) {
    if (motivo) *motivo = "calibre as juntas antes de executar";
    return false;
  }
  for (uint8_t i = 0; i < nPontos; i++) {
    const char* m = nullptr;
    if (!posturaValidaPassos(pontos[i].p1, pontos[i].p2, &m)) {
      if (motivo) *motivo = m ? m : "o programa contem ponto invalido";
      return false;
    }
  }
  // Trechos de solda percorrem reta: valide a reta inteira, nao so as pontas.
  for (uint8_t i = 0; i + 1 < nPontos; i++) {
    if (!pontos[i].soldaAteProximo) continue;
    if (!retaPercorrivel(i, motivo)) return false;
  }

  ensaio = modoEnsaio;
  idx    = 0;
  fase   = FASE_INDO_INICIO;

  soldaDesligar();
  moverCoordenado(pontos[0].p1, pontos[0].p2, velAuto);

  definirMensagem(ensaio ? "Ensaio iniciado, arco travado desligado"
                         : "Executando com arco");
  return true;
}

// ---------------------------------------------------------------------
static void prepararReta() {
  pontoParaXY(pontos[idx],     xa, ya);
  pontoParaXY(pontos[idx + 1], xb, yb);

  const float dist = sqrtf((xb - xa) * (xb - xa) + (yb - ya) * (yb - ya));
  const float vel  = (velCordaoMmS > 0.1f) ? velCordaoMmS : 1.0f;

  tSegTotal = (uint32_t)(dist / vel * 1000.0f);
  if (tSegTotal < 50) tSegTotal = 50;
  tSegIni   = millis();

  refT1 = passosParaGraus(J1, pontos[idx].p1);
  refT2 = passosParaGraus(J2, pontos[idx].p2);

  // Velocidade de seguimento com folga: o motor precisa alcancar o
  // setpoint interpolado, senao a ponta "corta caminho" e o cordao
  // deixa de ser reto.
  const long d1 = labs((long)pontos[idx + 1].p1 - pontos[idx].p1);
  const long d2 = labs((long)pontos[idx + 1].p2 - pontos[idx].p2);
  velSeg1 = (uint32_t)((uint64_t)d1 * 1000 / tSegTotal) * 3 + 200;
  velSeg2 = (uint32_t)((uint64_t)d2 * 1000 / tSegTotal) * 3 + 200;

  if (J1.motor) J1.motor->setAcceleration(J1.aceleracao * 4);
  if (J2.motor) J2.motor->setAcceleration(J2.aceleracao * 4);
}

// ---------------------------------------------------------------------
// Percorre a reta. Roda a CADA ciclo, inclusive com os motores andando:
// e um seguidor de setpoint, nao um "vai e espera chegar".
// ---------------------------------------------------------------------
static void atualizarReta() {
  const uint32_t decorrido = millis() - tSegIni;
  float a = (float)decorrido / (float)tSegTotal;
  if (a > 1.0f) a = 1.0f;

  const float x = xa + (xb - xa) * a;
  const float y = ya + (yb - ya) * a;

  float t1, t2;
  const char* m = nullptr;
  if (!resolverXY(x, y, refT1, refT2, t1, t2, &m)) {
    soldaDesligar();
    progParar();
    definirMensagem("Cordao abortado no meio: %s", m ? m : "ponto invalido");
    return;
  }
  refT1 = t1; refT2 = t2;

  seguirSetpoint(grausParaPassos(J1, t1), grausParaPassos(J2, t2),
                 velSeg1, velSeg2);

  if (a >= 1.0f && !motoresEmMovimento()) {
    if (!ensaio && pontos[idx].soldaAteProximo) {
      fase       = FASE_FECHANDO_ARCO;
      marcaTempo = millis();
    } else {
      idx++;
      if (idx + 1 >= nPontos) {
        progParar();
        definirMensagem(ensaio ? "Ensaio concluido" : "Programa concluido");
        return;
      }
      fase       = FASE_ABRINDO_ARCO;
      marcaTempo = millis();
    }
  }
}

// ---------------------------------------------------------------------
void progAtualizar() {
  if (fase == FASE_PARADO) return;

  // A fase de reta nao pode esperar os motores pararem.
  if (fase == FASE_SOLDANDO) { atualizarReta(); return; }

  if (motoresEmMovimento()) return;

  switch (fase) {
    case FASE_INDO_INICIO:
      idx        = 0;
      fase       = FASE_ABRINDO_ARCO;
      marcaTempo = millis();
      break;

    case FASE_ABRINDO_ARCO: {
      const bool comSolda = pontos[idx].soldaAteProximo;

      if (comSolda && !ensaio) {
        soldaDefinir(true);
        if (millis() - marcaTempo < DWELL_ABRE_ARCO_MS) return;
      }

      if (comSolda) {
        // Trecho de solda: RETA no espaco cartesiano.
        prepararReta();
        fase = FASE_SOLDANDO;
      } else {
        // Deslocamento: interpolacao nas juntas, mais rapida. O caminho
        // sai curvo, e nao ha problema nenhum nisso.
        moverCoordenado(pontos[idx + 1].p1, pontos[idx + 1].p2, velAuto);
        fase = FASE_DESLOCANDO;
      }
      break;
    }

    case FASE_DESLOCANDO:
      idx++;
      if (idx + 1 >= nPontos) {
        progParar();
        definirMensagem(ensaio ? "Ensaio concluido" : "Programa concluido");
        return;
      }
      fase       = FASE_ABRINDO_ARCO;
      marcaTempo = millis();
      break;

    case FASE_FECHANDO_ARCO:
      if (millis() - marcaTempo < DWELL_FECHA_ARCO_MS) return;
      soldaDesligar();
      idx++;
      if (idx + 1 >= nPontos) {
        progParar();
        definirMensagem("Programa concluido");
        return;
      }
      fase       = FASE_ABRINDO_ARCO;
      marcaTempo = millis();
      break;

    default:
      break;
  }
}

void progParar() {
  if (fase == FASE_PARADO) return;
  fase = FASE_PARADO;
  soldaDesligar();
  pararSuave();
  aplicarVelocidadeManual();
  aplicarAceleracao();
}
