#include "ota.h"
#include "motores.h"
#include "solda.h"
#include "programa.h"
#include "trajetoria.h"
#include <Update.h>
#include <string.h>

static ResumoOta r;
static bool      reiniciar    = false;
static uint32_t  reiniciarEm  = 0;

static void dizer(const char* m) {
  strncpy(r.motivo, m ? m : "", sizeof(r.motivo) - 1);
  r.motivo[sizeof(r.motivo) - 1] = '\0';
}

// ---------------------------------------------------------------------
// Ha para onde escrever?
//
// ESP.getFreeSketchSpace() devolve o TAMANHO DA PARTICAO DE OTA, e zero
// quando nao existe nenhuma -- que e o caso do partitions.csv de 3 MB
// deste projeto. Nao e uma estimativa: e a mesma pergunta que o Update
// faz antes de comecar.
// ---------------------------------------------------------------------
bool otaDisponivel() { return ESP.getFreeSketchSpace() > 0; }

ResumoOta otaResumo() {
  ResumoOta s = r;
  s.disponivel = otaDisponivel();
  s.espaco     = ESP.getFreeSketchSpace();
  return s;
}

// ---------------------------------------------------------------------
bool otaComecar(const char** motivo) {
  if (!otaDisponivel()) {
    if (motivo) *motivo = "este firmware foi gravado sem particao de OTA "
                          "(ver partitions_ota.csv)";
    return false;
  }
  if (r.estado == OTA_RECEBENDO) {
    if (motivo) *motivo = "ja ha uma atualizacao em andamento";
    return false;
  }
  if (modoAtual != MODO_MANUAL || progRodando() || trajReproduzindo()) {
    if (motivo) *motivo = "atualize com o robo parado no modo manual";
    return false;
  }
  if (motoresEmMovimento()) {
    if (motivo) *motivo = "espere o braco parar";
    return false;
  }
  if (soldaLigada()) {
    if (motivo) *motivo = "desligue o arco antes de atualizar";
    return false;
  }

  // O ESP32 reinicia no fim da gravacao. Driver habilitado com o gerador
  // de pulso morto e um eixo que ninguem esta comandando -- e o reinicio
  // dura o bastante para isso importar.
  servosHabilitar(false);
  soldaDesligar();

  if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
    r.estado = OTA_ERRO;
    dizer(Update.errorString());
    if (motivo) *motivo = r.motivo;
    return false;
  }
  r.estado   = OTA_RECEBENDO;
  r.recebido = 0;
  dizer("");
  definirMensagem("Recebendo firmware novo. Nao desligue a maquina");
  return true;
}

bool otaPedaco(const uint8_t* dados, size_t n) {
  if (r.estado != OTA_RECEBENDO) return false;
  if (Update.write((uint8_t*)dados, n) != n) {
    otaCancelar(Update.errorString());
    return false;
  }
  r.recebido += (uint32_t)n;
  return true;
}

bool otaTerminar(const char** motivo) {
  if (r.estado != OTA_RECEBENDO) {
    if (motivo) *motivo = r.motivo[0] ? r.motivo : "nenhuma atualizacao em andamento";
    return false;
  }
  // true = confirma que a imagem esta completa. O Update confere o
  // tamanho declarado no cabecalho antes de marcar a particao como
  // bootavel: imagem cortada no meio nao vira firmware de arranque.
  if (!Update.end(true)) {
    otaCancelar(Update.errorString());
    if (motivo) *motivo = r.motivo;
    return false;
  }
  r.estado = OTA_OK;
  dizer("gravado");
  // O reinicio NAO acontece aqui: a resposta ainda precisa chegar ao
  // navegador. Reiniciar dentro do handler deixaria o operador olhando
  // para uma requisicao que morreu, sem saber se deu certo.
  reiniciar   = true;
  reiniciarEm = millis() + 1200;
  definirMensagem("Firmware gravado: %lu kB. Reiniciando...",
                  (unsigned long)(r.recebido / 1024));
  return true;
}

void otaCancelar(const char* motivo) {
  if (r.estado == OTA_RECEBENDO) Update.abort();
  r.estado = OTA_ERRO;
  dizer(motivo);
  definirMensagem("Atualizacao interrompida: %s", motivo ? motivo : "erro");
}

bool otaPrecisaReiniciar() {
  return reiniciar && (int32_t)(millis() - reiniciarEm) >= 0;
}

void otaReiniciarAgora() {
  reiniciar = false;
  ESP.restart();
}
