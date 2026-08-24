#include "encoder.h"
#include <HardwareSerial.h>
#include <driver/uart.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

// A UART2 e a mesma que o objeto rs abre; o numero precisa bater para o
// modo RS485 por hardware cair no periferico certo.
#define ENC_UART_NUM  UART_NUM_2

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

// Ultimo quadro trocado, para a tela poder mostrar o que saiu e o que
// voltou. Sem isso, "sem resposta" e uma palavra sem prova -- e foi
// exatamente ver os bytes crus que resolveu o diagnostico na bancada.
static uint8_t  ultimoEnvio[8];
static uint8_t  ultimaResposta[16];
static uint8_t  nEnvio = 0, nResposta = 0;
static uint8_t  juntaDoQuadro = 0;

// Como a posicao de 32 bits e pedida ao driver. Ver lerPosicao().
enum { LEITURA_DUPLA = 0, LEITURA_SIMPLES = 1 };
static uint8_t modoLeitura[2]    = { LEITURA_DUPLA, LEITURA_DUPLA };
static uint8_t falhasSeguidas[2] = { 0, 0 };

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
  // end() seguido de begin() no mesmo instante deixa a UART do ESP32 em
  // estado indefinido -- o driver de UART do core precisa de um tempo
  // para largar e retomar os pinos. O sketch de diagnostico que funciona
  // na bancada tem estas duas pausas; a primeira versao daqui nao tinha,
  // e a diferenca era exatamente essa.
  if (linhaAberta) { rs.end(); delay(5); }
  rs.begin(configEncoder.baud, cfgSerial(configEncoder.paridade),
           PIN_RS485_RX, PIN_RS485_TX);
  delay(5);

  if (configEncoder.deHardware) {
    // O DE vira a linha RTS do periferico, e a UART entra em RS485
    // meio-duplex: ela levanta o DE ao comecar a transmitir e o baixa no
    // fim do ultimo bit de parada, sozinha. Nenhuma tarefa, interrupcao
    // ou pausa do Wi-Fi consegue esticar essa janela.
    //
    // Neste modo o proprio periferico desliga a recepcao enquanto
    // transmite, entao nao ha eco para descartar e o RE pode ficar
    // sempre em baixo (recebendo).
    uart_set_pin(ENC_UART_NUM, PIN_RS485_TX, PIN_RS485_RX,
                 PIN_RS485_DE, UART_PIN_NO_CHANGE);
    uart_set_mode(ENC_UART_NUM, UART_MODE_RS485_HALF_DUPLEX);
    digitalWrite(PIN_RS485_RE, LOW);
  } else {
    uart_set_mode(ENC_UART_NUM, UART_MODE_UART);
    pinMode(PIN_RS485_DE, OUTPUT);
    digitalWrite(PIN_RS485_DE, LOW);
  }
  linhaAberta = true;
}

// Com o DE no hardware nao ha o que fazer aqui: quem levanta e baixa e o
// periferico, e o RE fica sempre ouvindo.
static void modoEscuta() {
  if (!configEncoder.deHardware) digitalWrite(PIN_RS485_DE, LOW);
  digitalWrite(PIN_RS485_RE, LOW);
}
static void modoTransmissao() {
  if (configEncoder.deHardware) return;
  digitalWrite(PIN_RS485_RE, HIGH);
  digitalWrite(PIN_RS485_DE, HIGH);
}

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
  if (!configEncoder.deHardware) delayMicroseconds(50);
  rs.write(saida, nSaida);
  rs.flush();
  if (!configEncoder.deHardware) {
    // flush() esvazia a fila, mas o ultimo bit ainda pode estar saindo do
    // registrador de deslocamento. Baixar DE agora corta o fim do quadro
    // e o escravo descarta calado. Esperar, por outro lado, deixa a
    // linha DIRIGIDA por nos por mais um milissegundo -- e se o driver
    // responder rapido, a resposta colide e some. E este dilema que o
    // modo por hardware acaba: ele baixa no fim do ultimo bit, e ponto.
    delayMicroseconds(usPorChar() * 2);
    modoEscuta();
  }

  size_t n = 0;
  const uint32_t inicio = millis();
  uint32_t ultimoUs = micros();

  // A subtracao com sinal aguenta a volta do contador de milissegundos;
  // "millis() < inicio + espera" trava a leitura por 49 dias a cada 49
  // dias de maquina ligada.
  while ((int32_t)(millis() - inicio) < (int32_t)ENC_TIMEOUT_MS &&
         n < maxEntrada) {
    if (rs.available()) {
      entrada[n++] = (uint8_t)rs.read();
      ultimoUs = micros();
      continue;
    }
    if (n) {
      // O quadro ja comecou: acaba em poucos milissegundos, e e o
      // silencio de 3,5 caracteres que marca o fim. Aqui vale olhar de
      // perto. Um caractere a 19200 leva 570 us: 50 us nao perde byte.
      if ((micros() - ultimoUs) > usEntreQuadros()) break;
      delayMicroseconds(50);
    } else {
      // Ainda esperando o PRIMEIRO byte, o que pode levar a espera
      // inteira. delayMicroseconds e espera OCUPADA: ficar nela queima o
      // core 0, que e o mesmo do servidor web, e o painel engasga a cada
      // driver que demora. vTaskDelay entrega o core; a UART tem fila
      // propria em hardware, entao dormir 1 ms nao perde byte nenhum.
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
  return n;
}

// ---------------------------------------------------------------------
// Uma pergunta "ler N registradores a partir de X". Devolve as palavras
// cruas, na ordem em que vieram do fio.
// ---------------------------------------------------------------------
static bool lerRegs(uint8_t i, uint16_t reg, uint16_t quantos,
                    uint16_t* palavras, uint8_t& motivo) {
  motivo = MOTIVO_OK;

  uint8_t q[8];
  q[0] = configEncoder.id[i];
  q[1] = configEncoder.funcao;
  q[2] = (uint8_t)(reg >> 8);
  q[3] = (uint8_t)(reg & 0xFF);
  q[4] = (uint8_t)(quantos >> 8);
  q[5] = (uint8_t)(quantos & 0xFF);
  const uint16_t c = crc16(q, 6);
  q[6] = (uint8_t)(c & 0xFF);
  q[7] = (uint8_t)(c >> 8);

  // Um bloco de 8 registradores volta com 3 + 16 + 2 = 21 bytes. O
  // buffer tem de caber no maior pedido que este modulo faz, senao a
  // resposta boa e cortada e vira "formato inesperado".
  uint8_t r[32];
  const size_t n = trocar(q, 8, r, sizeof(r));

  portENTER_CRITICAL(&travaEnc);
  memcpy(ultimoEnvio, q, 8);           nEnvio = 8;
  memcpy(ultimaResposta, r, n < sizeof(ultimaResposta) ? n : sizeof(ultimaResposta));
  nResposta = (uint8_t)(n < sizeof(ultimaResposta) ? n : sizeof(ultimaResposta));
  juntaDoQuadro = (uint8_t)(i + 1);
  portEXIT_CRITICAL(&travaEnc);

  if (n < 5) { motivo = MOTIVO_SILENCIO; return false; }

  const uint16_t cc = crc16(r, n - 2);
  if (r[n - 2] != (uint8_t)(cc & 0xFF) || r[n - 1] != (uint8_t)(cc >> 8)) {
    motivo = MOTIVO_CRC; return false;
  }
  if (r[0] != configEncoder.id[i]) { motivo = MOTIVO_CRC; return false; }
  if (r[1] & 0x80) {
    // O driver respondeu "nao sei responder isso". E informacao boa: ele
    // esta la, so a pergunta esta errada.
    motivo = MOTIVO_EXCECAO; return false;
  }
  if (r[1] != configEncoder.funcao || r[2] != quantos * 2) {
    motivo = MOTIVO_FORMATO; return false;
  }
  for (uint16_t k = 0; k < quantos; k++)
    palavras[k] = (uint16_t)((r[3 + k * 2] << 8) | r[4 + k * 2]);
  return true;
}

// ---------------------------------------------------------------------
// Le a posicao de uma junta. Devolve false se nao veio resposta boa.
//
// Duas palavras de 16 bits podem vir de duas maneiras:
//
//   DUPLA   -- uma pergunta so, "leia 2 registradores". E barata e, o
//              que importa mais, ATOMICA: as duas palavras saem do mesmo
//              instante do contador.
//   SIMPLES -- duas perguntas de um registrador cada. E o que o programa
//              de teste de bancada faz, e portanto a UNICA forma
//              provada neste driver.
//
// O sistema comeca na dupla e cai para a simples sozinho se o driver nao
// responder a ela. Foi exatamente essa a diferenca entre o teste que
// funcionou e o sistema que so dava falha: ninguem nunca tinha pedido
// dois registradores de uma vez a este driver.
// ---------------------------------------------------------------------
static bool lerPosicao(uint8_t i, int32_t& valor, uint8_t& motivo) {
  const uint16_t reg = configEncoder.reg[i];

  if (!configEncoder.trintaEDois) {
    uint16_t p = 0;
    if (!lerRegs(i, reg, 1, &p, motivo)) return false;
    valor = (int16_t)p;                    // com sinal: pode ser negativo
    return true;
  }

  const bool baixa = configEncoder.baixaPrimeiro;

  if (modoLeitura[i] == LEITURA_DUPLA) {
    uint16_t p[2];
    if (lerRegs(i, reg, 2, p, motivo)) {
      falhasSeguidas[i] = 0;
      valor = baixa ? (int32_t)(((uint32_t)p[1] << 16) | p[0])
                    : (int32_t)(((uint32_t)p[0] << 16) | p[1]);
      return true;
    }
    // Silencio pode ser fio. Excecao ou formato e o driver dizendo que
    // NAO faz leitura de dois registradores -- ai nao adianta insistir.
    if (motivo == MOTIVO_EXCECAO || motivo == MOTIVO_FORMATO ||
        ++falhasSeguidas[i] >= 4) {
      modoLeitura[i]   = LEITURA_SIMPLES;
      falhasSeguidas[i] = 0;
    }
    return false;
  }

  // Duas perguntas nao sao atomicas: a palavra baixa pode dar a volta
  // entre uma e outra, e o resultado seria um salto de 65536 contagens
  // que nao aconteceu. Le a ALTA duas vezes, uma de cada lado da baixa,
  // e so aceita quando ela nao mudou no meio.
  const uint16_t regBaixa = baixa ? reg : (uint16_t)(reg + 1);
  const uint16_t regAlta  = baixa ? (uint16_t)(reg + 1) : reg;

  uint16_t alta1 = 0, palavraBaixa = 0, alta2 = 0;
  const bool leu = lerRegs(i, regAlta,  1, &alta1, motivo)
                && lerRegs(i, regBaixa, 1, &palavraBaixa, motivo)
                && lerRegs(i, regAlta,  1, &alta2, motivo);
  if (!leu) {
    // Nem a forma provada respondeu: o problema nao e a pergunta, e o
    // fio ou o endereco. Volta para a dupla depois de um tempo, senao a
    // maquina fica presa na forma cara triplicando perguntas no vazio --
    // e, quando o fio voltar, quem funcionar ganha.
    if (++falhasSeguidas[i] >= 8) {
      modoLeitura[i]    = LEITURA_DUPLA;
      falhasSeguidas[i] = 0;
    }
    return false;
  }
  falhasSeguidas[i] = 0;
  if (alta1 != alta2) {
    // Virou a palavra no meio da leitura. Nao e falha do driver: a
    // proxima volta do ciclo pega o par inteiro.
    motivo = MOTIVO_VIRADA;
    return false;
  }
  valor = (int32_t)(((uint32_t)alta1 << 16) | palavraBaixa);
  return true;
}

// ---------------------------------------------------------------------
// Converte a contagem em angulo da JUNTA e compara com o comandado.
//
// O encoder conta no eixo do MOTOR; o comandado tambem esta em passos de
// motor. A reducao mecanica leva os dois para o mesmo lugar: graus da
// junta.
// =====================================================================
//  Autoteste dentro do sistema rodando. Ver encoder.h.
// =====================================================================
static volatile bool pedidoTeste  = false;
static volatile uint8_t pedidoCaca = 0;      // 1 = marcar, 2 = comparar
static const uint16_t CACA_MAX = 256;        // a faixa que o T3D usa
static volatile bool testeRodando = false;
static char relatorio[520] = "nenhum teste rodado ainda";

static void anexar(size_t& p, const char* fmt, ...) {
  if (p + 2 >= sizeof(relatorio)) return;
  va_list ap;
  va_start(ap, fmt);
  const int n = vsnprintf(relatorio + p, sizeof(relatorio) - p, fmt, ap);
  va_end(ap);
  if (n > 0) p += (size_t)n;
  if (p >= sizeof(relatorio)) p = sizeof(relatorio) - 1;
}

static void anexarHex(size_t& p, const uint8_t* b, size_t n) {
  for (size_t k = 0; k < n && p + 4 < sizeof(relatorio); k++)
    anexar(p, " %02X", b[k]);
}

// Manda um quadro OUVINDO O PROPRIO ECO: o receptor fica ligado enquanto
// transmitimos, entao o que sai pelo DI volta pelo RO. Nao depende de
// haver ninguem do outro lado -- prova a ligacao ESP32 <-> MAX485.
static size_t trocarComEco(const uint8_t* saida, size_t nSaida,
                           uint8_t* entrada, size_t maxEntrada) {
  // O eco so existe com o receptor ligado durante a transmissao, o que o
  // modo por hardware justamente impede. Aqui o controle volta a ser
  // nosso pelo tempo do teste.
  uart_set_mode(ENC_UART_NUM, UART_MODE_UART);
  pinMode(PIN_RS485_DE, OUTPUT);

  while (rs.available()) rs.read();
  digitalWrite(PIN_RS485_RE, LOW);      // ouvindo, inclusive a nos mesmos
  digitalWrite(PIN_RS485_DE, HIGH);
  delayMicroseconds(50);
  rs.write(saida, nSaida);
  rs.flush();
  delayMicroseconds(usPorChar() * 2);
  digitalWrite(PIN_RS485_DE, LOW);

  size_t n = 0;
  const uint32_t inicio = millis();
  while ((int32_t)(millis() - inicio) < 60 && n < maxEntrada) {
    if (rs.available()) { entrada[n++] = (uint8_t)rs.read(); continue; }
    if (n) break;
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return n;
}

// Uma pergunta crua, sem interpretar: so mostra o que voltou.
static void sondar(size_t& p, const char* rotulo, uint8_t id, uint8_t func,
                   uint16_t reg, uint16_t quantos) {
  uint8_t q[8];
  q[0] = id; q[1] = func;
  q[2] = (uint8_t)(reg >> 8); q[3] = (uint8_t)(reg & 0xFF);
  q[4] = (uint8_t)(quantos >> 8); q[5] = (uint8_t)(quantos & 0xFF);
  const uint16_t c = crc16(q, 6);
  q[6] = (uint8_t)(c & 0xFF); q[7] = (uint8_t)(c >> 8);

  uint8_t r[16];
  const size_t n = trocar(q, 8, r, sizeof(r));

  anexar(p, "%s ->", rotulo);
  anexarHex(p, q, 8);
  if (!n) { anexar(p, "  <- SILENCIO\n"); return; }
  anexar(p, "  <-");
  anexarHex(p, r, n);
  const uint16_t cc = crc16(r, n - 2);
  const bool crcOk = n >= 4 && r[n - 2] == (uint8_t)(cc & 0xFF) &&
                               r[n - 1] == (uint8_t)(cc >> 8);
  if (!crcOk) {
    anexar(p, "  CRC NAO BATE (velocidade ou paridade perto, mas errada)\n");
  } else if (r[1] & 0x80) {
    // Excecao e boa noticia: o driver esta ai e falou.
    anexar(p, "  EXCECAO %u -- O DRIVER RESPONDEU, so a pergunta e que nao serve\n",
           (unsigned)(n > 2 ? r[2] : 0));
  } else {
    anexar(p, "  RESPOSTA BOA\n");
  }
}

static void executarTeste() {
  testeRodando = true;
  size_t p = 0;
  relatorio[0] = '\0';

  const uint8_t id = configEncoder.id[0];
  anexar(p, "%lu bps  %s  id %u\n", (unsigned long)configEncoder.baud,
         configEncoder.paridade == 1 ? "8E1"
       : configEncoder.paridade == 2 ? "8O1" : "8N1", (unsigned)id);

  // 1. Eco. Nao precisa do driver ligado.
  {
    const uint8_t padrao[6] = {0x55, 0xAA, 0x00, 0xFF, 0x5A, 0xA5};
    uint8_t volta[16];
    const size_t n = trocarComEco(padrao, sizeof(padrao), volta, sizeof(volta));
    const bool igual = (n == sizeof(padrao)) &&
                       (memcmp(padrao, volta, n) == 0);
    anexar(p, "eco  ->");
    anexarHex(p, padrao, sizeof(padrao));
    anexar(p, "  <-");
    if (!n) anexar(p, " nada");
    else    anexarHex(p, volta, n);
    anexar(p, igual ? "  MODULO OK\n"
                    : "  ECO FALHOU -- entre o ESP32 e o MAX485\n");
  }

  // Volta a linha para o modo configurado antes de falar com o driver.
  abrirLinha();
  modoEscuta();

  // 2. Sondagem: o registrador 0 e como o programa de bancada acha o
  //    driver. Ate a excecao serve de prova de vida.
  sondar(p, "f3 r0  ", id, 3, 0, 1);
  sondar(p, "f4 r0  ", id, 4, 0, 1);

  // 3. A pergunta de verdade.
  char rot[16];
  snprintf(rot, sizeof(rot), "f%u r%u ", (unsigned)configEncoder.funcao,
           (unsigned)configEncoder.reg[0]);
  sondar(p, rot, id, configEncoder.funcao, configEncoder.reg[0],
         configEncoder.trintaEDois ? 2 : 1);

  testeRodando = false;
}

// ---------------------------------------------------------------------
// Cacada do registrador da posicao, dentro do sistema.
//
// Le a faixa toda, o operador move o braco, le de novo e mostra o que
// mudou. E o unico jeito honesto de achar isto: o mapa Modbus do T3D nao
// esta publicado, e o registrador que anda junto com o eixo e a posicao
// -- os outros nao andam.
// ---------------------------------------------------------------------
static uint16_t cacaValor[CACA_MAX];
static bool     cacaTem[CACA_MAX];
static bool     cacaMarcada = false;

static uint16_t lerFaixa(uint16_t* destino, bool* tem) {
  uint16_t lidos = 0;
  for (uint16_t a = 0; a < CACA_MAX; a += 8) {
    uint16_t bloco[8];
    uint8_t motivo = MOTIVO_OK;
    if (!lerRegs(0, a, 8, bloco, motivo)) continue;
    for (uint8_t k = 0; k < 8; k++) { destino[a + k] = bloco[k]; tem[a + k] = true; }
    lidos = (uint16_t)(lidos + 8);
  }
  return lidos;
}

static void cacarMarcar() {
  testeRodando = true;
  for (uint16_t i = 0; i < CACA_MAX; i++) cacaTem[i] = false;
  const uint16_t n = lerFaixa(cacaValor, cacaTem);
  cacaMarcada = (n > 0);

  size_t p = 0;
  relatorio[0] = '\0';
  if (!n) {
    anexar(p, "nenhum registrador respondeu na faixa 0..%u.\n"
              "Confira endereco, velocidade e funcao -- ou rode o teste da linha.",
           (unsigned)(CACA_MAX - 1));
  } else {
    anexar(p, "%u registradores anotados (funcao %u, id %u).\n\n"
              "AGORA MOVA O BRACO -- de mao mesmo, bastante -- e aperte\n"
              "\"Comparar agora\".",
           (unsigned)n, (unsigned)configEncoder.funcao,
           (unsigned)configEncoder.id[0]);
  }
  testeRodando = false;
}

static void cacarComparar() {
  testeRodando = true;
  size_t p = 0;
  relatorio[0] = '\0';

  if (!cacaMarcada) {
    anexar(p, "marque o estado inicial primeiro.");
    testeRodando = false;
    return;
  }

  static uint16_t depois[CACA_MAX];
  static bool     temDepois[CACA_MAX];
  for (uint16_t i = 0; i < CACA_MAX; i++) temDepois[i] = false;
  lerFaixa(depois, temDepois);

  uint8_t  mudaram = 0;
  uint16_t campeao = 0;
  int32_t  maiorVar = 0;

  anexar(p, "registrador   antes -> depois   variou\n");
  for (uint16_t i = 0; i < CACA_MAX; i++) {
    if (!cacaTem[i] || !temDepois[i] || cacaValor[i] == depois[i]) continue;
    const int32_t d = (int32_t)depois[i] - (int32_t)cacaValor[i];
    if (mudaram < 12)
      anexar(p, "  %u (0x%02X)   %u -> %u   %+ld\n", (unsigned)i, (unsigned)i,
             (unsigned)cacaValor[i], (unsigned)depois[i], (long)d);
    if (d > maiorVar || -d > maiorVar) {
      maiorVar = d > 0 ? d : -d;
      campeao  = i;
    }
    mudaram++;
  }

  if (!mudaram) {
    anexar(p, "\nNENHUM registrador mudou. O braco chegou a se mover? Se sim,\n"
              "a posicao pode estar fora da faixa 0..%u, ou na outra funcao\n"
              "Modbus (troque 3 por 4 e repita).", (unsigned)(CACA_MAX - 1));
  } else {
    anexar(p, "\n%u mudaram. O que variou MAIS e a palavra BAIXA da posicao;\n"
              "o vizinho de cima, que variou pouco, e a ALTA.\n\n"
              "Palpite: registrador %u, palavra baixa primeiro.\n"
              "Ponha esse numero em \"registrador\" da junta 1 e salve.",
           (unsigned)mudaram, (unsigned)campeao);
  }
  testeRodando = false;
}

void encoderPedirTeste() { pedidoTeste = true; }
void encoderPedirCacada(bool comparar) { pedidoCaca = comparar ? 2 : 1; }
bool encoderTesteRodando() { return testeRodando; }

void encoderRelatorio(char* destino, size_t tam) {
  if (!destino || tam == 0) return;
  strncpy(destino, relatorio, tam - 1);
  destino[tam - 1] = '\0';
}

// ---------------------------------------------------------------------
// O operador acompanha a maquina pelo monitor serial -- e la que ele roda
// o programa de bancada. Se a leitura falhar, o diagnostico tem de sair
// LA TAMBEM, e nao so numa aba do painel: contador de falha nao ensina
// nada, os bytes ensinam. Uma linha a cada 5 s, para nao virar enxurrada.
static void avisarNoSerial(uint8_t i, bool ok) {
  static uint32_t ultimoAviso[2] = {0, 0};
  static bool     estavaBem[2]   = {false, false};

  if (ok) {
    if (!estavaBem[i]) {
      estavaBem[i] = true;
      Serial.print("[ENC] junta "); Serial.print(i + 1);
      Serial.print(" lendo: bruto "); Serial.println((long)leitura[i].bruto);
    }
    return;
  }
  estavaBem[i] = false;
  const uint32_t agora = millis();
  if (ultimoAviso[i] && (uint32_t)(agora - ultimoAviso[i]) < 5000) return;
  ultimoAviso[i] = agora;

  char quadro[120];
  encoderUltimoQuadro(quadro, sizeof(quadro));
  Serial.print("[ENC] junta "); Serial.print(i + 1);
  Serial.print(" sem leitura -- "); Serial.println(quadro);
}

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
    // Configuracao nova, pergunta nova: tenta de novo a forma barata em
    // vez de herdar a decisao tomada com a configuracao antiga.
    modoLeitura[i]    = LEITURA_DUPLA;
    falhasSeguidas[i] = 0;
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
    if (linhaAberta) { rs.end(); delay(5); linhaAberta = false; }
  }
  if (!linhaAberta) { abrirLinha(); modoEscuta(); }

  // O teste roda AQUI, na tarefa do encoder: ele mexe no modo da UART e
  // nos pinos do transceptor, e fazer isso de outro nucleo por baixo de
  // uma leitura em andamento corromperia o quadro.
  if (pedidoTeste) {
    pedidoTeste = false;
    executarTeste();
    proximaEm = millis();
    return;
  }
  if (pedidoCaca) {
    const uint8_t o = pedidoCaca;
    pedidoCaca = 0;
    if (o == 1) cacarMarcar(); else cacarComparar();
    proximaEm = millis();
    return;
  }

  if (!configEncoder.ativo) return;

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
  avisarNoSerial(vez, ok);
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
    Serial.print(", registrador "); Serial.print(configEncoder.reg[0]);
    Serial.print(", id "); Serial.print(configEncoder.id[0]);
    Serial.print(", DE por "); Serial.println(configEncoder.deHardware
                                              ? "hardware" : "GPIO");
    if (configEncoder.reg[1] == 0)
      Serial.println("[ENC] Junta 2 nao ligada (registrador 0).");
  } else {
    Serial.println("[ENC] Leitura de encoder desligada.");
  }

#ifndef ROBOCNC_TESTE
  // Prioridade 2, ACIMA da tarefa web (1). O motivo e concreto: entre
  // rs.flush() e baixar o DE ha uma janela de menos de um milissegundo
  // em que a linha ainda esta sendo dirigida por nos. Com a mesma
  // prioridade da tarefa web, o escalonador troca de tarefa no tique de
  // 1 ms bem no meio dessa janela; o driver responde, nos ainda estamos
  // com o DE alto, e o quadro morre na colisao. Toda vez, no mesmo
  // ponto -- que e o que faz parecer que "nunca le nada".
  xTaskCreatePinnedToCore(tarefaEncoder, "encoder", 3072, nullptr, 2, nullptr, 0);
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
  for (uint8_t i = 0; i < 2; i++) {
    modoLeitura[i]    = LEITURA_DUPLA;
    falhasSeguidas[i] = 0;
  }
  nEnvio = nResposta = juntaDoQuadro = 0;
  // No ESP32 o boot zera isto sozinho; aqui as globais sobrevivem ao
  // setup(), e uma cacada marcada num cenario valeria no seguinte.
  pedidoTeste  = false;
  pedidoCaca   = 0;
  testeRodando = false;
  cacaMarcada  = false;
  strcpy(relatorio, "nenhum teste rodado ainda");
}
#endif

// ---------------------------------------------------------------------
// O ultimo quadro que passou no fio, em hexadecimal. E o que transforma
// "nao le nada" num diagnostico.
// ---------------------------------------------------------------------
void encoderUltimoQuadro(char* destino, size_t tam) {
  if (!destino || tam == 0) return;
  destino[0] = '\0';

  uint8_t env[8], resp[16], nE, nR, jq;
  portENTER_CRITICAL(&travaEnc);
  memcpy(env, ultimoEnvio, sizeof(env));
  memcpy(resp, ultimaResposta, sizeof(resp));
  nE = nEnvio; nR = nResposta; jq = juntaDoQuadro;
  portEXIT_CRITICAL(&travaEnc);

  if (!nE) { snprintf(destino, tam, "nenhuma pergunta enviada ainda"); return; }

  const uint8_t iq = (jq == 2) ? 1 : 0;
  size_t p = 0;
  p += (size_t)snprintf(destino + p, tam - p, "junta %u  %s  ->", (unsigned)jq,
                        modoLeitura[iq] == LEITURA_DUPLA
                          ? "2 registradores" : "1 de cada vez");
  for (uint8_t k = 0; k < nE && p + 4 < tam; k++)
    p += (size_t)snprintf(destino + p, tam - p, " %02X", env[k]);
  if (p + 6 < tam) p += (size_t)snprintf(destino + p, tam - p, "   <-");
  if (!nR) { snprintf(destino + p, tam - p, " (silencio)"); return; }
  for (uint8_t k = 0; k < nR && p + 4 < tam; k++)
    p += (size_t)snprintf(destino + p, tam - p, " %02X", resp[k]);
}
