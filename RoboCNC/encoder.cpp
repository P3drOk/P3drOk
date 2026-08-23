#include "encoder.h"
#include <HardwareSerial.h>
#include <string.h>

// UART2. Os pinos SEMPRE vao explicitos no begin(): o padrao da UART2 e
// GPIO 16 e 17, que neste projeto sao o passo e a direcao da junta 1.
static HardwareSerial rs(2);

static LeituraEncoder leitura[2];
static portMUX_TYPE   travaEnc = portMUX_INITIALIZER_UNLOCKED;

static bool     linhaAberta = false;
static uint32_t proximaEm   = 0;
static uint8_t  vez         = 0;      // qual junta ler no proximo ciclo
// O core 1 nao pode chamar rs.begin(): a UART e da tarefa do core 0, e
// reabrir no meio de uma leitura corrompe o quadro. Ele so deixa recado.
static volatile bool pedidoReabrir = false;

// ---------------------------------------------------------------------
static uint32_t cfgSerial(uint8_t paridade) {
  switch (paridade) {
    case 1:  return SERIAL_8E1;
    case 2:  return SERIAL_8O1;
    default: return SERIAL_8N1;
  }
}
static uint32_t bitsPorChar(uint8_t paridade) { return paridade ? 11 : 10; }

static uint32_t usPorChar() {
  const uint32_t b = configEncoder.baud ? configEncoder.baud : 19200;
  return (bitsPorChar(configEncoder.paridade) * 1000000UL) / b;
}
// Silencio de fim de quadro: 3,5 caracteres, piso de 1750 us.
static uint32_t usEntreQuadros() {
  const uint32_t t = (usPorChar() * 7) / 2;
  return t < 1750 ? 1750 : t;
}

static void abrirLinha() {
  if (linhaAberta) rs.end();
  rs.begin(configEncoder.baud, cfgSerial(configEncoder.paridade),
           PIN_RS485_RX, PIN_RS485_TX);
  linhaAberta = true;
}

static void modoEscuta()      { digitalWrite(PIN_RS485_DE, LOW);  digitalWrite(PIN_RS485_RE, LOW); }
static void modoTransmissao() { digitalWrite(PIN_RS485_RE, HIGH); digitalWrite(PIN_RS485_DE, HIGH); }

// ---------------------------------------------------------------------
static uint16_t crc16(const uint8_t* b, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= b[i];
    for (uint8_t k = 0; k < 8; k++)
      crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
  }
  return crc;
}

// Um ciclo de pergunta e resposta. Devolve os bytes recebidos.
static size_t trocar(const uint8_t* saida, size_t nSaida,
                     uint8_t* entrada, size_t maxEntrada) {
  while (rs.available()) rs.read();

  modoTransmissao();
  delayMicroseconds(50);
  rs.write(saida, nSaida);
  rs.flush();
  // flush() esvazia a fila, mas o ultimo bit ainda pode estar saindo do
  // registrador de deslocamento. Baixar DE agora corta o fim do quadro e
  // o escravo descarta calado.
  delayMicroseconds(usPorChar() * 2);
  modoEscuta();

  size_t n = 0;
  const uint32_t limite = millis() + ENC_TIMEOUT_MS;
  uint32_t ultimoUs = micros();

  while (millis() < limite && n < maxEntrada) {
    if (rs.available()) {
      entrada[n++] = (uint8_t)rs.read();
      ultimoUs = micros();
      continue;
    }
    if (n && (micros() - ultimoUs) > usEntreQuadros()) break;
    // Sem esta pausa o laco vira espera ocupada: ate 60 ms queimando o
    // core 0, que e o mesmo do servidor web. Um caractere a 19200 leva
    // 570 us, entao esperar 50 us entre olhadas nao perde byte nenhum.
    delayMicroseconds(50);
  }
  return n;
}

// ---------------------------------------------------------------------
// Le a posicao de uma junta. Devolve false se nao veio resposta boa.
// ---------------------------------------------------------------------
static bool lerPosicao(uint8_t i, int32_t& valor, uint8_t& motivo) {
  motivo = MOTIVO_OK;
  const uint16_t quantos = configEncoder.trintaEDois ? 2 : 1;

  uint8_t q[8];
  q[0] = configEncoder.id[i];
  q[1] = configEncoder.funcao;
  q[2] = (uint8_t)(configEncoder.reg[i] >> 8);
  q[3] = (uint8_t)(configEncoder.reg[i] & 0xFF);
  q[4] = (uint8_t)(quantos >> 8);
  q[5] = (uint8_t)(quantos & 0xFF);
  const uint16_t c = crc16(q, 6);
  q[6] = (uint8_t)(c & 0xFF);
  q[7] = (uint8_t)(c >> 8);

  uint8_t r[16];
  const size_t n = trocar(q, 8, r, sizeof(r));
  if (n < 5) { motivo = MOTIVO_SILENCIO; return false; }

  const uint16_t cc = crc16(r, n - 2);
  if (r[n - 2] != (uint8_t)(cc & 0xFF) || r[n - 1] != (uint8_t)(cc >> 8)) {
    motivo = MOTIVO_CRC; return false;
  }
  if (r[0] != configEncoder.id[i]) { motivo = MOTIVO_CRC; return false; }
  if (r[1] & 0x80) {
    // O driver respondeu "esse registrador nao existe". E informacao boa:
    // ele esta la, so o endereco esta errado.
    motivo = MOTIVO_EXCECAO; return false;
  }
  if (r[1] != configEncoder.funcao || r[2] != quantos * 2) {
    motivo = MOTIVO_FORMATO; return false;
  }

  if (quantos == 1) {
    valor = (int16_t)((r[3] << 8) | r[4]);       // com sinal: pode ser negativo
    return true;
  }
  const uint16_t p0 = (uint16_t)((r[3] << 8) | r[4]);
  const uint16_t p1 = (uint16_t)((r[5] << 8) | r[6]);
  // Muito driver Modbus manda a palavra BAIXA primeiro. Errar isto faz a
  // posicao dar saltos de dezenas de milhares.
  valor = configEncoder.baixaPrimeiro
        ? (int32_t)(((uint32_t)p1 << 16) | p0)
        : (int32_t)(((uint32_t)p0 << 16) | p1);
  return true;
}

// ---------------------------------------------------------------------
// Converte a contagem em angulo da JUNTA e compara com o comandado.
//
// O encoder conta no eixo do MOTOR; o comandado tambem esta em passos de
// motor. A reducao mecanica leva os dois para o mesmo lugar: graus da
// junta.
// ---------------------------------------------------------------------
static void publicar(uint8_t i, bool ok, int32_t bruto, uint8_t motivo) {
  const Junta& j = (i == 0) ? J1 : J2;
  const float  cv  = configEncoder.contagensPorVolta[i];
  const float  red = (j.reducao > 0.001f) ? j.reducao : 1.0f;

  // O angulo COMANDADO vem do Snapshot, nao de posicaoJ1(). Esta tarefa
  // roda no core 0, e motores.h e do core 1 -- a regra de ouro do
  // projeto. O Snapshot existe exatamente para isto.
  Snapshot s;
  lerSnapshot(s);
  const float comandado = (i == 0) ? s.t1 : s.t2;

  portENTER_CRITICAL(&travaEnc);
  leitura[i].motivo = motivo;
  if (ok) {
    leitura[i].bruto    = bruto;
    leitura[i].idadeMs  = 0;
    leitura[i].leituras++;
    if (cv > 0.5f) {
      const float voltasMotor = (float)(bruto - leitura[i].referencia) / cv;
      leitura[i].graus  = voltasMotor * 360.0f / red + j.grausHome;
      leitura[i].erro   = comandado - leitura[i].graus;
      leitura[i].valido = true;
    } else {
      leitura[i].valido = false;
    }
  } else {
    leitura[i].falhas++;
  }
  portEXIT_CRITICAL(&travaEnc);
}

// ---------------------------------------------------------------------
LeituraEncoder encoderLer(uint8_t junta) {
  const uint8_t i = (junta == 2) ? 1 : 0;
  LeituraEncoder copia;
  portENTER_CRITICAL(&travaEnc);
  copia = leitura[i];
  portEXIT_CRITICAL(&travaEnc);
  return copia;
}

void encoderZerar(uint8_t junta) {
  portENTER_CRITICAL(&travaEnc);
  for (uint8_t i = 0; i < 2; i++) {
    if (junta == 0 || junta == i + 1) {
      leitura[i].referencia = leitura[i].bruto;
      leitura[i].erro = 0.0f;
    }
  }
  portEXIT_CRITICAL(&travaEnc);
}

// Chamado pelo CORE 1. Nao toca no radio: so pede, e a tarefa do core 0
// reabre no comeco do proximo ciclo. Reabrir a UART por baixo de uma
// leitura em andamento corrompe o quadro -- e a mesma regra que separa
// motor de rede neste projeto.
void encoderReconfigurar() {
  portENTER_CRITICAL(&travaEnc);
  for (uint8_t i = 0; i < 2; i++) {
    leitura[i].leituras = 0;
    leitura[i].falhas   = 0;
    leitura[i].motivo   = MOTIVO_NUNCA;
    leitura[i].valido   = false;
  }
  portEXIT_CRITICAL(&travaEnc);
  pedidoReabrir = true;
}

// ---------------------------------------------------------------------
// Um ciclo. Le UMA junta por vez, alternando: assim o periodo de cada
// leitura e previsivel e o barramento nunca tem duas perguntas no ar.
// ---------------------------------------------------------------------
static void ciclo() {
  // Envelhece o que ja foi lido. Dado velho para de valer, senao a tela
  // mostra erro calculado em cima de leitura de um minuto atras.
  static uint32_t ultimoMs = 0;
  const uint32_t agora = millis();
  const uint32_t dt = ultimoMs ? (agora - ultimoMs) : 0;
  ultimoMs = agora;
  portENTER_CRITICAL(&travaEnc);
  for (uint8_t i = 0; i < 2; i++) {
    if (leitura[i].idadeMs < ENC_IDADE_MAX_MS * 4) leitura[i].idadeMs += dt;
    if (leitura[i].idadeMs > ENC_IDADE_MAX_MS) leitura[i].valido = false;
  }
  portEXIT_CRITICAL(&travaEnc);

  if (pedidoReabrir) {
    pedidoReabrir = false;
    if (linhaAberta) { rs.end(); linhaAberta = false; }
  }
  if (!configEncoder.ativo) return;
  if (!linhaAberta) { abrirLinha(); modoEscuta(); }

  const uint16_t per = (configEncoder.periodoMs < ENC_PERIODO_MIN_MS)
                     ? ENC_PERIODO_MIN_MS : configEncoder.periodoMs;
  if ((int32_t)(agora - proximaEm) < 0) return;
  proximaEm = agora + per;

  // Junta com registrador 0 nao foi configurada ainda: nao adianta
  // perguntar, e o registrador 0 costuma ser tabela de parametro.
  uint8_t tentativas = 0;
  while (tentativas < 2 && configEncoder.reg[vez] == 0) {
    vez = (uint8_t)(1 - vez);
    tentativas++;
  }
  if (configEncoder.reg[vez] == 0) return;   // junta nao configurada

  int32_t bruto = 0;
  uint8_t motivo = MOTIVO_OK;
  const bool ok = lerPosicao(vez, bruto, motivo);
  publicar(vez, ok, bruto, motivo);
  vez = (uint8_t)(1 - vez);
}

// ---------------------------------------------------------------------
static void tarefaEncoder(void* p) {
  (void)p;
  for (;;) {
    ciclo();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

void encoderIniciar() {
  pinMode(PIN_RS485_DE, OUTPUT);
  pinMode(PIN_RS485_RE, OUTPUT);
  modoEscuta();

  memset((void*)leitura, 0, sizeof(leitura));
  for (uint8_t i = 0; i < 2; i++) leitura[i].motivo = MOTIVO_NUNCA;

  if (configEncoder.ativo) {
    abrirLinha();
    modoEscuta();
    Serial.print("[ENC] Modbus em "); Serial.print(configEncoder.baud);
    Serial.print(" bps, funcao "); Serial.print(configEncoder.funcao);
    Serial.print(", juntas nos enderecos "); Serial.print(configEncoder.id[0]);
    Serial.print(" e "); Serial.println(configEncoder.id[1]);
  } else {
    Serial.println("[ENC] Leitura de encoder desligada.");
  }

#ifndef ROBOCNC_TESTE
  xTaskCreatePinnedToCore(tarefaEncoder, "encoder", 3072, nullptr, 1, nullptr, 0);
#else
  (void)tarefaEncoder;
#endif
}

#ifdef ROBOCNC_TESTE
void encoderCicloTeste() { ciclo(); }
void encoderReiniciarTeste() {
  memset((void*)leitura, 0, sizeof(leitura));
  pedidoReabrir = false;
  linhaAberta = false;
  proximaEm = 0;
  vez = 0;
}
#endif
