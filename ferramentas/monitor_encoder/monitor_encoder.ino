// =====================================================================
//  MONITOR DO ENCODER  --  ESP32 sozinho na placa
//
//  Le a posicao do encoder pelo driver, por Modbus RTU sobre RS485, e
//  mostra ao vivo: posicao, delta, velocidade, RPM, sentido e passos
//  acumulados. Grave no lugar do firmware e abra o monitor serial a
//  115200.
//
//  E o codigo que PROVOU que a leitura funciona nesta maquina. Quando o
//  sistema nao le e este le, a diferenca esta no sistema.
//
//  So leitura. Nenhuma funcao aqui escreve registrador: um engano que
//  escrevesse num parametro do servo estragaria a maquina de um jeito
//  que nao se desfaz pela tela.
//
//  Ver LEIA-ME.md nesta pasta.
// =====================================================================

#include <HardwareSerial.h>

// ---------------------------------------------------------------------
// CONFIGURACAO -- e aqui que se mexe
// ---------------------------------------------------------------------
#define ENCODER_MODBUS_BAUD    19200
#define ENCODER_MODBUS_PARITY  SERIAL_8N1
#define ENCODER_MODBUS_ID      1
#define ENCODER_MODBUS_FUNC    3       // 3 = holding, 4 = input registers

// Registrador da palavra BAIXA da posicao. A palavra ALTA e a seguinte.
// Este e o numero que muda de maquina para maquina: se nao souber o seu,
// ache com ferramentas/teste_rs485 (modo 7).
#define ENCODER_REG_BASE       90

// Contagens do encoder por volta do MOTOR. Meca com o modo 8 do
// teste_rs485; 131072 e um encoder de 17 bits.
#define CONTAGENS_POR_VOLTA    131072.0f

// Pinos do MAX485. Iguais aos do sistema (ver LIGACOES.md).
#define PIN_RS485_RX  22    // vem do RO, ja em 3,3 V
#define PIN_RS485_TX  21    // vai para o DI
#define PIN_RS485_DE   4    // 1 = transmitindo
#define PIN_RS485_RE  26    // 0 = ouvindo

// Espera pela resposta. 100 ms e folgado: na pratica a leitura acaba
// assim que o quadro fecha.
static const uint32_t ESPERA_MS = 100;

// Tremor de um ou dois passos com o eixo parado nao e movimento. Sem
// esta zona morta, o contador de inversoes nao valeria nada.
static const int32_t PARADO_CONTAGENS = 3;

static const uint16_t MAX_HISTORICO = 400;

HardwareSerial rs485(2);

// ---------------------------------------------------------------------
struct Leitura {
  int32_t  bruto;          // contagem crua, 32 bits
  int32_t  delta;          // desde a leitura anterior
  float    velocidade;     // contagens do motor por segundo
  float    rpm;            // voltas do motor por minuto
  int8_t   sentido;        // +1 cresce, -1 decresce, 0 parado
  uint32_t passos;         // soma de |delta|: o caminho andado
  uint32_t inversoes;
  float    graus;          // do eixo do MOTOR
  uint32_t quando;         // millis da leitura
  bool     valido;
};

struct Estatisticas {
  uint32_t leituras, erros;
  int32_t  brutoMin, brutoMax;
  float    velMin, velMax;
  uint32_t desde;
};

static Leitura      L;
static Estatisticas E;
static bool     pausado = false;
static bool     csvContinuo = false;
static int32_t  histBruto[MAX_HISTORICO];
static uint32_t histQuando[MAX_HISTORICO];
static float    histVel[MAX_HISTORICO];
static uint16_t histN = 0;      // quantos ja entraram (para o corte)
static uint16_t histIni = 0;    // inicio do anel

// =====================================================================
//  Linha RS485
// =====================================================================
static void modoEscuta() {
  digitalWrite(PIN_RS485_DE, LOW);
  digitalWrite(PIN_RS485_RE, LOW);
}
static void modoTransmissao() {
  digitalWrite(PIN_RS485_DE, HIGH);
  digitalWrite(PIN_RS485_RE, HIGH);
}

static void abrirLinha() {
  pinMode(PIN_RS485_DE, OUTPUT);
  pinMode(PIN_RS485_RE, OUTPUT);
  rs485.end();
  delay(10);
  rs485.begin(ENCODER_MODBUS_BAUD, ENCODER_MODBUS_PARITY,
              PIN_RS485_RX, PIN_RS485_TX);
  delay(10);
  modoEscuta();
}

static uint16_t crc16(const uint8_t* b, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= b[i];
    for (uint8_t k = 0; k < 8; k++)
      crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xA001) : (uint16_t)(crc >> 1);
  }
  return crc;
}

// Le 'quantos' registradores a partir de 'inicio'. Devolve false se a
// resposta nao veio, ou veio corrompida, ou veio como excecao.
static bool lerRegistradores(uint16_t inicio, uint16_t quantos,
                             uint16_t* palavras) {
  uint8_t q[8];
  q[0] = ENCODER_MODBUS_ID;
  q[1] = ENCODER_MODBUS_FUNC;
  q[2] = (uint8_t)(inicio >> 8);
  q[3] = (uint8_t)(inicio & 0xFF);
  q[4] = (uint8_t)(quantos >> 8);
  q[5] = (uint8_t)(quantos & 0xFF);
  const uint16_t c = crc16(q, 6);
  q[6] = (uint8_t)(c & 0xFF);
  q[7] = (uint8_t)(c >> 8);

  while (rs485.available()) rs485.read();

  modoTransmissao();
  delayMicroseconds(50);
  rs485.write(q, 8);
  rs485.flush();
  // flush() espera a fila esvaziar, mas o ultimo bit ainda pode estar
  // saindo do registrador de deslocamento. Baixar o DE agora corta o fim
  // do quadro e o escravo descarta calado.
  delayMicroseconds(1000);
  modoEscuta();

  // A resposta boa tem tamanho conhecido: para assim que ela fecha.
  const size_t esperados = 3 + (size_t)quantos * 2 + 2;
  uint8_t r[64];
  size_t n = 0;
  const uint32_t inicioMs = millis();
  while ((int32_t)(millis() - inicioMs) < (int32_t)ESPERA_MS &&
         n < sizeof(r)) {
    if (rs485.available()) {
      r[n++] = (uint8_t)rs485.read();
      if (n >= esperados) break;
    }
  }
  if (n < 5) return false;

  const uint16_t cc = crc16(r, n - 2);
  if (r[n - 2] != (uint8_t)(cc & 0xFF) || r[n - 1] != (uint8_t)(cc >> 8)) return false;
  if (r[0] != ENCODER_MODBUS_ID) return false;
  if (r[1] & 0x80) return false;                 // excecao
  if (r[1] != ENCODER_MODBUS_FUNC) return false;
  if (r[2] != quantos * 2) return false;

  for (uint16_t k = 0; k < quantos; k++)
    palavras[k] = (uint16_t)((r[3 + k * 2] << 8) | r[4 + k * 2]);
  return true;
}

// Posicao de 32 bits: uma pergunta, dois registradores, palavra baixa
// primeiro. Uma pergunta so tambem e ATOMICA -- o par sai do mesmo
// instante do contador.
static bool lerPosicao(int32_t& valor) {
  uint16_t p[2];
  if (!lerRegistradores(ENCODER_REG_BASE, 2, p)) return false;
  valor = (int32_t)(((uint32_t)p[1] << 16) | p[0]);
  return true;
}

// =====================================================================
//  Contas
// =====================================================================
static void guardarNoHistorico() {
  const uint16_t i = (histIni + histN) % MAX_HISTORICO;
  histBruto[i]  = L.bruto;
  histQuando[i] = L.quando;
  histVel[i]    = L.velocidade;
  if (histN < MAX_HISTORICO) histN++;
  else histIni = (histIni + 1) % MAX_HISTORICO;   // anel: o velho sai
}
static uint16_t hist(uint16_t k) { return (histIni + k) % MAX_HISTORICO; }

static void atualizar(int32_t bruto) {
  static bool     tinhaAntes = false;
  static int32_t  brutoAntes = 0;
  static uint32_t quandoAntes = 0;

  const uint32_t agora = millis();
  L.bruto  = bruto;
  L.quando = agora;
  L.valido = true;
  L.graus  = (float)bruto * 360.0f / CONTAGENS_POR_VOLTA;

  E.leituras++;
  if (E.leituras == 1) { E.brutoMin = E.brutoMax = bruto; E.desde = agora; }
  if (bruto < E.brutoMin) E.brutoMin = bruto;
  if (bruto > E.brutoMax) E.brutoMax = bruto;

  if (tinhaAntes) {
    // Subtracao em complemento de dois: a volta do contador de 32 bits
    // sai certa sozinha, sem caso especial.
    const int32_t  d  = (int32_t)((uint32_t)bruto - (uint32_t)brutoAntes);
    const uint32_t dt = agora - quandoAntes;
    L.delta = d;

    if (dt > 0 && dt < 2000) {
      // Em float: (d * 1000) em inteiro estoura com meio milhao de
      // contagens, que um eixo rapido faz em um segundo.
      L.velocidade = (float)d * 1000.0f / (float)dt;
      L.rpm = L.velocidade * 60.0f / CONTAGENS_POR_VOLTA;
    }

    const int32_t mod = d < 0 ? -d : d;
    if (mod > PARADO_CONTAGENS) {
      const int8_t s = (d > 0) ? 1 : -1;
      if (L.sentido != 0 && L.sentido != s) L.inversoes++;
      L.sentido = s;
      L.passos += (uint32_t)mod;
    } else {
      L.sentido = 0;
    }
    if (L.velocidade > E.velMax) E.velMax = L.velocidade;
    if (L.velocidade < E.velMin) E.velMin = L.velocidade;
  }

  tinhaAntes  = true;
  brutoAntes  = bruto;
  quandoAntes = agora;
  guardarNoHistorico();
}

static void aoFalhar() {
  E.erros++;
  // Sem leitura nao ha velocidade: manter a ultima faria a tela dizer que
  // o eixo continua girando depois que o fio caiu.
  L.valido = false;
  L.velocidade = 0.0f;
  L.rpm = 0.0f;
  L.sentido = 0;
  L.delta = 0;
}

static void zerar() {
  memset(&L, 0, sizeof(L));
  memset(&E, 0, sizeof(E));
  histN = histIni = 0;
  Serial.println("\n-- passos e estatisticas zerados --");
}

// =====================================================================
//  Tela
// =====================================================================
static const char* nomeSentido() {
  return L.sentido > 0 ? "cresce  " : L.sentido < 0 ? "decresce" : "parado  ";
}

static void cabecalho() {
  Serial.println();
  Serial.println("  tempo |      bruto |  delta |    c/s |    rpm | sentido  |  graus |   passos");
  Serial.println("  ------+------------+--------+--------+--------+----------+--------+---------");
}

static void linha() {
  Serial.printf("  %5lu | %10ld | %+6ld | %6.0f | %6.1f | %s | %6.1f | %8lu\n",
                (unsigned long)(L.quando / 1000), (long)L.bruto, (long)L.delta,
                (double)L.velocidade, (double)L.rpm, nomeSentido(),
                (double)L.graus, (unsigned long)L.passos);
}

static void estatisticas() {
  const float seg = (millis() - E.desde) / 1000.0f;
  Serial.println();
  Serial.println("== ESTATISTICAS ==");
  Serial.printf("  tempo ligado        %.1f s\n", (double)seg);
  Serial.printf("  leituras            %lu  (%.1f por segundo)\n",
                (unsigned long)E.leituras, seg > 0.5f ? E.leituras / seg : 0.0);
  Serial.printf("  falhas              %lu\n", (unsigned long)E.erros);
  Serial.printf("  faixa percorrida    %ld ate %ld  (%ld contagens)\n",
                (long)E.brutoMin, (long)E.brutoMax,
                (long)(E.brutoMax - E.brutoMin));
  Serial.printf("  velocidade          %.0f ate %.0f c/s\n",
                (double)E.velMin, (double)E.velMax);
  Serial.printf("  inversoes           %lu\n", (unsigned long)L.inversoes);
  Serial.printf("  caminho andado      %lu contagens  (%.2f voltas do motor)\n",
                (unsigned long)L.passos, (double)L.passos / CONTAGENS_POR_VOLTA);
  Serial.println();
}

static void resumo() {
  Serial.println();
  Serial.println("== LEITURA ATUAL ==");
  Serial.printf("  bruto               %ld\n", (long)L.bruto);
  Serial.printf("  graus do motor      %.2f\n", (double)L.graus);
  Serial.printf("  voltas do motor     %.4f\n",
                (double)L.bruto / CONTAGENS_POR_VOLTA);
  Serial.printf("  delta               %+ld\n", (long)L.delta);
  Serial.printf("  velocidade          %.0f c/s  (%.1f rpm)\n",
                (double)L.velocidade, (double)L.rpm);
  Serial.printf("  sentido             %s\n", nomeSentido());
  Serial.printf("  caminho andado      %lu contagens\n", (unsigned long)L.passos);
  Serial.printf("  estado              %s\n", L.valido ? "lendo" : "SEM LEITURA");
  Serial.println();
}

static void grafico() {
  if (histN < 2) { Serial.println("Poucos dados ainda."); return; }
  int32_t lo = histBruto[hist(0)], hi = lo;
  for (uint16_t k = 0; k < histN; k++) {
    const int32_t v = histBruto[hist(k)];
    if (v < lo) lo = v;
    if (v > hi) hi = v;
  }
  int32_t faixa = hi - lo;
  if (faixa == 0) faixa = 1;

  Serial.println();
  Serial.printf("== POSICAO, ultimos %u pontos  (%ld ate %ld) ==\n",
                (unsigned)(histN > 40 ? 40 : histN), (long)lo, (long)hi);
  const uint16_t ini = histN > 40 ? histN - 40 : 0;
  for (uint16_t k = ini; k < histN; k++) {
    const int32_t v = histBruto[hist(k)];
    const int col = (int)(((int64_t)(v - lo) * 50) / faixa);
    Serial.print("  ");
    for (int j = 0; j < 51; j++) Serial.print(j == col ? '*' : (j < col ? '-' : ' '));
    Serial.printf(" %ld\n", (long)v);
  }
  Serial.println();
}

static void csv() {
  Serial.println();
  Serial.println("ms,bruto,delta,velocidade,rpm,graus");
  for (uint16_t k = 0; k < histN; k++) {
    const uint16_t i = hist(k);
    const int32_t d = (k == 0) ? 0
        : (int32_t)((uint32_t)histBruto[i] - (uint32_t)histBruto[hist(k - 1)]);
    Serial.printf("%lu,%ld,%ld,%.0f,%.2f,%.2f\n",
                  (unsigned long)histQuando[i], (long)histBruto[i], (long)d,
                  (double)histVel[i],
                  (double)(histVel[i] * 60.0f / CONTAGENS_POR_VOLTA),
                  (double)((float)histBruto[i] * 360.0f / CONTAGENS_POR_VOLTA));
  }
  Serial.println();
}

static void configuracao() {
  Serial.println();
  Serial.println("== CONFIGURACAO ==");
  Serial.printf("  %d bps 8N1, id %d, funcao %d\n",
                ENCODER_MODBUS_BAUD, ENCODER_MODBUS_ID, ENCODER_MODBUS_FUNC);
  Serial.printf("  registrador %d (baixa) e %d (alta), palavra baixa primeiro\n",
                ENCODER_REG_BASE, ENCODER_REG_BASE + 1);
  Serial.printf("  %.0f contagens por volta do motor\n",
                (double)CONTAGENS_POR_VOLTA);
  Serial.printf("  pinos: RX %d, TX %d, DE %d, RE %d\n",
                PIN_RS485_RX, PIN_RS485_TX, PIN_RS485_DE, PIN_RS485_RE);
  Serial.println();
}

// Marca, o operador da UMA volta no eixo do motor, marca de novo. E o
// unico numero do encoder que nao da para descobrir olhando.
static void medirContagensPorVolta() {
  int32_t antes = 0;
  Serial.println();
  Serial.println("== CONTAGENS POR VOLTA ==");
  if (!lerPosicao(antes)) { Serial.println("  sem leitura. Confira a ligacao."); return; }
  Serial.printf("  marcado em %ld\n", (long)antes);
  Serial.println("  >>> de UMA VOLTA COMPLETA no eixo do MOTOR e aperte uma tecla");
  while (!Serial.available()) delay(10);
  while (Serial.available()) Serial.read();

  int32_t depois = 0;
  if (!lerPosicao(depois)) { Serial.println("  perdi a leitura. Repita."); return; }
  int32_t d = depois - antes;
  if (d < 0) d = -d;
  Serial.printf("  agora em %ld  ->  uma volta = %ld contagens\n",
                (long)depois, (long)d);
  if (d < 100) {
    Serial.println("  Pouco demais para ser uma volta. O eixo girou mesmo?");
    return;
  }
  // Encaixa no redondo mais proximo dentro de 3%: uma volta a mao nunca
  // fecha exata, e e mais provavel a mao ter ficado torta do que o
  // encoder ter numero quebrado.
  const int32_t COMUNS[] = {1024, 2500, 4096, 10000, 16384, 32768,
                            65536, 131072, 262144, 524288};
  for (int32_t c : COMUNS) {
    const int32_t err = d > c ? d - c : c - d;
    if ((int64_t)err * 100 < (int64_t)c * 3) {
      Serial.printf("  isso e praticamente %ld -- use esse numero redondo.\n", (long)c);
      break;
    }
  }
  Serial.println("  Ponha em CONTAGENS_POR_VOLTA, e no painel do robo.");
  Serial.println();
}

static void ajuda() {
  Serial.println();
  Serial.println("== COMANDOS ==");
  Serial.println("  r  zerar passos e estatisticas      g  grafico da posicao");
  Serial.println("  p  pausar / continuar               e  despejar CSV");
  Serial.println("  s  estatisticas                     c  configuracao em uso");
  Serial.println("  d  resumo da leitura atual          8  medir contagens por volta");
  Serial.println("  ?  esta ajuda                       9  CSV continuo liga/desliga");
  Serial.println();
}

// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  abrirLinha();
  memset(&L, 0, sizeof(L));
  memset(&E, 0, sizeof(E));
  E.desde = millis();

  Serial.println();
  Serial.println("MONITOR DO ENCODER -- ESP32 sozinho na placa");
  configuracao();
  ajuda();
  cabecalho();
}

void loop() {
  static uint32_t ultimoDesenho = 0;
  static uint32_t ultimaFalhaAvisada = 0;

  if (Serial.available()) {
    const char cmd = (char)Serial.read();
    while (Serial.available()) Serial.read();
    switch (cmd) {
      case 'r': case 'R': zerar(); cabecalho(); break;
      case 'p': case 'P':
        pausado = !pausado;
        Serial.println(pausado ? "\n-- pausado --" : "\n-- continuando --");
        if (!pausado) cabecalho();
        break;
      case 's': case 'S': estatisticas(); break;
      case 'd': case 'D': resumo(); break;
      case 'g': case 'G': grafico(); break;
      case 'e': case 'E': csv(); break;
      case 'c': case 'C': configuracao(); break;
      case '8': medirContagensPorVolta(); cabecalho(); break;
      case '9':
        csvContinuo = !csvContinuo;
        Serial.println(csvContinuo ? "\nms,bruto,delta,velocidade,rpm,graus"
                                   : "\n-- CSV continuo desligado --");
        break;
      case '?': ajuda(); break;
      default: break;
    }
  }

  if (pausado) { delay(50); return; }

  int32_t bruto = 0;
  if (lerPosicao(bruto)) {
    atualizar(bruto);
  } else {
    aoFalhar();
    // Uma linha a cada 2 s: enxurrada de erro esconde o proprio erro.
    if ((uint32_t)(millis() - ultimaFalhaAvisada) > 2000) {
      ultimaFalhaAvisada = millis();
      Serial.printf("  SEM LEITURA (%lu falhas). Confira A/B, DE/RE, endereco "
                    "e registrador.\n", (unsigned long)E.erros);
    }
    delay(20);
    return;
  }

  if ((uint32_t)(millis() - ultimoDesenho) > 100) {
    ultimoDesenho = millis();
    if (csvContinuo) {
      Serial.printf("%lu,%ld,%ld,%.0f,%.2f,%.2f\n",
                    (unsigned long)L.quando, (long)L.bruto, (long)L.delta,
                    (double)L.velocidade, (double)L.rpm, (double)L.graus);
    } else {
      linha();
    }
  }
  delay(10);
}
