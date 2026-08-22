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

// ---------------------------------------------------------------------
// CARTAO MICRO SD (modulo adaptador TF de 6 pinos, SPI)
// ---------------------------------------------------------------------
// O modulo pequeno de 6 pinos (o azul com os quatro resistores 103) e
// 3,3 V PURO: nao tem regulador nem conversor de nivel. Ligue em 3V3.
// Em 5 V o cartao morre.
//
//   modulo      ESP32          observacao
//   -------     -----------    --------------------------------------
//   3V3         3V3            NUNCA no 5V deste modulo
//   GND         GND
//   CS          GPIO 5         tem pull-up interno, seguro no boot
//   SCK         GPIO 14
//   MOSI        GPIO 13
//   MISO        GPIO 12 NAO!   -> GPIO 25 (ver abaixo)
//
// ATENCAO AO GPIO 12. A pinagem "padrao" do HSPI usa 12 para MISO, e o
// modulo tem pull-up de 10k nessa linha. O GPIO 12 e strapping (MTDI):
// alto no boot programa o regulador do flash para 1,8 V e a placa nao
// da mais boot. Por isso o MISO vai para o 25. O ESP32 remapeia SPI por
// matriz de GPIO, entao nao ha perda nenhuma.
//
// Ponha 10 uF ceramico entre 3V3 e GND junto ao modulo: o cartao puxa
// picos de ~100 mA na escrita e a queda de tensao derruba a montagem.
#ifndef PIN_SD_CS
#define PIN_SD_CS    5
#endif
#ifndef PIN_SD_SCK
#define PIN_SD_SCK   14
#endif
#ifndef PIN_SD_MOSI
#define PIN_SD_MOSI  13
#endif
#ifndef PIN_SD_MISO
#define PIN_SD_MISO  25
#endif

// Deixe false para compilar sem nenhum codigo de cartao (a maquina
// continua funcionando exatamente como antes, so em NVS).
#ifndef CARTAO_INSTALADO
#define CARTAO_INSTALADO  true
#endif


// 20 MHz e conservador e funciona com cabo de protoboard. Suba para
// 40000000 so com fiacao curta e soldada.
static const uint32_t SD_FREQ_HZ = 20000000;

// LED de status. Deixe 255 para desligar (necessario quando o rele esta
// usando o GPIO 2, senao os dois brigam pelo mesmo pino).
#define PIN_LED_STATUS    255

// Nivel logico do sinal ALM quando existe falha no driver
#define ALARME_ATIVO_EM   LOW
// Mude para true depois de instalar o botao fisico de emergencia.
// (o #ifndef permite o banco de testes compilar com o botao "instalado"
// para exercitar esse ramo sem mexer no valor de producao)
#ifndef ESTOP_FISICO_INSTALADO
#define ESTOP_FISICO_INSTALADO  false
#endif

// Mude para true SOMENTE depois de ligar os fios ALM dos drivers e os
// pull-ups de 10k. Com false o firmware ignora esses pinos.
// ATENCAO: com um pino de entrada solto, o ESP32 le ruido. Se este flag
// ficar true sem a fiacao, o sistema entra em falha e recusa todo
// comando - foi exatamente esse o travamento da v2.1.
#ifndef ALARME_FISICO_INSTALADO
#define ALARME_FISICO_INSTALADO false
#endif

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

// Margem de seguranca aplicada aos limites de curso calibrados.
// gravidadeViolacao() usa EXATAMENTE esta mesma margem: se as duas contas
// divergirem, existe uma faixa onde a postura e invalida e a gravidade e
// zero, e o jog de recuperacao nunca libera o movimento de volta.
static const float MARGEM_LIMITE_GRAUS = 0.5f;

// Curso minimo que a calibracao aceita por junta. Precisa ser bem maior
// que 2 x MARGEM_LIMITE_GRAUS, senao a calibracao "valida" produz um
// intervalo util negativo e tranca o eixo.
static const float CURSO_MINIMO_GRAUS = 5.0f;

// Resolucao da validacao de um caminho interpolado nas juntas.
// Os limites de curso sao caixas no espaco das juntas (a reta entre dois
// pontos validos fica valida), mas o envelope cartesiano NAO e convexo
// nesse espaco: o interior do caminho precisa ser verificado.
static const float PASSO_VALIDACAO_GRAUS = 2.0f;

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
// ARQUIVOS
// ---------------------------------------------------------------------
static const uint8_t  MAX_NOME_ARQ    = 24;   // sem extensao, sem caminho
static const uint8_t  MAX_ARQ_LISTA   = 40;   // arquivos mostrados por pasta
static const uint8_t  MAX_LOG_SESSOES = 40;   // arquivos de log antes de girar

// Zona morta do joystick: abaixo disso o eixo fica parado. Sem ela o
// dedo tremendo no centro do disco manda pulso o tempo todo.
static const float JOY_ZONA_MORTA = 0.12f;
// Fracao minima de velocidade quando o joystick sai da zona morta: o
// movimento comeca perceptivel em vez de "quase parado".
static const float JOY_FRACAO_MIN = 0.05f;

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
  CMD_CALIB_CANCELAR,

  // Joystick: f1 e f2 sao a fracao de velocidade de cada junta, de -1 a
  // +1. Um comando so para os dois eixos - metade das requisicoes HTTP
  // do heartbeat comparado a mandar CMD_JOG por eixo.
  CMD_JOG_XY,

  // Arquivos. O 'nome' do Comando carrega o nome do arquivo.
  CMD_ARQ_SALVAR_PROG,     // core 1: copia pontos -> staging e pede a gravacao
  CMD_ARQ_APLICAR_PROG,    // core 1: staging -> pontos (postado pela tarefa SD)
  CMD_ARQ_SALVAR_TRAJ,     // core 1: empresta o buffer e pede a gravacao
  CMD_ARQ_CARREGAR_TRAJ,   // core 1: empresta o buffer e pede a leitura
  CMD_ARQ_LIBERAR_TRAJ,    // core 1: devolve o buffer (postado pela tarefa SD)
  CMD_ARQ_SALVAR_CONFIG    // core 1: prepara a area e pede a gravacao
};

struct Comando {
  TipoComando tipo;
  int32_t a;
  int32_t b;
  float   f1;
  float   f2;
  // Nome de arquivo, para os comandos de armazenamento. Vazio nos demais.
  char    nome[MAX_NOME_ARQ + 1];
};

struct Waypoint {
  uint32_t tMs;    // tempo relativo ao inicio da gravacao
  int32_t  p1;     // passos da junta 1
  int32_t  p2;     // passos da junta 2
  uint8_t  solda;  // estado do rele neste ponto
};
