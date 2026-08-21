#pragma once
#include <Arduino.h>

// =====================================================================
//  RoboCNC 2DOF - Configuracao central
//  Hardware alvo: ESP32 + 2x driver HLTNC T3D-L20A + servo 80AST-A1C04025
// =====================================================================

// ---------------------------------------------------------------------
// PINOS
// ---------------------------------------------------------------------
// Saidas de pulso/direcao (vao para PUL+/DIR+ dos drivers via buffer 5V)
#define PIN_J1_PULSO      16
#define PIN_J1_DIR        17
#define PIN_J2_PULSO      18
#define PIN_J2_DIR        19

// Habilitacao dos servos (SON dos dois drivers, via optoacoplador)
#define PIN_SERVO_ON      23

// Alarme dos drivers (ALM). GPIO 34/35 sao SOMENTE ENTRADA e nao possuem
// pull-up interno: use pull-up externo de 10k para 3V3.
#define PIN_ALARME_J1     34
#define PIN_ALARME_J2     35

// Rele da solda. Ativo em nivel ALTO, com pull-down fisico de 10k para GND
// (o GPIO flutua durante o boot do ESP32).
//
// GPIO 2 = LED da propria placa. Util para BANCADA: da para ver o rele
// "acionando" sem nada ligado. Para a maquina de verdade troque para 26:
// o GPIO 2 e strapping pin e pisca sozinho durante o boot, o que num
// rele de solda significa abrir arco na hora de energizar.
#define PIN_RELE_SOLDA    2

// Botao de emergencia fisico (contato NC -> LOW significa emergencia)
#define PIN_ESTOP         27

// LED de status. Deixe 255 para desligar (necessario quando o rele esta
// usando o GPIO 2, senao os dois brigam pelo mesmo pino).
#define PIN_LED_STATUS    255

// Nivel logico do sinal ALM quando existe falha no driver
#define ALARME_ATIVO_EM   LOW
// Mude para true depois de instalar o botao fisico de emergencia
#define ESTOP_FISICO_INSTALADO  false

// Mude para true SOMENTE depois de ligar os fios ALM dos drivers e os
// pull-ups de 10k. Com false o firmware ignora esses pinos.
// ATENCAO: com um pino de entrada solto, o ESP32 le ruido. Se este flag
// ficar true sem a fiacao, o sistema entra em falha e recusa todo
// comando - foi exatamente esse o travamento da v2.1.
#define ALARME_FISICO_INSTALADO false

// ---------------------------------------------------------------------
// LIMITES ELETRICOS / MECANICOS
// ---------------------------------------------------------------------
// Frequencia maxima de pulso aceita pelo driver em modo coletor aberto.
// Mantenha folga: nunca comande acima disso.
static const uint32_t FREQ_PULSO_MAX_HZ = 180000;

static const uint32_t PASSOS_POR_VOLTA_PADRAO = 10000;  // engrenagem eletronica do T3D
static const float    REDUCAO_PADRAO          = 1.0f;   // reducao mecanica da junta

static const uint32_t VEL_NORMAL_PADRAO   = 3000;
static const uint32_t VEL_PRECISAO_PADRAO = 500;
static const uint32_t VEL_AUTO_PADRAO     = 1200;
static const uint32_t VEL_SOLDA_PADRAO    = 600;    // legado (Hz)
// Velocidade do cordao em mm/s: e assim que se especifica solda, nao em
// pulsos. O firmware converte para pulsos resolvendo a cinematica.
static const float    VEL_CORDAO_PADRAO   = 5.0f;
static const float    PASSO_INTERP_MM     = 1.5f;  // resolucao da reta
static const uint32_t ACEL_PADRAO         = 8000;

// ---------------------------------------------------------------------
// GEOMETRIA E ANTI-COLISAO
// ---------------------------------------------------------------------
static const float ELO1_PADRAO_MM = 200.0f;
static const float ELO2_PADRAO_MM = 200.0f;

// Folga angular antes da dobra total do cotovelo.
// theta2 = 0 -> braco esticado (sem colisao).
// theta2 = +/-180 -> elo 2 dobrado sobre o elo 1 (colisao).
// Postura invalida quando |theta2| > 180 - FOLGA_DOBRA.
static const float FOLGA_DOBRA_PADRAO = 20.0f;

// Envelope cartesiano permitido (mm, origem no eixo da junta 1).
// Y_MIN protege a mesa / bancada.
static const float ENV_Y_MIN_PADRAO   = -150.0f;
static const float ENV_RAIO_MIN_PADRAO = 40.0f;   // zona morta em volta da base

// Margem de seguranca aplicada aos limites de curso calibrados
static const float MARGEM_LIMITE_GRAUS = 0.5f;

// Protecoes ligadas por padrao.
// A de curso vem da SUA calibracao, entao e confiavel: nasce ligada.
// A de envelope depende de voce ter informado o comprimento real dos
// elos. Com valores errados ela bloqueia o braco inteiro sem motivo
// aparente, entao nasce DESLIGADA e voce liga depois de conferir a
// geometria na tela.
static const bool PROT_CURSO_PADRAO    = true;
static const bool PROT_DOBRA_PADRAO    = true;
static const bool PROT_ENVELOPE_PADRAO = false;

// ---------------------------------------------------------------------
// TEMPOS
// ---------------------------------------------------------------------
static const uint32_t TIMEOUT_JOG_MS      = 350;    // sem heartbeat -> para o jog
static const uint32_t TIMEOUT_CONEXAO_MS  = 2500;   // sem contato HTTP -> parada segura
static const uint32_t TIMEOUT_ARCO_MS     = 60000;  // arco continuo maximo
static const uint32_t PERIODO_AMOSTRA_MS  = 25;     // taxa de gravacao (40 Hz)
static const uint16_t MAX_WAYPOINTS       = 1500;   // ~37 s de trajetoria continua
static const uint8_t  MAX_PONTOS          = 40;     // pontos do programa de solda
static const uint32_t DWELL_ABRE_ARCO_MS  = 400;    // espera o arco estabilizar
static const uint32_t DWELL_FECHA_ARCO_MS = 250;    // fecha a cratera no fim

// ---------------------------------------------------------------------
// WIFI
// ---------------------------------------------------------------------
static const char* const WIFI_AP_SSID  = "Robo2dof";
static const char* const WIFI_AP_SENHA = "12345678";

// ---------------------------------------------------------------------
// TIPOS
// ---------------------------------------------------------------------
enum Modo : uint8_t {
  MODO_MANUAL,
  MODO_GRAVANDO,
  MODO_REPRODUZINDO,
  MODO_EXECUTANDO,
  MODO_POSICIONANDO,
  MODO_CALIBRANDO,
  MODO_FALHA
};

enum EstadoCalib : uint8_t {
  CAL_INATIVO,
  CAL_HOME,
  CAL_J1_NEG,
  CAL_J1_VOLTA_NEG,
  CAL_J1_POS,
  CAL_J1_VOLTA_POS,
  CAL_J2_NEG,
  CAL_J2_VOLTA_NEG,
  CAL_J2_POS,
  CAL_J2_VOLTA_POS,
  CAL_CONCLUIDO
};

enum TipoComando : uint8_t {
  CMD_JOG,              // a = junta (1|2), b = direcao (-1|0|1)
  CMD_PARAR,
  CMD_PRECISAO,         // a = 0|1|-1 (-1 alterna)
  CMD_SERVOS,           // a = 0|1 (desliga/liga drivers)
  CMD_GRAVAR_INICIAR,
  CMD_GRAVAR_PARAR,
  CMD_REPRODUZIR,
  CMD_TRAJ_LIMPAR,
  CMD_SOLDA,            // a = 0|1
  CMD_TESTE_RELE,       // pulso curto de teste de bancada
  CMD_PONTO_GRAVAR,
  CMD_PONTO_REMOVER,    // a = indice
  CMD_PONTO_SOLDA,      // a = indice, b = 0|1
  CMD_PROG_LIMPAR,
  CMD_PROG_EXECUTAR,    // a = 1 para ensaio sem arco
  CMD_PROG_PARAR,
  CMD_IR_PARA_PONTO,    // a = indice
  CMD_APLICAR_CONFIG,   // salva em NVS e reaplica apos mudanca via web
  CMD_RESTAURAR_PADROES,
  CMD_MOVER_ANGULOS,    // f1 = theta1, f2 = theta2
  CMD_IR_HOME,
  CMD_CALIB_INICIAR,
  CMD_CALIB_CONFIRMAR,
  CMD_CALIB_CANCELAR
};

struct Comando {
  TipoComando tipo;
  int32_t a;
  int32_t b;
  float   f1;
  float   f2;
};

struct Waypoint {
  uint32_t tMs;    // tempo relativo ao inicio da gravacao
  int32_t  p1;     // passos da junta 1
  int32_t  p2;     // passos da junta 2
  uint8_t  solda;  // estado do rele neste ponto
};
