#include "controle_bt.h"
#include "estado.h"
#include <math.h>

#if BLUETOOTH_INSTALADO

#define DABBLE_EXPERIMENTAL
#include <DabbleESP32.h>

static bool     conectado      = false;
static uint32_t ultimoJogMs    = 0;   // ultimo comando de jog emitido
static bool     jogAtivo       = false;
static uint32_t ultimoBotaoMs  = 0;
static bool     precisaoAntes  = false;   // so para o log

// O Dabble entrega o analogico em raio 0..7 e angulo em graus.
// Convertido para as mesmas fracoes de -1 a +1 que o joystick da tela
// manda: um so caminho de codigo no firmware para as duas interfaces.
static void lerAnalogico(float& fx, float& fy) {
  const float raio = (float)GamePad.getRadius();
  const float ang  = (float)GamePad.getAngle() * (float)M_PI / 180.0f;
  float r = raio / 7.0f;
  if (r > 1.0f) r = 1.0f;
  fx = r * cosf(ang);
  fy = r * sinf(ang);
}

static bool debounce(uint32_t agora) {
  if (agora - ultimoBotaoMs < BT_DEBOUNCE_MS) return false;
  ultimoBotaoMs = agora;
  return true;
}

void btIniciar() {
  Dabble.begin(BT_NOME);
  Serial.printf("[BT] Bluetooth ativo. No aplicativo Dabble, GamePad, "
                "conecte em \"%s\".\n", BT_NOME);
}

bool        btConectado() { return conectado; }
const char* btNome()      { return BT_NOME; }

void btAtualizar() {
  Dabble.processInput();

  const bool agoraConectado = Dabble.isAppConnected();
  if (agoraConectado != conectado) {
    conectado = agoraConectado;
    Serial.printf("[BT] Aplicativo %s\n", conectado ? "conectado" : "desconectado");
    if (!conectado) {
      // Sem app nao ha operador: zera o jog na hora, sem esperar o
      // timeout. O heartbeat de 350 ms do firmware e a segunda rede.
      enviarComando(CMD_JOG_XY, 0, 0, 0.0f, 0.0f);
      jogAtivo = false;
    }
  }
  if (!conectado) return;

  // Operador presente conta como contato: sem isto, quem usa so o
  // gamepad veria o supervisor cortar o movimento em 2,5 s por
  // "conexao perdida".
  registrarContatoOperador();

  const uint32_t agora = millis();

  // ---- PARADA: fora da fila, igual ao botao PARAR da tela ----
  if (GamePad.isCrossPressed()) {
    solicitarParada();
    jogAtivo = false;
    return;                       // nada mais e processado neste ciclo
  }

  // ---- jog: direcional em velocidade cheia, analogico proporcional ----
  float fx = 0.0f, fy = 0.0f;
  if      (GamePad.isRightPressed()) fx =  1.0f;
  else if (GamePad.isLeftPressed())  fx = -1.0f;
  if      (GamePad.isUpPressed())    fy =  1.0f;
  else if (GamePad.isDownPressed())  fy = -1.0f;

  if (fx == 0.0f && fy == 0.0f) lerAnalogico(fx, fy);

  const bool querMover = (fabsf(fx) >= JOY_ZONA_MORTA || fabsf(fy) >= JOY_ZONA_MORTA);
  if (querMover) {
    // Reemite a cada ciclo: e o heartbeat que o firmware exige.
    enviarComando(CMD_JOG_XY, 0, 0, fx, fy);
    ultimoJogMs = agora;
    jogAtivo    = true;
  } else if (jogAtivo) {
    // Soltou o direcional: manda o zero uma vez. Se ele se perder, o
    // heartbeat de 350 ms do firmware para o eixo de qualquer forma.
    enviarComando(CMD_JOG_XY, 0, 0, 0.0f, 0.0f);
    jogAtivo = false;
    ultimoJogMs = agora;
  }

  // ---- botoes ----
  if (GamePad.isTrianglePressed() && debounce(agora)) {
    enviarComando(CMD_PRECISAO, -1);
    precisaoAntes = !precisaoAntes;
    Serial.printf("[BT] triangulo -> modo precisao %s\n",
                  precisaoAntes ? "ligado" : "desligado");
  }
  else if (GamePad.isSquarePressed() && debounce(agora)) {
    enviarComando(CMD_PONTO_GRAVAR);
    Serial.println("[BT] quadrado -> gravar ponto");
  }
  else if (GamePad.isCirclePressed() && debounce(agora)) {
    enviarComando(CMD_IR_HOME);
    Serial.println("[BT] circulo -> ir para o zero");
  }
  else if (GamePad.isStartPressed() && debounce(agora)) {
    // Sempre ENSAIO. Executar com arco exige a confirmacao da tela.
    enviarComando(CMD_PROG_EXECUTAR, 1);
    Serial.println("[BT] start -> ensaio (sem arco)");
  }
  else if (GamePad.isSelectPressed() && debounce(agora)) {
    enviarComando(CMD_SERVOS, servosLigados ? 0 : 1);
    Serial.printf("[BT] select -> %s servos\n", servosLigados ? "desabilitar" : "habilitar");
  }
}

#else   // ------------------------------------------------ sem bluetooth

void btIniciar() {}
void btAtualizar() {}
bool        btConectado() { return false; }
const char* btNome()      { return ""; }

#endif
