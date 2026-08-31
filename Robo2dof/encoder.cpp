#include "encoder.h"
#include <HardwareSerial.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <math.h>   // fabsf/lroundf dos derivados da leitura

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


// ---------------------------------------------------------------------
static uint32_t cfgSerial(uint8_t paridade) {
  switch (paridade) {
    case 1:  return SERIAL_8E1;
    case 2:  return SERIAL_8O1;
    default: return SERIAL_8N1;
  }
}

// DE alto = nos dirigimos a linha. RE baixo = escutamos. Os niveis sao
// os do monitor do operador, sem invencao.
static void modoEscuta() {
  digitalWrite(PIN_RS485_DE, LOW);
  digitalWrite(PIN_RS485_RE, LOW);
}
static void modoTransmissao() {
  digitalWrite(PIN_RS485_DE, HIGH);
  digitalWrite(PIN_RS485_RE, HIGH);
}

static void abrirLinha() {
  // IGUAL ao monitor que funciona na maquina do operador:
  //
  //     rs.end(); delay(10); rs.begin(...); delay(10);
  //
  // end() e chamado SEMPRE, inclusive na primeira vez -- e o que o
  // codigo dele faz, e a UART2 pode chegar aqui com resto de
  // configuracao. As duas pausas de 10 ms nao sao superstiçao: o driver
  // de UART do core precisa de tempo para largar e retomar os pinos.
  rs.end();
  delay(10);
  rs.begin(configEncoder.baud, cfgSerial(configEncoder.paridade),
           PIN_RS485_RX, PIN_RS485_TX);
  delay(10);

  // Os dois pinos do transceptor sao NOSSOS, por GPIO. Tinha aqui um
  // modo em que o periferico da UART dirigia o DE sozinho (RS485
  // meio-duplex por hardware). Em teoria e melhor -- baixa o DE no fim
  // exato do ultimo bit. Na pratica, na maquina do operador, o DE nunca
  // subia: o quadro nao chegava a sair no barramento e o driver nunca
  // tinha o que responder. Silencio absoluto, para sempre.
  //
  // O codigo dele controla os dois pinos a mao e funciona. E assim que
  // fica.
  pinMode(PIN_RS485_DE, OUTPUT);
  pinMode(PIN_RS485_RE, OUTPUT);
  modoEscuta();

  linhaAberta = true;
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

// ---------------------------------------------------------------------
// Um ciclo de pergunta e resposta, na mesma sequencia do monitor que
// funciona na maquina do operador:
//
//     limpa a entrada -> DE alto -> 50 us -> escreve -> flush ->
//     1000 us -> DE baixo -> espera a resposta
//
// 'esperados' e quantos bytes a resposta boa tem. O monitor dele nao
// sabe disso e espera os 100 ms inteiros toda vez; aqui a conta e
// conhecida (3 + 2*N + 2), entao a leitura termina assim que o quadro
// fecha. Mais rapido, e sem depender de medir silencio no fio.
// ---------------------------------------------------------------------
static size_t trocar(const uint8_t* saida, size_t nSaida,
                     uint8_t* entrada, size_t maxEntrada,
                     size_t esperados = 0,
                     uint32_t prazoMs = ENC_TIMEOUT_MS) {
  while (rs.available()) rs.read();

  modoTransmissao();
  delayMicroseconds(50);
  rs.write(saida, nSaida);
  rs.flush();
  // flush() espera a fila esvaziar, mas o ultimo bit ainda pode estar
  // saindo do registrador de deslocamento. Baixar o DE agora corta o fim
  // do quadro e o escravo descarta calado. O monitor do operador espera
  // 1000 us fixos aqui, e a essa velocidade e o que da certo.
  delayMicroseconds(1000);
  modoEscuta();

  size_t n = 0;
  const uint32_t inicio = millis();

  // Subtracao com sinal: aguenta a volta do contador de milissegundos.
  while ((int32_t)(millis() - inicio) < (int32_t)prazoMs &&
         n < maxEntrada) {
    if (rs.available()) {
      entrada[n++] = (uint8_t)rs.read();
      if (esperados && n >= esperados) break;   // quadro completo
      continue;
    }
    // Nada na fila. O monitor dele gira aqui em espera ocupada, porque
    // esta sozinho na placa. Aqui nao: este nucleo e o mesmo do servidor
    // web, e queimar 100 ms nele engasga o painel a cada driver que
    // demora. vTaskDelay entrega o nucleo; a UART tem fila propria em
    // hardware, entao dormir 1 ms nao perde byte nenhum.
    vTaskDelay(pdMS_TO_TICKS(1));
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
  // Resposta boa: id + funcao + contagem + os dados + CRC.
  const size_t n = trocar(q, 8, r, sizeof(r), 3 + (size_t)quantos * 2 + 2);

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
// UMA pergunta, dois registradores -- exatamente como o monitor do
// operador faz. Havia aqui um recuo automatico para "um registrador por
// vez", inventado quando eu achava que o driver podia nao aceitar a
// pergunta dupla. O log dele desmentiu isso: os modos 4 e 6 do programa
// de bancada leem OITO registradores por pergunta e funcionam. O recuo
// so acrescentava um jeito a mais de dar errado, e saiu.
//
// A leitura de dois registradores de uma vez tambem e ATOMICA: as duas
// palavras saem do mesmo instante do contador, e nao ha o problema da
// palavra baixa dar a volta no meio.
// ---------------------------------------------------------------------
static bool lerPosicao(uint8_t i, int32_t& valor, uint8_t& motivo) {
  const uint16_t reg = configEncoder.reg[i];

  if (!configEncoder.trintaEDois) {
    uint16_t p = 0;
    if (!lerRegs(i, reg, 1, &p, motivo)) return false;
    valor = (int16_t)p;                    // com sinal: pode ser negativo
    return true;
  }

  uint16_t p[2];
  if (!lerRegs(i, reg, 2, p, motivo)) return false;
  // Muito driver Modbus manda a palavra BAIXA primeiro. Errar isto faz a
  // posicao dar saltos de dezenas de milhares em vez de crescer suave.
  valor = configEncoder.baixaPrimeiro
        ? (int32_t)(((uint32_t)p[1] << 16) | p[0])
        : (int32_t)(((uint32_t)p[0] << 16) | p[1]);
  return true;
}

// =====================================================================
//  ESCRITA -- o habilita (SON) dos drivers
//
//  Este modulo lia e so lia. O habilita deixou de ser um pino e passou a
//  morar aqui, entao a escrita entrou -- mas por uma porta estreita: um
//  registrador so, o configurado em configSon, e nada mais. Nenhuma
//  outra funcao deste arquivo escreve.
//
//  Toda escrita e CONFERIDA RELENDO. Driver que responde "aceitei" e
//  guarda outra coisa existe: registrador so de leitura, escrita
//  bloqueada por nivel de acesso, parametro que so vale com o servo
//  parado. Sem reler, o firmware diria "desabilitado" com o eixo
//  energizado -- que e exatamente a mentira que nao se pode contar aqui.
// =====================================================================
static volatile uint8_t  pedidoSon   = 0;   // 0 nada, 1 ligar, 2 desligar
static volatile uint8_t  estadoSon   = SON_OCIOSO;
static volatile uint32_t sonPedidoMs = 0;
static char sonMotivo[96] = "";

// Declarada aqui porque escreverUm() a usa para conferir, e ela vem
// logo abaixo -- a confirmacao e parte da escrita, nao um passo a parte.
static bool lerUmaPalavra(uint8_t id, uint16_t reg, uint16_t& valor);

// Um quadro de escrita, ja conferido relendo. 'id' e explicito porque
// aqui nao se escreve "para a junta i" e sim para um endereco do
// barramento -- os dois drivers podem compartilhar o mesmo.
static bool escreverUm(uint8_t id, uint16_t reg, uint16_t valor, bool usar16) {
  uint8_t q[11];
  size_t nq = 0;
  q[0] = id;
  if (usar16) {
    // Funcao 16: ha driver que recusa a 06 mesmo para um registrador so.
    q[1] = 16;
    q[2] = (uint8_t)(reg >> 8);   q[3] = (uint8_t)(reg & 0xFF);
    q[4] = 0;                     q[5] = 1;      // um registrador
    q[6] = 2;                                    // dois bytes
    q[7] = (uint8_t)(valor >> 8); q[8] = (uint8_t)(valor & 0xFF);
    nq = 9;
  } else {
    q[1] = 6;
    q[2] = (uint8_t)(reg >> 8);   q[3] = (uint8_t)(reg & 0xFF);
    q[4] = (uint8_t)(valor >> 8); q[5] = (uint8_t)(valor & 0xFF);
    nq = 6;
  }
  const uint16_t c = crc16(q, nq);
  q[nq]     = (uint8_t)(c & 0xFF);
  q[nq + 1] = (uint8_t)(c >> 8);
  nq += 2;

  // As duas funcoes respondem com 8 bytes: eco do cabecalho e CRC.
  uint8_t r[16];
  const size_t n = trocar(q, nq, r, sizeof(r), 8, SON_TIMEOUT_MS);

  portENTER_CRITICAL(&travaEnc);
  memcpy(ultimoEnvio, q, nq < sizeof(ultimoEnvio) ? nq : sizeof(ultimoEnvio));
  nEnvio = (uint8_t)(nq < sizeof(ultimoEnvio) ? nq : sizeof(ultimoEnvio));
  memcpy(ultimaResposta, r, n < sizeof(ultimaResposta) ? n : sizeof(ultimaResposta));
  nResposta = (uint8_t)(n < sizeof(ultimaResposta) ? n : sizeof(ultimaResposta));
  portEXIT_CRITICAL(&travaEnc);

  if (n < 5) return false;
  const uint16_t cc = crc16(r, n - 2);
  if (r[n - 2] != (uint8_t)(cc & 0xFF) || r[n - 1] != (uint8_t)(cc >> 8)) return false;
  if (r[0] != id) return false;
  if (r[1] & 0x80) return false;          // o driver recusou a escrita

  // A prova nao e o "aceitei": e o valor de volta.
  uint16_t lido = 0;
  if (!lerUmaPalavra(id, reg, lido)) return false;
  return lido == valor;
}

// Le um registrador pela funcao 3 (holding), que e onde parametro mora.
// A funcao 4 e so de leitura e nunca guardaria o que acabamos de
// escrever, entao a confirmacao tem de vir pela 3 mesmo que o encoder
// esteja configurado na 4.
static bool lerUmaPalavra(uint8_t id, uint16_t reg, uint16_t& valor) {
  uint8_t q[8];
  q[0] = id;  q[1] = 3;
  q[2] = (uint8_t)(reg >> 8);  q[3] = (uint8_t)(reg & 0xFF);
  q[4] = 0;                    q[5] = 1;
  const uint16_t c = crc16(q, 6);
  q[6] = (uint8_t)(c & 0xFF);  q[7] = (uint8_t)(c >> 8);

  uint8_t r[16];
  const size_t n = trocar(q, 8, r, sizeof(r), 7, SON_TIMEOUT_MS);
  if (n < 7) return false;
  const uint16_t cc = crc16(r, n - 2);
  if (r[n - 2] != (uint8_t)(cc & 0xFF) || r[n - 1] != (uint8_t)(cc >> 8)) return false;
  if (r[0] != id || r[1] != 3 || r[2] != 2) return false;
  valor = (uint16_t)((r[3] << 8) | r[4]);
  return true;
}

// Escreve o habilita nos DOIS drivers. Os dois tem de confirmar: meio
// braco energizado e pior que nenhum, porque parece que obedeceu.
//
// UMA TENTATIVA POR CICLO, e a razao vale escrever.
//
// A versao anterior fazia tudo de uma vez: dois drivers, tres tentativas
// cada, escrita mais releitura -- ate doze transacoes dentro de um unico
// ciclo(). Com o barramento mudo, cada uma gastava o prazo inteiro, e o
// banco mediu 822 ms presos num ciclo so. A tarefa do encoder roda no
// core 0 com prioridade 2 e a tarefa de rede no MESMO core com
// prioridade 1: nesse tempo o painel engasgava, o jog cortava
// (TIMEOUT_JOG_MS = 350) e a leitura vencia de idade.
//
// Um botao de habilitar servos nao pode derrubar o movimento de quem
// esta comandando. Espalhando as tentativas, cada ciclo custa no maximo
// duas transacoes de SON_TIMEOUT_MS -- e entre elas a tarefa de rede
// roda e o operador continua com painel na mao. Ver o cenario V06.
static uint8_t  sonDriver    = 0;   // qual junta a proxima tentativa ataca
static uint8_t  sonTentativa = 0;
static bool     sonLigar     = false;
static uint8_t  sonJuntaAlvo = 0;   // 1, 2, ou 0 = as duas
static bool     sonJuntaOk[2] = {false, false};

static void sonComecar(bool ligar, uint8_t junta) {
  sonLigar     = ligar;
  sonJuntaAlvo = junta;
  sonDriver    = (junta == 2) ? 1 : 0;
  sonTentativa = 0;
  sonJuntaOk[0] = sonJuntaOk[1] = false;
  estadoSon    = SON_PENDENTE;
  sonMotivo[0] = '\0';
}

// Um passo. Devolve true quando ainda ha trabalho para o proximo ciclo.
// Ultimo indice de junta que este pedido tem de atender. Pedido de uma
// junta so comeca e termina nela; pedido das duas anda de 0 a 1 -- a
// menos que os dois enderecos sejam iguais, que e um driver so na
// bancada e nao dois.
static uint8_t sonUltimoIndice() {
  if (sonJuntaAlvo == 1) return 0;
  if (sonJuntaAlvo == 2) return 1;
  return (configEncoder.id[1] == configEncoder.id[0]) ? 0 : 1;
}

static bool sonPasso() {
  const uint16_t valor = sonLigar ? configSon.valLiga : configSon.valDesliga;
  const uint8_t  id    = configEncoder.id[sonDriver];
  const uint8_t  fim   = sonUltimoIndice();

  if (escreverUm(id, configSon.reg, valor, configSon.funcao16)) {
    sonJuntaOk[sonDriver] = true;
    // Enderecos iguais: um driver so responde pelas duas juntas.
    if (configEncoder.id[1] == configEncoder.id[0]) sonJuntaOk[1] = true;
    sonTentativa = 0;
    if (sonDriver >= fim) {
      estadoSon    = SON_OK;
      sonMotivo[0] = '\0';
      return false;
    }
    sonDriver++;
    return true;
  }

  if (++sonTentativa < SON_TENTATIVAS) return true;   // insiste no proximo ciclo

  estadoSon = SON_FALHOU;
  snprintf(sonMotivo, sizeof(sonMotivo),
           "a junta %u (driver %u) nao confirmou %s no registrador %u",
           (unsigned)(sonDriver + 1), (unsigned)id,
           sonLigar ? "habilitar" : "DESABILITAR", (unsigned)configSon.reg);
  return false;
}

uint8_t encoderSonJunta() { return sonJuntaAlvo; }
bool encoderSonJuntaOk(uint8_t junta) {
  if (junta != 1 && junta != 2) return false;
  return sonJuntaOk[junta - 1];
}

// Chamado pelo CORE 1. So deixa recado: quem fala no barramento e a
// tarefa do core 0, e escrever daqui por baixo de uma leitura em
// andamento corromperia os dois quadros.
void encoderPedirSon(bool ligar, uint8_t junta) {
  sonMotivo[0] = '\0';
  estadoSon    = SON_PENDENTE;
  sonPedidoMs  = millis();
  sonJuntaAlvo = junta;
  pedidoSon    = ligar ? 1 : 2;
}

// Pede e espera. Quem executa continua sendo a tarefa do core 0 -- este
// laco so cede o processador ate ela terminar. Escrever daqui por baixo
// de uma leitura em andamento corromperia os dois quadros.
bool encoderSonEsperar(uint32_t prazoMs) {
  const uint32_t inicio = millis();
  while ((int32_t)(millis() - inicio) < (int32_t)prazoMs) {
    const uint8_t e = encoderSonEstado();
    if (e == SON_OK)     return true;
    if (e == SON_FALHOU) return false;
    delay(2);
  }
  return encoderSonEstado() == SON_OK;
}

uint8_t encoderSonEstado() {
  // Pedido que nao foi atendido no prazo e falha, nao espera eterna. A
  // tarefa do core 0 pode ter morrido, e ficar "pendente" para sempre
  // seria a tela dizendo "calma" com o eixo energizado.
  if (estadoSon == SON_PENDENTE &&
      (int32_t)(millis() - sonPedidoMs) > (int32_t)SON_PRAZO_MS) {
    estadoSon = SON_FALHOU;
    if (!sonMotivo[0])
      snprintf(sonMotivo, sizeof(sonMotivo),
               "o barramento nao respondeu em %u ms", (unsigned)SON_PRAZO_MS);
  }
  return estadoSon;
}

void encoderSonMotivo(char* destino, size_t tam) {
  if (!destino || !tam) return;
  snprintf(destino, tam, "%s", sonMotivo);
}

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
  while (rs.available()) rs.read();
  digitalWrite(PIN_RS485_RE, LOW);      // ouvindo, inclusive a nos mesmos
  digitalWrite(PIN_RS485_DE, HIGH);
  delayMicroseconds(50);
  rs.write(saida, nSaida);
  rs.flush();
  delayMicroseconds(1000);              // o mesmo tempo da leitura normal
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
// Instante e valor da leitura anterior boa: e deles que sai a
// velocidade. Ficam em escopo de ARQUIVO, nao dentro da funcao, porque
// encoderReiniciarTeste() precisa zera-los: no ESP32 o boot zera tudo,
// mas no banco as estaticas sobrevivem ao setup() e o primeiro delta de
// um cenario sairia medido contra o cenario anterior. Foi assim que
// "passos acumulados" apareceu com 129 milhoes num teste de 32 mil.
static uint32_t encUltimoMs[2]   = {0, 0};
static bool     encTinhaAntes[2] = {false, false};
static int32_t  encBrutoAntes[2] = {0, 0};

static uint16_t cacaValor[CACA_MAX];   // antes de qualquer giro
static uint16_t cacaMeio[CACA_MAX];    // depois do primeiro giro
static bool     cacaTem[CACA_MAX];
static uint8_t  cacaEtapa = 0;         // 0 = nada, 1 = marcado, 2 = meio lido

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
  cacaEtapa = n ? 1 : 0;

  size_t p = 0;
  relatorio[0] = '\0';
  if (!n) {
    anexar(p, "nenhum registrador respondeu na faixa 0..%u.\n"
              "Confira endereco, velocidade e funcao -- ou rode o teste da linha.",
           (unsigned)(CACA_MAX - 1));
  } else {
    anexar(p, "%u registradores anotados (funcao %u, id %u).\n\n"
              "AGORA MOVA O BRACO, num SO sentido, e aperte\n"
              "\"Comparar agora\".",
           (unsigned)n, (unsigned)configEncoder.funcao,
           (unsigned)configEncoder.id[0]);
  }
  testeRodando = false;
}

// Segunda leitura. Ela sozinha nao conclui nada -- serve para o crivo do
// sentido, que e o que separa posicao de ruido.
static void cacarMeio() {
  testeRodando = true;
  bool tem[CACA_MAX];
  for (uint16_t i = 0; i < CACA_MAX; i++) tem[i] = false;
  lerFaixa(cacaMeio, tem);
  for (uint16_t i = 0; i < CACA_MAX; i++) if (!tem[i]) cacaTem[i] = false;
  cacaEtapa = 2;

  size_t p = 0;
  relatorio[0] = '\0';
  anexar(p, "Primeiro giro anotado.\n\n"
            "AGORA GIRE MAIS, no MESMO sentido, e aperte\n"
            "\"Comparar agora\" de novo.\n\n"
            "E este segundo giro que separa a posicao do ruido: erro de\n"
            "seguimento e velocidade oscilam, a posicao nao volta.");
  testeRodando = false;
}

static void cacarComparar() {
  testeRodando = true;
  size_t p = 0;
  relatorio[0] = '\0';

  if (cacaEtapa == 0) {
    anexar(p, "marque o estado inicial primeiro.");
    testeRodando = false;
    return;
  }
  if (cacaEtapa == 1) { testeRodando = false; cacarMeio(); return; }

  static uint16_t depois[CACA_MAX];
  static bool     temDepois[CACA_MAX];
  for (uint16_t i = 0; i < CACA_MAX; i++) temDepois[i] = false;
  lerFaixa(depois, temDepois);

  uint8_t mudaram = 0;
  anexar(p, "registrador   antes -> depois   variou\n");
  for (uint16_t i = 0; i < CACA_MAX; i++) {
    if (!cacaTem[i] || !temDepois[i] || cacaValor[i] == depois[i]) continue;
    if (mudaram < 10)
      anexar(p, "  %u (0x%02X)   %u -> %u   %+ld\n", (unsigned)i, (unsigned)i,
             (unsigned)cacaValor[i], (unsigned)depois[i],
             (long)((int32_t)depois[i] - (int32_t)cacaValor[i]));
    mudaram++;
  }

  if (!mudaram) {
    anexar(p, "\nNENHUM registrador mudou. O braco chegou a se mover? Se sim,\n"
              "a posicao pode estar fora da faixa 0..%u, ou na outra funcao\n"
              "Modbus (troque 3 por 4 e repita).", (unsigned)(CACA_MAX - 1));
    testeRodando = false;
    return;
  }

  // Qual deles e a POSICAO? Listar o que mudou nao basta: o driver mexe
  // em varias coisas quando o eixo gira -- erro de seguimento,
  // velocidade. Na maquina do operador mudaram cinco de uma vez, e dois
  // deles andavam juntos, o que confundia.
  //
  // O par da posicao tem uma assinatura que nenhum outro tem: montado
  // como 32 bits com a palavra BAIXA primeiro, ele anda uma quantidade
  // que cabe numa girada de mao; montado ao contrario, salta centenas de
  // milhoes. Procura-se o par r/r+1 em que a montagem certa e MUITO mais
  // mansa que a errada.
  // O CRIVO: o operador girou sempre para o MESMO LADO, entao a posicao
  // andou sempre para o mesmo lado -- nos dois giros.
  //
  // Nenhum criterio de "quem variou mais" faz isso, e foi assim que a
  // versao anterior errou na maquina do operador: o registrador 94 foi de
  // 0 para 65535, o maior salto da lista, mas com sinal isso e -1, e o
  // vizinho 95 pulava para os dois lados. Era erro de seguimento.
  // Erro de seguimento OSCILA; posicao nao volta.
  uint16_t melhor = 0;
  uint32_t melhorD = 0;
  bool     achou = false;
  for (uint16_t i = 0; i + 1 < CACA_MAX; i++) {
    if (!cacaTem[i] || !cacaTem[i + 1] || !temDepois[i] || !temDepois[i + 1]) continue;

    const uint32_t v0 = ((uint32_t)cacaValor[i + 1] << 16) | cacaValor[i];
    const uint32_t v1 = ((uint32_t)cacaMeio [i + 1] << 16) | cacaMeio [i];
    const uint32_t v2 = ((uint32_t)depois   [i + 1] << 16) | depois   [i];

    // Complemento de dois: a volta da palavra baixa sai certa sozinha.
    const int32_t d1 = (int32_t)(v1 - v0);
    const int32_t d2 = (int32_t)(v2 - v1);
    if (d1 == 0 || d2 == 0) continue;              // parou: nao e posicao
    if ((d1 > 0) != (d2 > 0)) continue;            // voltou: e ruido

    // E a montagem invertida tem de ser muito pior, senao nao sao duas
    // metades de um numero so, e sim dois vizinhos quaisquer.
    const uint32_t t0 = ((uint32_t)cacaValor[i] << 16) | cacaValor[i + 1];
    const uint32_t t2 = ((uint32_t)depois[i]    << 16) | depois[i + 1];
    const int64_t  dCerto = (int64_t)d1 + d2;
    const int64_t  dTorto = (int64_t)(int32_t)(t2 - t0);
    const int64_t  mCerto = dCerto < 0 ? -dCerto : dCerto;
    const int64_t  mTorto = dTorto < 0 ? -dTorto : dTorto;
    if (mCerto * 8 > mTorto) continue;

    if (!achou || (uint32_t)mCerto > melhorD) {
      achou = true; melhor = i; melhorD = (uint32_t)mCerto;
    }
  }

  if (achou) {
    anexar(p, "\n%u mudaram.\n\n"
              "=== O PAR E %u (baixa) e %u (alta) ===\n"
              "Andou %lu contagens, SEMPRE PARA O MESMO LADO nos dois giros.\n"
              "E isso que separa posicao de ruido.\n\n"
              "Ponha %u em \"registrador\" da junta 1 e salve.",
           (unsigned)mudaram, (unsigned)melhor, (unsigned)(melhor + 1),
           (unsigned long)melhorD, (unsigned)melhor);
  } else {
    anexar(p, "\n%u mudaram, mas nenhum PAR andou para o MESMO LADO nos\n"
              "dois giros. Essa e a assinatura de erro de seguimento e\n"
              "velocidade: oscilam e voltam para perto de zero.\n"
              "Repita girando SEMPRE para o mesmo lado, e bastante.",
           (unsigned)mudaram);
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

// Salto a confirmar: a leitura que pulou longe demais fica de lado ate a
// proxima dizer se era o eixo ou defeito. Ver publicar().
static bool    encSaltoPendente[2] = {false, false};
static int32_t encSaltoBruto[2]    = {0, 0};

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

  const uint32_t agora = millis();

  portENTER_CRITICAL(&travaEnc);

  // SALTO IMPOSSIVEL: numero possivel nao e o mesmo que numero do eixo.
  //
  // Quadro corrompido que passou no CRC, palavra baixa de um instante
  // casada com a alta de outro, contador dando a volta -- todos chegam
  // aqui como um inteiro perfeitamente plausivel. O que os denuncia e a
  // DISTANCIA ate a leitura anterior: a posicao pula meia volta ou uma
  // volta inteira do motor de uma amostra para a outra, e nenhum eixo
  // desta maquina faz isso.
  //
  // Obedecer teleporta a posicao oficial da maquina: o desenho salta, o
  // ancoramento reescreve a contagem com o numero errado e o braco
  // arranca para o lugar errado. Era o "braco pulando angulo". Recusar
  // custa uma amostra; obedecer custa a peca.
  //
  // E O QUE SEPARA UM GLITCH DE UM MOVIMENTO GRANDE E DE VERDADE:
  // o glitch NAO SE REPETE. Um quadro corrompido vem uma vez e o
  // seguinte volta para perto de onde o eixo estava; um eixo que
  // realmente foi para longe continua la na amostra seguinte.
  //
  // Entao um salto nao e recusado de vez: ele fica PENDENTE. A amostra
  // seguinte decide -- se confirmar a posicao nova, ela e aceita e o
  // eixo perdeu uma amostra; se voltar, era defeito e nada foi obedecido.
  if (ok && encTinhaAntes[i] && cv > 0.5f) {
    const uint32_t dtSalto = agora - encUltimoMs[i];
    const int32_t  d       = (int32_t)((uint32_t)bruto - (uint32_t)encBrutoAntes[i]);
    float voltas = ENC_SALTO_VOLTAS_POR_S * (float)dtSalto / 1000.0f;
    if (voltas < ENC_SALTO_VOLTAS_MIN) voltas = ENC_SALTO_VOLTAS_MIN;
    const float limite = voltas * cv;
    if (fabsf((float)d) > limite) {
      const int32_t dCand =
          (int32_t)((uint32_t)bruto - (uint32_t)encSaltoBruto[i]);
      if (encSaltoPendente[i] && fabsf((float)dCand) <= limite) {
        // A amostra anterior tinha apontado para ca, e esta confirma: o
        // eixo esta mesmo la. Aceita e volta ao normal.
        encSaltoPendente[i] = false;
      } else {
        encSaltoPendente[i] = true;
        encSaltoBruto[i]    = bruto;
        ok = false;
        motivo = MOTIVO_SALTO;
        leitura[i].saltos++;
      }
    } else {
      encSaltoPendente[i] = false;
    }
  }
  leitura[i].motivo = motivo;
  if (ok) {
    leitura[i].bruto    = bruto;
    leitura[i].idadeMs  = 0;
    leitura[i].leituras++;
    // Escala ensinada ganha do par contagens-por-volta + reducao: ela foi
    // MEDIDA nesta maquina, e os outros dois foram digitados.
    const float cpg = configEncoder.contagensPorGrau[i];
    if (cpg > 0.0001f || cpg < -0.0001f) {
      leitura[i].graus  = (float)(bruto - leitura[i].referencia) / cpg + j.grausHome;
      leitura[i].erro   = comandado - leitura[i].graus;
      leitura[i].valido = true;
    } else if (cv > 0.5f) {
      const float voltasMotor = (float)(bruto - leitura[i].referencia) / cv;
      leitura[i].graus  = voltasMotor * 360.0f / red + j.grausHome;
      leitura[i].erro   = comandado - leitura[i].graus;
      leitura[i].valido = true;
    } else {
      leitura[i].valido = false;
    }

    // ---- derivados ------------------------------------------------
    if (encTinhaAntes[i]) {
      // Subtracao em complemento de dois: a volta do contador de 32 bits
      // sai certa sozinha, sem caso especial.
      const int32_t d  = (int32_t)((uint32_t)bruto - (uint32_t)encBrutoAntes[i]);
      const uint32_t dt = agora - encUltimoMs[i];
      leitura[i].delta = d;

      if (dt > 0 && dt < 2000) {
        // Em float: (d * 1000) em inteiro estoura com meio milhao de
        // contagens, que um eixo rapido faz em um segundo.
        leitura[i].velocidade = (float)d * 1000.0f / (float)dt;
        leitura[i].rpm = (cv > 0.5f)
                       ? leitura[i].velocidade * 60.0f / cv : 0.0f;
      }

      const int32_t mod = d < 0 ? -d : d;
      if (mod > ENC_PARADO_CONTAGENS) {
        const int8_t s = (d > 0) ? 1 : -1;
        // So conta inversao entre dois movimentos de verdade: parar e
        // voltar nao e inversao, e tremor tambem nao.
        if (leitura[i].sentido != 0 && leitura[i].sentido != s)
          leitura[i].inversoes++;
        leitura[i].sentido = s;
        leitura[i].passosTotais += (uint32_t)mod;
      } else {
        leitura[i].sentido = 0;
      }

      if (leitura[i].velocidade > leitura[i].velMax)
        leitura[i].velMax = leitura[i].velocidade;
      if (leitura[i].velocidade < leitura[i].velMin)
        leitura[i].velMin = leitura[i].velocidade;
    }
    if (bruto < leitura[i].brutoMin || !encTinhaAntes[i]) leitura[i].brutoMin = bruto;
    if (bruto > leitura[i].brutoMax || !encTinhaAntes[i]) leitura[i].brutoMax = bruto;

    encTinhaAntes[i] = true;
    encBrutoAntes[i] = bruto;
    encUltimoMs[i]   = agora;
  } else {
    leitura[i].falhas++;
    // Sem leitura nao ha velocidade: manter a ultima faria a tela dizer
    // que o eixo continua girando depois que o fio caiu.
    leitura[i].velocidade = 0.0f;
    leitura[i].rpm        = 0.0f;
    leitura[i].sentido    = 0;
    leitura[i].delta      = 0;
    // Silencio de verdade recomeca a contagem de tempo: sem resposta nao
    // ha do que a proxima amostra ser medida.
    //
    // Salto recusado e outra coisa: ali HOUVE resposta, e a leitura
    // anterior continua valendo como ponto de comparacao. Zerar aqui
    // faria a amostra seguinte passar sem conferencia -- e um defeito que
    // se repete passaria em toda segunda amostra.
    if (motivo != MOTIVO_SALTO) encTinhaAntes[i] = false;
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

int32_t encoderReferencia(uint8_t junta) {
  const uint8_t i = (junta == 2) ? 1 : 0;
  portENTER_CRITICAL(&travaEnc);
  const int32_t r = leitura[i].referencia;
  portEXIT_CRITICAL(&travaEnc);
  return r;
}

// Chamada no boot, com o valor que estava no NVS. Sem isto a referencia
// nasceria zerada e o angulo lido sairia absurdo no primeiro ciclo.
void encoderCarregarReferencia(uint8_t junta, int32_t bruto) {
  const uint8_t i = (junta == 2) ? 1 : 0;
  portENTER_CRITICAL(&travaEnc);
  leitura[i].referencia = bruto;
  portEXIT_CRITICAL(&travaEnc);
}

bool encoderDefinirZero(uint8_t junta, float graus) {
  if (junta != 1 && junta != 2) return false;
  const uint8_t i = junta - 1;
  const Junta& j = (junta == 1) ? J1 : J2;

  const LeituraEncoder L = encoderLer(junta);
  if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) return false;

  // Tem de usar a MESMA conversao de publicar(), senao o zero ensinado
  // sai deslocado da leitura que a tela mostra -- os dois lados da mesma
  // conta feitos por caminhos diferentes.
  const float cpg = configEncoder.contagensPorGrau[i];
  int32_t ref;
  if (cpg > 0.0001f || cpg < -0.0001f) {
    ref = L.bruto - (int32_t)lroundf((graus - j.grausHome) * cpg);
  } else {
    const float cv  = configEncoder.contagensPorVolta[i];
    const float red = (j.reducao > 0.001f) ? j.reducao : 1.0f;
    if (cv < 1.0f) return false;
    // graus da junta -> voltas do motor -> contagens. A referencia e a
    // contagem que sobraria se a junta estivesse em zero.
    const float voltas = (graus - j.grausHome) * red / 360.0f;
    ref = L.bruto - (int32_t)lroundf(voltas * cv);
  }

  portENTER_CRITICAL(&travaEnc);
  leitura[i].referencia = ref;
  portEXIT_CRITICAL(&travaEnc);
  return true;
}

void encoderZerar(uint8_t junta) {
  portENTER_CRITICAL(&travaEnc);
  for (uint8_t i = 0; i < 2; i++) {
    if (junta == 0 || junta == i + 1) {
      leitura[i].referencia = leitura[i].bruto;
      leitura[i].erro = 0.0f;
      // Zerar aqui e comecar de novo: acumulado antigo misturado com
      // referencia nova responde a uma pergunta que ninguem fez.
      leitura[i].passosTotais = 0;
      leitura[i].inversoes    = 0;
      leitura[i].brutoMin = leitura[i].brutoMax = leitura[i].bruto;
      leitura[i].velMax = leitura[i].velMin = 0.0f;
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
    // E os derivados recomecam. Trocar o registrador troca o SIGNIFICADO
    // do numero: comparar a leitura nova com a anterior seria medir a
    // distancia entre duas coisas diferentes, e sairia um salto de
    // dezenas de milhoes de contagens que nunca aconteceu.
    encTinhaAntes[i]  = false;
    leitura[i].delta        = 0;
    leitura[i].velocidade   = 0.0f;
    leitura[i].rpm          = 0.0f;
    leitura[i].sentido      = 0;
    leitura[i].passosTotais = 0;
    leitura[i].inversoes    = 0;
    leitura[i].velMax = leitura[i].velMin = 0.0f;
    leitura[i].brutoMin = leitura[i].brutoMax = 0;
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
  // O habilita vem primeiro. Ele e o unico caminho que sobrou para tirar
  // torque do braco: nao pode ficar atras de um autoteste de 3 segundos
  // na fila.
  // O habilita vem primeiro, e sai daqui depois de UM passo -- nunca com
  // a escrita inteira nas costas. Ver sonPasso().
  if (pedidoSon) {
    const uint8_t o = pedidoSon;
    pedidoSon = 0;
    sonComecar(o == 1, sonJuntaAlvo);
    sonPasso();
    proximaEm = millis();
    return;
  }
  if (estadoSon == SON_PENDENTE) {
    sonPasso();
    proximaEm = millis();
    return;
  }

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

  // QUEM NAO RESPONDE HA MUITO TEMPO PERGUNTA-SE DE VEZ EM QUANDO.
  //
  // O ciclo alternava as duas juntas sempre. Numa bancada com um driver
  // so -- que e o caso mais comum durante a montagem -- metade das
  // leituras era uma espera ate o timeout, e essa espera acontece na
  // MESMA tarefa que divide o nucleo 0 com a rede. O sintoma nao e o
  // motor: e a tela engasgando, "as vezes trava".
  //
  // Depois de cinco falhas seguidas a junta muda de regime: continua
  // sendo perguntada, uma vez a cada dez ciclos, o suficiente para
  // reaparecer sozinha assim que o cabo voltar. Uma leitura boa devolve
  // o ritmo normal na hora.
  static uint8_t  falhasSeguidas[2] = {0, 0};   // NOLINT
  static uint32_t ciclos = 0;
  ciclos++;
  if (falhasSeguidas[vez] >= 5 && (ciclos % 10) != 0) {
    const uint8_t outra = (uint8_t)(1 - vez);
    // A outra junta so assume se ela propria estiver respondendo --
    // senao as duas mudas viravam duas leituras por ciclo em vez de
    // nenhuma.
    if (configEncoder.reg[outra] != 0 && falhasSeguidas[outra] < 5) {
      vez = outra;
    } else {
      vez = outra;
      return;
    }
  }

  int32_t bruto = 0;
  uint8_t motivo = MOTIVO_OK;
  const bool ok = lerPosicao(vez, bruto, motivo);
  if (ok) falhasSeguidas[vez] = 0;
  else if (falhasSeguidas[vez] < 255) falhasSeguidas[vez]++;
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

  // A REFERENCIA sobrevive ao memset. Ela vem do NVS, e
  // carregarConfiguracoes() roda ANTES daqui no setup(): limpar tudo
  // apagaria o zero absoluto que o operador ensinou, e a maquina
  // nasceria localizada em qualquer lugar.
  const int32_t refGuardada[2] = { leitura[0].referencia, leitura[1].referencia };
  memset((void*)leitura, 0, sizeof(leitura));
  for (uint8_t i = 0; i < 2; i++) {
    leitura[i].motivo     = MOTIVO_NUNCA;
    leitura[i].referencia = refGuardada[i];
  }

  if (configEncoder.ativo) {
    abrirLinha();
    modoEscuta();
    Serial.print("[ENC] Modbus em "); Serial.print(configEncoder.baud);
    Serial.print(" bps, funcao "); Serial.print(configEncoder.funcao);
    Serial.print(", registrador "); Serial.print(configEncoder.reg[0]);
    Serial.print(", id "); Serial.print(configEncoder.id[0]);
    Serial.println();
    if (configEncoder.reg[1] == 0)
      Serial.println("[ENC] Junta 2 nao ligada (registrador 0).");
  } else {
    Serial.println("[ENC] Leitura de encoder desligada.");
  }

#ifndef ROBO2DOF_TESTE
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

#ifdef ROBO2DOF_TESTE
void encoderCicloTeste() { ciclo(); }
void encoderReiniciarTeste() {
  memset((void*)leitura, 0, sizeof(leitura));
  pedidoReabrir = false;
  linhaAberta = false;
  proximaEm = 0;
  vez = 0;
  for (uint8_t i = 0; i < 2; i++) {
    encUltimoMs[i]    = 0;
    encTinhaAntes[i]  = false;
    encBrutoAntes[i]  = 0;
  }
  nEnvio = nResposta = juntaDoQuadro = 0;
  // No ESP32 o boot zera isto sozinho; aqui as globais sobrevivem ao
  // setup(), e uma cacada marcada num cenario valeria no seguinte.
  pedidoTeste  = false;
  pedidoCaca   = 0;
  pedidoSon    = 0;
  estadoSon    = SON_OCIOSO;
  sonDriver    = 0;
  sonTentativa = 0;
  sonJuntaAlvo = 0;
  sonJuntaOk[0] = sonJuntaOk[1] = false;
  sonMotivo[0] = '\0';
  testeRodando = false;
  cacaEtapa    = 0;
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

  size_t p = 0;
  p += (size_t)snprintf(destino + p, tam - p, "junta %u  ->", (unsigned)jq);
  for (uint8_t k = 0; k < nE && p + 4 < tam; k++)
    p += (size_t)snprintf(destino + p, tam - p, " %02X", env[k]);
  if (p + 6 < tam) p += (size_t)snprintf(destino + p, tam - p, "   <-");
  if (!nR) { snprintf(destino + p, tam - p, " (silencio)"); return; }
  for (uint8_t k = 0; k < nR && p + 4 < tam; k++)
    p += (size_t)snprintf(destino + p, tam - p, " %02X", resp[k]);
}
