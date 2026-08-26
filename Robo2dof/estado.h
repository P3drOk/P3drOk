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

  // Angulo REAL da junta na posicao de referencia da calibracao.
  //
  // O contador de passos nasce em zero no HOME, mas zero passo nao
  // significa zero grau: a cinematica direta assume theta1 = 0 com o elo
  // 1 apontando para +X (braco esticado na horizontal, para a direita).
  // Se a referencia foi gravada em outra postura, o desenho na tela sai
  // girado em relacao ao braco de verdade. Este offset, em GRAUS,
  // reconcilia os dois -- e por ser em graus ele sobrevive a uma
  // correcao de resolucao.
  float grausHome     = 0.0f;

  float    aceleracao = ACEL_PADRAO;   // graus/s2
  bool     alarme     = false;

  // Sentido do eixo. Se a fiacao do DIR estiver invertida em relacao ao
  // que a cinematica espera, o braco vai para um lado e o desenho para o
  // outro -- e nenhuma calibracao conserta isso, porque o erro nao e de
  // escala, e de sinal. Marque aqui em vez de trocar fio.
  //
  // A cinematica espera angulo CRESCENTE no sentido anti-horario, com
  // theta1 = 0 apontando para +X (direita).
  bool     inverterDir = false;
};

extern Junta J1;
extern Junta J2;

// ---------------------------------------------------------------------
// Parametros de configuracao (persistidos em NVS)
// ---------------------------------------------------------------------
// Em graus por segundo. Ver a nota em config.h: Hz nao servia porque
// significa velocidades diferentes em juntas com engrenagens diferentes.
extern float velNormal;
extern float velPrecisao;
extern float velAuto;
extern float velCordaoMmS;   // mm/s: o cordao ja e especificado assim

extern float elo1Mm;
extern float elo2Mm;
extern float folgaDobra;
extern float envYMin;
extern float envRaioMin;

extern bool protCurso;
extern bool protDobra;
extern bool protEnvelope;

extern uint16_t escalaVelocidadeTraj;  // 10..200 (%)
// Passos ao longo dos quais a aceleracao sobe de zero ate o valor cheio.
// E o que tira o tranco da partida. 0 = rampa trapezoidal pura.
extern uint8_t  suavidadePartida;

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
// Heartbeat do operador.
//
// Alimentado por todo handler HTTP. Sem contato por TIMEOUT_CONEXAO_MS
// o supervisor corta movimento e arco: interface fechada ou Wi-Fi caido
// nao pode deixar o braco andando.
// ---------------------------------------------------------------------
extern volatile uint32_t ultimoContatoOperadorMs;
void registrarContatoOperador();

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
  float    velNormal, velPrecisao, velAuto;   // graus/s
  float    velCordaoMmS;                      // mm/s
  float    acel1, acel2;                      // graus/s2
  uint32_t ppv1, ppv2;
  float    red1, red2;
  bool     inv1, inv2;
  uint16_t escalaTraj;
  uint8_t  suavidade;
  float    elo1, elo2, folgaDobra, envY, envRaio;
  bool     protCurso, protDobra, protEnvelope;

  // ---- CALIBRACAO -----------------------------------------------------
  // O backup de configuracao guardava o curso das juntas so como
  // comentario, para conferencia. Isso fazia o arquivo parecer um backup
  // da maquina sem ser um: restaurar devolvia velocidades e elos, e
  // deixava o operador refazendo o assistente de calibracao -- que e,
  // de longe, a parte mais demorada de por a maquina de pe.
  //
  // 'temCalib' distingue arquivo novo de arquivo antigo: sem esta marca
  // um backup gravado pela versao anterior zeraria a calibracao viva.
  bool     temCalib;
  bool     cal1, cal2;
  long     p1Min, p1Max, p2Min, p2Max;
  float    home1, home2;
};
extern ConfigPendente configPendente;

// ---------------------------------------------------------------------
// Encoder por Modbus.
//
// Um barramento RS485 e os dois drivers nele, com enderecos diferentes.
// Velocidade, paridade e funcao sao do barramento; endereco de escravo,
// registrador e contagens por volta sao de cada junta.
//
// O registrador e configuravel porque o mapa Modbus do T3D nao esta
// publicado. Numero fixo no codigo seria adivinhacao.
// ---------------------------------------------------------------------
struct ConfigEncoder {
  bool     ativo;
  uint32_t baud;
  uint8_t  paridade;        // 0=8N1 1=8E1 2=8O1
  uint8_t  funcao;          // 3 = holding, 4 = input registers
  uint16_t periodoMs;
  bool     trintaEDois;     // posicao em 2 registradores (32 bits)
  bool     baixaPrimeiro;   // palavra baixa vem antes da alta
  uint8_t  id[2];           // endereco Modbus de cada junta
  uint16_t reg[2];          // registrador da posicao de cada junta
  float    contagensPorVolta[2];   // do ENCODER, por volta do MOTOR
};
extern ConfigEncoder configEncoder;      // vivo, so o core 1 escreve

// Assentamento pelo encoder. Ver correcao.h para as regras.
struct ConfigCorrecao {
  bool  ativa;             // assentar no fim de cada movimento
  float toleranciaGraus;   // abaixo disto ja esta bom, nao retoca
  float maxCorrecaoGraus;  // acima disto NAO retoca: denuncia
  uint8_t tentativas;      // quantos retoques antes de desistir
  bool  vigiar;            // avisar quando o erro passar do limite andando
  float alertaGraus;       // limite da vigilancia
};
extern ConfigCorrecao configCorrecao;

// ---------------------------------------------------------------------
// ENCODER ABSOLUTO: a maquina sabe onde esta assim que liga
//
// O encoder do servo guarda a posicao com a maquina desligada. Se alguem
// empurrar o braco a mao com tudo apagado, ao ligar ele sabe. Isso muda
// a natureza da calibracao:
//
//   ANTES  o zero era "onde o braco estava quando ligou", e o operador
//          tinha de leva-lo ate a referencia toda vez. Sem fim de curso,
//          a unica protecao era ele lembrar.
//   AGORA  o zero e um NUMERO GRAVADO -- a contagem crua do encoder que
//          corresponde a 0 grau. Ensina-se uma vez; dali em diante a
//          maquina se localiza sozinha em todo boot, sem fim de curso e
//          sem procurar batente.
// ---------------------------------------------------------------------
struct ConfigZero {
  bool  sincronizar;    // no boot, acertar a contagem pelo encoder
  bool  irParaZero;     // e depois levar o braco para 0 grau
  float toleranciaGraus;// abaixo disso ja esta no zero, nao move
  // A referencia so vale se alguem a ENSINOU. Sem isto, uma maquina
  // recem-montada acreditaria que a contagem crua 0 do encoder e o zero
  // da junta -- um numero arbitrario -- e iria para la sozinha ao ligar.
  // De fabrica: nao ensinado, e a maquina se comporta como antes.
  bool  ensinado[2];
};
extern ConfigZero configZero;
extern ConfigEncoder encoderPendente;    // area de preparo, core 0 enche
void aplicarEncoderPendente();           // core 1: grava e reconfigura

void prepararConfigPendente();   // core 0: copia o estado vivo para a area
void aplicarConfigPendente();    // core 1: copia de volta e recalcula

// ---------------------------------------------------------------------
// MODO OPERADOR x TECNICO
//
// Na producao, quem esta na maquina o dia inteiro precisa de quatro
// botoes: executar, pausar, repetir, parar. Calibracao, resolucao,
// registrador Modbus e sentido dos eixos sao ajustes de instalacao -- e
// um toque errado neles no meio do turno para a linha.
//
// O modo operador ESCONDE esses paineis. Sair dele pede uma senha curta.
//
// O QUE ISTO E, E O QUE NAO E. E uma trava contra toque errado, nao e
// seguranca de rede: a maquina serve seu proprio Wi-Fi e quem estiver
// nele alcanca a API direto, sem passar pela tela. Tratar isto como
// senha de verdade seria mentir para quem compra.
// ---------------------------------------------------------------------
struct ConfigPainel {
  bool operador;      // comeca no modo operador a cada boot
  char senha[9];      // curta, so para nao ser um toque acidental
};
extern ConfigPainel configPainel;

// ---------------------------------------------------------------------
// PRODUCAO: quantas pecas esta maquina ja fez
//
// Um numero que o dono da maquina usa e um numero que o firmware nao
// tinha: sem ele nao da para dizer se um problema apareceu na peca 10 ou
// na 3000, nem quando trocar bico e difusor. Vive no NVS, sobrevive a
// queda de energia, e so o core 1 escreve.
//
// Ciclo = uma execucao COM ARCO que chegou ao fim. Ensaio nao conta --
// ensaio nao gasta consumivel nem produz peca. Programa abortado no meio
// tambem nao: peca pela metade nao e peca.
// ---------------------------------------------------------------------
struct Producao {
  uint32_t ciclosTotais;    // desde sempre
  uint32_t ciclosSessao;    // desde este boot
  uint32_t abortados;       // execucoes com arco que nao chegaram ao fim
  uint32_t horasArcoS;      // segundos de arco aberto, acumulados
  uint32_t desdeManutencao; // ciclos desde a ultima manutencao zerada
};
extern Producao producao;

void producaoContarCiclo(bool concluido);   // core 1
void producaoZerarManutencao();             // core 1
void producaoSomarArco(uint32_t ms);        // core 1: chamado pelo rele

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
