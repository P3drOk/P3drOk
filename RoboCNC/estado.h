#pragma once
#include "config.h"
#include <FastAccelStepper.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// =====================================================================
//  Estado global do robo.
//
//  REGRA DE OURO DESTE PROJETO:
//  - O core 1 (loop) e o UNICO que pode tocar em FastAccelStepper.
//  - O core 0 (servidor web) so envia Comando pela fila e le o Snapshot.
//  Nunca chame nada de motores.h a partir de um handler HTTP.
// =====================================================================

// ---------------------------------------------------------------------
// Junta
// ---------------------------------------------------------------------
struct Junta {
  FastAccelStepper* motor = nullptr;
  uint8_t  pinoPulso  = 0;
  uint8_t  pinoDir    = 0;
  uint8_t  pinoAlarme = 0;

  bool  calibrada     = false;
  float passosPorGrau = 0.0f;
  // Resolucao POR EIXO: cada junta pode ter engrenagem eletronica e
  // reducao mecanica diferentes da outra.
  uint32_t passosPorVolta = PASSOS_POR_VOLTA_PADRAO;
  float    reducao        = REDUCAO_PADRAO;
  long  passosMin     = 0;      // limite de curso negativo, em passos
  long  passosMax     = 0;      // limite de curso positivo, em passos
  float grausMin      = -90.0f;
  float grausMax      =  90.0f;

  uint32_t aceleracao = ACEL_PADRAO;
  bool     alarme     = false;
};

extern Junta J1;
extern Junta J2;

// ---------------------------------------------------------------------
// Parametros de configuracao (persistidos em NVS)
// ---------------------------------------------------------------------
extern uint32_t velNormal;
extern uint32_t velPrecisao;
extern uint32_t velAuto;
extern float    velCordaoMmS;

extern float elo1Mm;
extern float elo2Mm;
extern float folgaDobra;
extern float envYMin;
extern float envRaioMin;

extern bool protCurso;
extern bool protDobra;
extern bool protEnvelope;

extern uint16_t escalaVelocidadeTraj;  // 10..200 (%)

// ---------------------------------------------------------------------
// Estado volatil
// ---------------------------------------------------------------------
extern Modo         modoAtual;
extern EstadoCalib  estadoCalib;
extern bool         modoPrecisao;
extern bool         servosLigados;
extern char         ultimaMensagem[96];

// ---------------------------------------------------------------------
// Fila de comandos (web -> loop)
// ---------------------------------------------------------------------
extern QueueHandle_t filaComandos;
bool enviarComando(TipoComando tipo, int32_t a = 0, int32_t b = 0,
                   float f1 = 0.0f, float f2 = 0.0f);
// Mesma fila, carregando um nome de arquivo. O nome e copiado para
// dentro do Comando: nada de ponteiro atravessando nucleo.
bool enviarComandoNomeado(TipoComando tipo, const char* nome,
                          int32_t a = 0, int32_t b = 0);

// ---------------------------------------------------------------------
// PARADA: caminho fora da fila.
//
// A fila de comandos e compartilhada com o heartbeat de jog (uma
// mensagem a cada 100 ms por eixo). Se ela encher, xQueueSend descarta -
// e uma parada de emergencia que depende de haver espaco em buffer nao e
// uma parada de emergencia. Esta flag e escrita direto pelo handler HTTP
// e testada no topo do loop(), antes de drenar a fila.
// ---------------------------------------------------------------------
extern volatile bool pedidoParada;
void solicitarParada();

// Descarta o que estiver enfileirado. Depois de uma parada os heartbeats
// de jog que ja estavam na fila religariam o movimento no mesmo ciclo.
void limparFilaComandos();

// ---------------------------------------------------------------------
// Portao unico de movimento.
//
// Escrito por supervisionar() (core 1) todo ciclo, consultado por todo
// caminho que possa mover um motor. Falso quando faltam servos, ha
// alarme de driver, emergencia acionada, conexao perdida ou o sistema
// esta em falha.
//
// Sem isso o firmware gera pulsos para um driver desabilitado: o eixo
// nao anda, mas o contador de passos anda - e e nesse contador que toda
// a protecao de curso se apoia.
// ---------------------------------------------------------------------
extern bool movimentoLiberado;

// ---------------------------------------------------------------------
// Heartbeat de conexao (alimentado pelos handlers HTTP)
// ---------------------------------------------------------------------
extern volatile uint32_t ultimoContatoWebMs;
void registrarContatoWeb();

// ---------------------------------------------------------------------
// Snapshot publicado pelo loop e lido pela web
// ---------------------------------------------------------------------
struct Snapshot {
  uint8_t  modo;
  uint8_t  calib;
  long     p1, p2;
  float    t1, t2;
  float    x, y;
  // Velocidade instantanea, para a interface mostrar movimento real
  float    v1Hz, v2Hz;      // pulsos por segundo de cada junta
  float    vPontaMmS;       // velocidade da ponta no plano
  bool     precisao;
  bool     solda;
  bool     servosLigados;
  bool     alarme1, alarme2;
  bool     calibrada1, calibrada2;
  bool     emMovimento;
  uint16_t trajPontos;
  uint32_t trajDuracaoMs;
  uint8_t  trajProgresso;   // 0..100
  char     mensagem[96];
};

void publicarSnapshot(const Snapshot& s);
void lerSnapshot(Snapshot& destino);

void definirMensagem(const char* fmt, ...);

// ---------------------------------------------------------------------
// Area de preparo de configuracao.
//
// Os handlers HTTP rodam no core 0 e NAO podem escrever nas variaveis
// vivas: recalcularResolucao() altera passosPorGrau, grausMin e grausMax,
// que o core 1 le dentro de posturaValida() a cada ciclo. O handler
// preenche esta area, enfileira CMD_APLICAR_CONFIG, e o core 1 copia
// para as variaveis vivas num ponto seguro do ciclo.
// ---------------------------------------------------------------------
struct ConfigPendente {
  uint32_t velNormal, velPrecisao, velAuto;
  float    velCordaoMmS;
  uint32_t acel1, acel2;
  uint32_t ppv1, ppv2;
  float    red1, red2;
  uint16_t escalaTraj;
  float    elo1, elo2, folgaDobra, envY, envRaio;
  bool     protCurso, protDobra, protEnvelope;
};
extern ConfigPendente configPendente;

void prepararConfigPendente();   // core 0: copia o estado vivo para a area
void aplicarConfigPendente();    // core 1: copia de volta e recalcula

// ---------------------------------------------------------------------
// Persistencia
// ---------------------------------------------------------------------
void recalcularResolucao();
// Contador de partidas, guardado no NVS. O ESP32 nao tem relogio de
// tempo real: sem esse numero todo arquivo de log da maquina nasceria
// com o mesmo nome e o anterior seria sobrescrito a cada boot.
uint32_t proximaSessao();
void carregarConfiguracoes();
void salvarConfiguracoes();
void restaurarPadroes();
