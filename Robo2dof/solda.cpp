#include "solda.h"
#include "estado.h"

static bool     ligada     = false;
static bool     permitida  = false;
static uint32_t inicioArco = 0;
static bool     emTeste    = false;
static uint32_t fimTeste   = 0;

// ---------------------------------------------------------------------
void soldaIniciar() {
  pinMode(PIN_RELE_SOLDA, OUTPUT);
  digitalWrite(PIN_RELE_SOLDA, LOW);
  ligada    = false;
  permitida = false;
}

void soldaDesligar() {
  emTeste = false;
  if (ligada) {
    digitalWrite(PIN_RELE_SOLDA, LOW);
    ligada = false;
    Serial.println("[SOLDA] Rele desligado.");
  } else {
    // Reforca o nivel mesmo se o estado interno ja indicava desligado.
    digitalWrite(PIN_RELE_SOLDA, LOW);
  }
}

void soldaDefinir(bool ligar) {
  if (!ligar) { soldaDesligar(); return; }

  if (!permitida) {
    if (ligada) soldaDesligar();
    definirMensagem("Solda bloqueada: condicao de seguranca nao atendida");
    return;
  }
  if (!ligada) {
    inicioArco = millis();
    digitalWrite(PIN_RELE_SOLDA, HIGH);
    ligada = true;
    Serial.println("[SOLDA] Rele ligado.");
  }
}

bool soldaLigada() { return ligada; }

void soldaTestar(uint32_t duracaoMs) {
  if (duracaoMs > 3000) duracaoMs = 3000;
  inicioArco = millis();
  fimTeste   = inicioArco + duracaoMs;
  emTeste    = true;
  ligada     = true;
  digitalWrite(PIN_RELE_SOLDA, HIGH);
  Serial.printf("[SOLDA] TESTE DE SAIDA: pino %d em nivel alto por %lu ms\n",
                (int)PIN_RELE_SOLDA, (unsigned long)duracaoMs);
  definirMensagem("Teste de saida: rele acionado por %lu ms",
                  (unsigned long)duracaoMs);
}

void soldaPermitir(bool permitido) {
  permitida = permitido;
  // O pulso de teste tem tempo proprio e nao e cortado pelo
  // intertravamento - senao seria impossivel testar a fiacao antes de
  // ter servos, drivers e emergencia ligados.
  if (emTeste) return;
  if (!permitido && ligada) {
    soldaDesligar();
    definirMensagem("Solda cortada: intertravamento de seguranca");
  }
}

void soldaAtualizar() {
  if (emTeste) {
    if ((int32_t)(millis() - fimTeste) >= 0) {
      emTeste = false;
      soldaDesligar();
      definirMensagem("Teste de saida concluido");
    }
    return;
  }
  if (ligada && (millis() - inicioArco > TIMEOUT_ARCO_MS)) {
    soldaDesligar();
    definirMensagem("Solda cortada: tempo maximo de arco excedido");
  }
}
