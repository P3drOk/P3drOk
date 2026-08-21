#include "programa.h"
#include "estado.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include <math.h>
#include <stdio.h>

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
  Violacao v;
  if (!posturaValidaDet(passosParaGraus(J1, p1), passosParaGraus(J2, p2), v)) {
    static char aviso[128];
    char det[112];
    violacaoTexto(v, det, sizeof(det));
    snprintf(aviso, sizeof(aviso), "%s", det);
    if (motivo) *motivo = aviso;
    return false;
  }
  pontos[nPontos].p1 = (int32_t)p1;
  pontos[nPontos].p2 = (int32_t)p2;
  pontos[nPontos].soldaAteProximo = 0;
  nPontos++;
  definirMensagem("Ponto %u gravado", (unsigned)nPontos);
  return true;
}

bool progCarregarDe(const Ponto* origem, uint8_t n, const char** motivo) {
  if (fase != FASE_PARADO) {
    if (motivo) *motivo = "pare o programa antes de carregar outro";
    return false;
  }
  if (!origem || n < 2) {
    if (motivo) *motivo = "arquivo com menos de 2 pontos";
    return false;
  }
  if (n > MAX_PONTOS) {
    if (motivo) *motivo = "arquivo com pontos demais";
    return false;
  }
  // Valida tudo ANTES de escrever: nada de programa carregado pela metade.
  for (uint8_t i = 0; i < n; i++) {
    Violacao v;
    if (!posturaValidaDet(passosParaGraus(J1, origem[i].p1),
                          passosParaGraus(J2, origem[i].p2), v)) {
      static char aviso[144];
      char det[112];
      violacaoTexto(v, det, sizeof(det));
      snprintf(aviso, sizeof(aviso), "ponto %u do arquivo: %s",
               (unsigned)(i + 1), det);
      if (motivo) *motivo = aviso;
      return false;
    }
  }
  memcpy(pontos, origem, (size_t)n * sizeof(Ponto));
  nPontos = n;
  // O ultimo ponto nao tem "proximo": solda ligada nele nao significa nada.
  pontos[nPontos - 1].soldaAteProximo = 0;
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
static bool retaPercorrivelDet(uint8_t i, Violacao& v) {
  float x0, y0, x1, y1;
  pontoParaXY(pontos[i],     x0, y0);
  pontoParaXY(pontos[i + 1], x1, y1);
  return retaCartesianaValida(x0, y0, x1, y1,
                              passosParaGraus(J1, pontos[i].p1),
                              passosParaGraus(J2, pontos[i].p2), v);
}

// ---------------------------------------------------------------------
bool progConferirTrecho(uint8_t i, char* aviso, size_t tam) {
  if (aviso && tam) aviso[0] = '\0';
  if (i + 1 >= nPontos) return true;

  Violacao v;
  const bool ok = pontos[i].soldaAteProximo
                ? retaPercorrivelDet(i, v)
                : caminhoJuntasValidoDet(passosParaGraus(J1, pontos[i].p1),
                                         passosParaGraus(J2, pontos[i].p2),
                                         passosParaGraus(J1, pontos[i + 1].p1),
                                         passosParaGraus(J2, pontos[i + 1].p2), v);
  if (ok) return true;

  if (aviso && tam) {
    char det[112];
    violacaoTexto(v, det, sizeof(det));
    snprintf(aviso, tam, "%s %u->%u: %s",
             pontos[i].soldaAteProximo ? "cordao" : "deslocamento",
             (unsigned)(i + 1), (unsigned)(i + 2), det);
  }
  return false;
}

// ---------------------------------------------------------------------
bool progIniciar(bool modoEnsaio, const char** motivo) {
  // Todas as recusas passam por aqui: uma frase que diz o ponto ou o
  // trecho, a junta, o valor exigido e o limite. Recusa generica faz o
  // operador olhar dois pontos folgados e concluir que a maquina errou.
  static char aviso[208];
  aviso[0] = '\0';

  if (nPontos < 2) {
    if (motivo) *motivo = "grave pelo menos 2 pontos";
    return false;
  }
  if (!J1.calibrada || !J2.calibrada) {
    if (motivo) *motivo = "calibre as juntas antes de executar";
    return false;
  }
  // Vale para o ensaio tambem: ele move o braco pelo percurso inteiro.
  if (!servosLigados) {
    if (motivo) *motivo = "habilite os servos antes de executar";
    return false;
  }

  Violacao v;
  char det[112];

  // A posicao ATUAL entra na conta: o braco parado fora da area util
  // reprovava o programa com uma mensagem que falava de junta, e o
  // operador ia procurar o defeito nos pontos.
  if (!posturaValidaDet(passosParaGraus(J1, posicaoJ1()),
                        passosParaGraus(J2, posicaoJ2()), v)) {
    violacaoTexto(v, det, sizeof(det));
    snprintf(aviso, sizeof(aviso),
             "o braco esta parado fora da area util (%s). Traga-o de volta com o jog", det);
    if (motivo) *motivo = aviso;
    return false;
  }

  for (uint8_t i = 0; i < nPontos; i++) {
    if (!posturaValidaDet(passosParaGraus(J1, pontos[i].p1),
                          passosParaGraus(J2, pontos[i].p2), v)) {
      violacaoTexto(v, det, sizeof(det));
      snprintf(aviso, sizeof(aviso), "ponto %u: %s", (unsigned)(i + 1), det);
      if (motivo) *motivo = aviso;
      return false;
    }
  }

  // Aproximacao ate o primeiro ponto: interpolada nas juntas.
  if (!caminhoJuntasValidoDet(passosParaGraus(J1, posicaoJ1()),
                              passosParaGraus(J2, posicaoJ2()),
                              passosParaGraus(J1, pontos[0].p1),
                              passosParaGraus(J2, pontos[0].p2), v)) {
    violacaoTexto(v, det, sizeof(det));
    snprintf(aviso, sizeof(aviso),
             "no caminho da posicao atual ate o ponto 1: %s", det);
    if (motivo) *motivo = aviso;
    return false;
  }

  // Cada trecho pelo que ele realmente percorre: reta cartesiana quando
  // ha solda, interpolacao nas juntas quando e so deslocamento.
  for (uint8_t i = 0; i + 1 < nPontos; i++) {
    if (!progConferirTrecho(i, aviso, sizeof(aviso))) {
      if (motivo) *motivo = aviso;
      return false;
    }
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
    definirMensagem("Cordao %u->%u abortado a %.0f%% do trecho: %s",
                    (unsigned)(idx + 1), (unsigned)(idx + 2), a * 100.0f,
                    m ? m : "ponto invalido");
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
