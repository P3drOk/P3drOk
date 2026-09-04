#include "trajetoria.h"
#include "estado.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"

static Waypoint  buffer[MAX_WAYPOINTS];
static uint16_t  nPontos   = 0;

static bool      gravando  = false;
static uint32_t  t0Gravacao = 0;
static uint32_t  ultimaAmostraMs = 0;

static bool      reproduzindo = false;
static uint32_t  t0Reproducao = 0;
static uint32_t  tTraj        = 0;   // tempo interno da trajetoria
static uint16_t  idxSegmento  = 0;
static uint32_t  velSeguir1   = 1000;
static uint32_t  velSeguir2   = 1000;

static const long DEADBAND_PASSOS = 3;

// Buffer emprestado para a tarefa de cartao (ver trajetoria.h).
static bool emprestado = false;

// ---------------------------------------------------------------------
void trajLimpar() {
  if (emprestado) return;
  nPontos = 0;
  gravando = false;
  reproduzindo = false;
  definirMensagem("Trajetoria apagada");
}

uint16_t trajPontos()    { return nPontos; }
uint32_t trajDuracaoMs() { return nPontos > 0 ? buffer[nPontos - 1].tMs : 0; }
bool trajGravando()      { return gravando; }
bool trajReproduzindo()  { return reproduzindo; }
const Waypoint* trajBuffer() { return buffer; }

bool trajEmprestado() { return emprestado; }

bool trajEmprestar() {
  if (gravando || reproduzindo) return false;
  emprestado = true;
  return true;
}

void trajDevolver() { emprestado = false; }

Waypoint* trajBufferGravavel() { return emprestado ? buffer : nullptr; }

// Ver progDeslocarPassos(): a mesma renumeracao, para o que foi gravado
// a mao livre.
void trajDeslocarPassos(long d1, long d2) {
  if (d1 == 0 && d2 == 0) return;
  for (uint16_t i = 0; i < nPontos; i++) {
    buffer[i].p1 = (int32_t)((long)buffer[i].p1 + d1);
    buffer[i].p2 = (int32_t)((long)buffer[i].p2 + d2);
  }
}

void trajDefinirN(uint16_t n) {
  if (!emprestado) return;
  nPontos = (n > MAX_WAYPOINTS) ? MAX_WAYPOINTS : n;
}

uint8_t trajProgresso() {
  const uint32_t dur = trajDuracaoMs();
  if (!reproduzindo || dur == 0) return 0;
  uint32_t p = (uint32_t)((uint64_t)tTraj * 100 / dur);
  return p > 100 ? 100 : (uint8_t)p;
}

// ---------------------------------------------------------------------
// GRAVACAO
// ---------------------------------------------------------------------
bool trajIniciarGravacao() {
  if (emprestado) return false;   // cartao lendo/gravando este buffer
  nPontos         = 0;
  gravando        = true;
  t0Gravacao      = millis();
  ultimaAmostraMs = 0;

  buffer[0] = { 0, (int32_t)posicaoJ1(), (int32_t)posicaoJ2(),
                (uint8_t)(soldaLigada() ? 1 : 0) };
  nPontos = 1;

  definirMensagem("Gravando: mova o braco e ligue a solda onde precisar");
  return true;
}

void trajAmostrar(long p1, long p2, bool solda) {
  if (!gravando) return;

  const uint32_t agora = millis();
  const uint32_t t = agora - t0Gravacao;
  if (t - ultimaAmostraMs < PERIODO_AMOSTRA_MS) return;
  ultimaAmostraMs = t;

  if (nPontos >= MAX_WAYPOINTS) {
    gravando = false;
    definirMensagem("Gravacao encerrada: memoria de trajetoria cheia");
    return;
  }

  const Waypoint& ultimo = buffer[nPontos - 1];
  const bool parado =
      labs((long)ultimo.p1 - p1) < DEADBAND_PASSOS &&
      labs((long)ultimo.p2 - p2) < DEADBAND_PASSOS;
  const bool mudouSolda = (ultimo.solda != (solda ? 1 : 0));

  // Pontos parados sao descartados, mas uma mudanca de solda sempre entra
  // (e o instante do acionamento do arco, nao pode ser perdido).
  if (parado && !mudouSolda) return;

  buffer[nPontos++] = { t, (int32_t)p1, (int32_t)p2,
                        (uint8_t)(solda ? 1 : 0) };
}

void trajPararGravacao() {
  if (!gravando) return;
  gravando = false;

  // Ponto final explicito com a posicao real de parada.
  if (nPontos < MAX_WAYPOINTS) {
    buffer[nPontos++] = { (uint32_t)(millis() - t0Gravacao),
                          (int32_t)posicaoJ1(), (int32_t)posicaoJ2(),
                          (uint8_t)(soldaLigada() ? 1 : 0) };
  }
  definirMensagem("Trajetoria gravada: %u pontos, %.1f s",
                  (unsigned)nPontos, trajDuracaoMs() / 1000.0f);
}

// ---------------------------------------------------------------------
// REPRODUCAO
// ---------------------------------------------------------------------
static void calcularVelocidadesSeguimento() {
  // Velocidade de seguimento = maior velocidade exigida pela trajetoria,
  // com folga. O motor precisa conseguir alcancar o setpoint, senao ele
  // "atrasa" e o caminho sai deformado.
  uint32_t maior1 = 1, maior2 = 1;
  for (uint16_t i = 1; i < nPontos; i++) {
    const uint32_t dt = buffer[i].tMs - buffer[i - 1].tMs;
    if (dt == 0) continue;
    const uint32_t v1 = (uint32_t)(labs((long)buffer[i].p1 - buffer[i - 1].p1) * 1000UL / dt);
    const uint32_t v2 = (uint32_t)(labs((long)buffer[i].p2 - buffer[i - 1].p2) * 1000UL / dt);
    if (v1 > maior1) maior1 = v1;
    if (v2 > maior2) maior2 = v2;
  }
  const uint32_t escala = escalaVelocidadeTraj > 0 ? escalaVelocidadeTraj : 100;
  velSeguir1 = (maior1 * 3 / 2) * escala / 100 + 50;
  velSeguir2 = (maior2 * 3 / 2) * escala / 100 + 50;
}

bool trajIniciarReproducao(const char** motivo) {
  if (emprestado) {
    if (motivo) *motivo = "aguarde o cartao terminar de ler a trajetoria";
    return false;
  }
  if (nPontos < 2) {
    if (motivo) *motivo = "nenhuma trajetoria gravada";
    return false;
  }
  // Reproduzir nao exige calibracao: a trajetoria foi gravada nesta
  // mesma regua e volta pelo mesmo caminho. Sem limites medidos o que
  // falta e a protecao de curso, nao a capacidade de repetir.
  // A reproducao aciona o rele e move os dois eixos: sem servos ela so
  // conta passos e desmancha a referencia de calibracao.
  if (!servosLigados) {
    if (motivo) *motivo = "habilite os servos antes de reproduzir";
    return false;
  }

  // Revalida o caminho inteiro antes de encostar em qualquer motor.
  for (uint16_t i = 0; i < nPontos; i++) {
    const char* m = nullptr;
    if (!posturaValidaPassos(buffer[i].p1, buffer[i].p2, &m)) {
      if (motivo) *motivo = m ? m : "trajetoria invalida";
      return false;
    }
  }

  // A aproximacao ate o primeiro ponto e interpolada nas juntas: valida
  // o interior dela tambem, nao so o destino.
  {
    const char* m = nullptr;
    if (!caminhoJuntasValidoPassos(posicaoJ1(), posicaoJ2(),
                                   buffer[0].p1, buffer[0].p2, &m)) {
      if (motivo) *motivo = m ? m : "o caminho ate o inicio sai da area util";
      return false;
    }
  }

  calcularVelocidadesSeguimento();

  // Aproxima do ponto inicial de forma coordenada antes de comecar.
  moverCoordenado(buffer[0].p1, buffer[0].p2, velAuto);

  reproduzindo = true;
  t0Reproducao = 0;   // 0 = ainda indo para o ponto inicial
  tTraj        = 0;
  idxSegmento  = 0;
  definirMensagem("Reproduzindo trajetoria (%u%%)", (unsigned)escalaVelocidadeTraj);
  return true;
}

void trajAtualizarReproducao() {
  if (!reproduzindo) return;

  // Fase 1: posicionar no inicio.
  if (t0Reproducao == 0) {
    if (motoresEmMovimento()) return;
    t0Reproducao = millis();
    // Aceleracao alta durante o seguimento: a suavidade vem da
    // interpolacao no tempo, nao da rampa de cada segmento.
    if (J1.motor) J1.motor->setAcceleration(grausPorSegParaHz(J1, J1.aceleracao * 4.0f));
    if (J2.motor) J2.motor->setAcceleration(grausPorSegParaHz(J2, J2.aceleracao * 4.0f));
    return;
  }

  const uint32_t escala = escalaVelocidadeTraj > 0 ? escalaVelocidadeTraj : 100;
  tTraj = (uint32_t)((uint64_t)(millis() - t0Reproducao) * escala / 100);

  const uint32_t dur = trajDuracaoMs();

  if (tTraj >= dur) {
    seguirSetpoint(buffer[nPontos - 1].p1, buffer[nPontos - 1].p2,
                   velSeguir1, velSeguir2);
    soldaDefinir(buffer[nPontos - 1].solda != 0);
    if (!motoresEmMovimento()) {
      soldaDesligar();
      trajPararReproducao();
      definirMensagem("Trajetoria concluida");
    }
    return;
  }

  // Avanca o indice do segmento que contem tTraj.
  while (idxSegmento + 1 < nPontos && buffer[idxSegmento + 1].tMs <= tTraj) {
    idxSegmento++;
  }
  if (idxSegmento + 1 >= nPontos) return;

  const Waypoint& a = buffer[idxSegmento];
  const Waypoint& b = buffer[idxSegmento + 1];
  const uint32_t dt = b.tMs - a.tMs;

  float alfa = 0.0f;
  if (dt > 0) alfa = (float)(tTraj - a.tMs) / (float)dt;
  if (alfa < 0.0f) alfa = 0.0f;
  if (alfa > 1.0f) alfa = 1.0f;

  const long alvo1 = a.p1 + (long)((b.p1 - a.p1) * alfa);
  const long alvo2 = a.p2 + (long)((b.p2 - a.p2) * alfa);

  seguirSetpoint(alvo1, alvo2, velSeguir1, velSeguir2);
  soldaDefinir(a.solda != 0);
}

void trajPararReproducao() {
  if (!reproduzindo) return;
  reproduzindo = false;
  soldaDesligar();
  pararSuave();
  aplicarAceleracao();
  aplicarVelocidadeManual();
}
