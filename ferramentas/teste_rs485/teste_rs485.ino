// =====================================================================
//  Diagnostico de RS485 / Modbus RTU  --  RoboCNC 2DOF
//
//  Sketch AVULSO. Nao faz parte do firmware da maquina: grave este
//  sozinho, com o robo desligado dos motores se quiser.
//
//  PARA QUE SERVE
//
//  Descobrir se o driver HLTNC T3D responde em RS485 e, se responder,
//  em que velocidade, paridade e endereco -- e depois achar em qual
//  registrador esta a posicao do encoder.
//
//  ISTO NAO CHUTA REGISTRADOR. O mapa Modbus do T3D varia por versao de
//  firmware e nao esta publicado; qualquer endereco que este arquivo
//  trouxesse pronto seria adivinhacao. O que ele faz e VARRER e mostrar
//  o que responde, que e como se acha o mapa quando o manual nao ajuda.
//
//  LIGACAO (ver ferramentas/RS485_T3D.md antes de ligar)
//
//      MAX485            ESP32
//      -------           -----
//      RO   ---> divisor 5V->3,3V ---> GPIO 22   (RX do ESP32)
//      DI   <--------------------------GPIO 21   (TX do ESP32)
//      DE   <--------------------------GPIO  4
//      RE   <--------------------------GPIO 26
//      VCC  --- 5 V        GND --- GND (comum com o ESP32)
//      A, B --- par trancado ate o driver
//
//  DE e RE em pinos SEPARADOS de proposito: e o que permite o autoteste
//  de loopback do modo 1, que prova a fiacao sem depender do driver.
//  Em operacao normal eles andam juntos.
//
//  Estes quatro pinos estao livres no mapa do firmware (config.h).
// =====================================================================

#include <string.h>

// ---------------------------------------------------------------------
// Pinos
// ---------------------------------------------------------------------
static const int PIN_RX = 22;   // vem do RO do MAX485, ja em 3,3 V
static const int PIN_TX = 21;   // vai para o DI
static const int PIN_DE = 4;    // driver enable  (1 = transmitindo)
static const int PIN_RE = 26;   // receiver enable (0 = ouvindo)

HardwareSerial rs(2);           // UART2 do ESP32

// ---------------------------------------------------------------------
// Configuracao corrente da linha
// ---------------------------------------------------------------------
static const uint32_t BAUDS[] = {9600, 19200, 38400, 57600, 115200, 4800, 250000};
static const uint8_t  N_BAUDS = sizeof(BAUDS) / sizeof(BAUDS[0]);

struct Paridade { uint32_t cfg; const char* nome; uint8_t bitsPorChar; };
static const Paridade PARIDADES[] = {
  {SERIAL_8N1, "8N1", 10},
  {SERIAL_8E1, "8E1", 11},
  {SERIAL_8O1, "8O1", 11},
};
static const uint8_t N_PARIDADES = sizeof(PARIDADES) / sizeof(PARIDADES[0]);

static uint32_t baudAtual     = 9600;
static uint8_t  paridadeAtual = 0;
static uint8_t  idAtual       = 1;
static uint8_t  funcAtual     = 3;      // 3 = holding, 4 = input registers
static bool     ouvirEco      = false;  // deixa o receptor ligado ao transmitir

// ---------------------------------------------------------------------
// Tempo de um caractere na velocidade atual, em microssegundos.
// Modbus RTU mede silencio em caracteres, nao em milissegundos fixos.
// ---------------------------------------------------------------------
static uint32_t usPorChar() {
  const uint32_t bits = PARIDADES[paridadeAtual].bitsPorChar;
  return (bits * 1000000UL) / (baudAtual ? baudAtual : 9600);
}

// Silencio entre quadros: 3,5 caracteres, com piso de 1750 us -- acima
// de 19200 a norma fixa esse piso em vez de encolher junto.
static uint32_t usEntreQuadros() {
  const uint32_t t = (usPorChar() * 7) / 2;
  return t < 1750 ? 1750 : t;
}

static void abrirLinha() {
  rs.end();
  delay(5);
  rs.begin(baudAtual, PARIDADES[paridadeAtual].cfg, PIN_RX, PIN_TX);
  delay(5);
}

// ---------------------------------------------------------------------
// Controle do transceptor
// ---------------------------------------------------------------------
static void modoEscuta() {
  digitalWrite(PIN_DE, LOW);    // driver desligado
  digitalWrite(PIN_RE, LOW);    // receptor ligado (RE e ativo em baixo)
}
static void modoTransmissao() {
  // Com ouvirEco, o receptor fica ligado junto: o que sai volta pelo RO.
  // Serve para provar a fiacao sem nada do outro lado.
  digitalWrite(PIN_RE, ouvirEco ? LOW : HIGH);
  digitalWrite(PIN_DE, HIGH);
}

// ---------------------------------------------------------------------
// CRC16 do Modbus RTU (polinomio 0xA001, inicio 0xFFFF)
// ---------------------------------------------------------------------
static uint16_t crc16(const uint8_t* b, size_t n) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < n; i++) {
    crc ^= b[i];
    for (uint8_t k = 0; k < 8; k++) {
      if (crc & 1) crc = (uint16_t)((crc >> 1) ^ 0xA001);
      else         crc = (uint16_t)(crc >> 1);
    }
  }
  return crc;
}

static void hexLinha(const uint8_t* b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (b[i] < 0x10) Serial.print("0");
    Serial.print(b[i], HEX);
    Serial.print(" ");
  }
}

// ---------------------------------------------------------------------
// Envia um quadro e espera a resposta.
//
// Devolve quantos bytes chegaram (0 = silencio). NAO valida CRC: quem
// chama decide. Byte torto tambem e informacao -- quer dizer que a
// velocidade esta perto e a paridade errada, e isso e ouro.
// ---------------------------------------------------------------------
static size_t trocar(const uint8_t* saida, size_t nSaida,
                     uint8_t* entrada, size_t maxEntrada,
                     uint32_t esperaMs) {
  // Limpa lixo antigo da linha.
  while (rs.available()) rs.read();

  modoTransmissao();
  delayMicroseconds(50);                 // o transceptor precisa acordar
  rs.write(saida, nSaida);
  rs.flush();
  // flush() esvazia a fila, mas o ultimo bit ainda pode estar saindo do
  // registrador de deslocamento. Baixar DE agora corta o fim do quadro,
  // e o escravo descarta tudo -- e uma das causas classicas de "nao
  // responde nada".
  delayMicroseconds(usPorChar() * 2);
  modoEscuta();

  size_t n = 0;
  const uint32_t limite = millis() + esperaMs;
  uint32_t ultimoByteUs = 0;

  while (millis() < limite) {
    if (rs.available()) {
      while (rs.available() && n < maxEntrada) {
        entrada[n++] = (uint8_t)rs.read();
        ultimoByteUs = micros();
      }
      // Fim do quadro: silencio de 3,5 caracteres depois do ultimo byte.
      while (n < maxEntrada) {
        if (rs.available()) {
          entrada[n++] = (uint8_t)rs.read();
          ultimoByteUs = micros();
        } else if (micros() - ultimoByteUs > usEntreQuadros()) {
          return n;
        }
      }
      return n;
    }
  }
  return n;
}

// Monta e envia "ler N registradores a partir de X".
static size_t lerRegistradores(uint8_t id, uint8_t func, uint16_t inicio,
                               uint16_t quantos, uint8_t* resp, size_t maxResp,
                               uint32_t esperaMs) {
  uint8_t q[8];
  q[0] = id;
  q[1] = func;
  q[2] = (uint8_t)(inicio >> 8);
  q[3] = (uint8_t)(inicio & 0xFF);
  q[4] = (uint8_t)(quantos >> 8);
  q[5] = (uint8_t)(quantos & 0xFF);
  const uint16_t c = crc16(q, 6);
  q[6] = (uint8_t)(c & 0xFF);
  q[7] = (uint8_t)(c >> 8);
  return trocar(q, 8, resp, maxResp, esperaMs);
}

static bool crcConfere(const uint8_t* b, size_t n) {
  if (n < 4) return false;
  const uint16_t c = crc16(b, n - 2);
  return (b[n - 2] == (uint8_t)(c & 0xFF)) && (b[n - 1] == (uint8_t)(c >> 8));
}

// =====================================================================
//  MODO 1 - Autoteste do modulo (loopback)
//
//  Nao precisa do driver. Com DE=1 e RE=0 ao mesmo tempo, o MAX485
//  dirige o par A/B e le de volta o que ele proprio colocou la. Se isto
//  falhar, o problema esta entre o ESP32 e o modulo -- pino trocado,
//  nivel logico, alimentacao -- e nao adianta procurar o driver.
// =====================================================================
static void autoteste() {
  Serial.println();
  Serial.println("== AUTOTESTE DO MODULO (loopback) ==");
  Serial.println("Pode deixar o cabo do driver desligado. Isto so testa");
  Serial.println("ESP32 <-> MAX485.");
  Serial.println();

  const bool guardado = ouvirEco;
  ouvirEco = true;

  uint8_t ok = 0, total = 0;
  for (uint8_t p = 0; p < N_PARIDADES; p++) {
    for (uint8_t b = 0; b < 5; b++) {           // ate 115200
      baudAtual = BAUDS[b];
      paridadeAtual = p;
      abrirLinha();

      const uint8_t padrao[] = {0x55, 0xAA, 0x00, 0xFF, 0x5A, 0xA5};
      uint8_t volta[16];
      const size_t n = trocar(padrao, sizeof(padrao), volta, sizeof(volta), 60);
      total++;

      const bool igual = (n == sizeof(padrao)) &&
                         (memcmp(padrao, volta, n) == 0);
      if (igual) ok++;

      Serial.print(igual ? "  OK   " : "  --   ");
      Serial.print(baudAtual);
      Serial.print(" ");
      Serial.print(PARIDADES[p].nome);
      Serial.print("   enviou 6 bytes, voltaram ");
      Serial.print((int)n);
      if (n && !igual) { Serial.print("  ["); hexLinha(volta, n); Serial.print("]"); }
      Serial.println();
    }
  }

  ouvirEco = guardado;
  Serial.println();
  if (ok == total) {
    Serial.println("MODULO OK em todas as velocidades.");
    Serial.println("A fiacao ESP32<->MAX485 esta boa. Se mesmo assim nada");
    Serial.println("responde, o problema esta do lado do driver ou do par A/B:");
    Serial.println("  - A e B trocados (troque os dois fios e repita o modo 3)");
    Serial.println("  - RS485 desabilitado nos parametros do driver");
    Serial.println("  - pino errado no RJ45");
  } else if (ok == 0) {
    Serial.println("NADA VOLTOU EM NENHUMA VELOCIDADE.");
    Serial.println("O problema esta entre o ESP32 e o modulo:");
    Serial.println("  - RO nao chegou no GPIO de RX (confira o divisor)");
    Serial.println("  - DI, DE ou RE em pino trocado");
    Serial.println("  - modulo sem 5 V, ou GND nao comum com o ESP32");
    Serial.println("  - conversor de nivel bidirecional de MOSFET no RO:");
    Serial.println("    troque por divisor de 2 resistores (ver o .md)");
  } else {
    Serial.println("VOLTOU SO EM PARTE DAS VELOCIDADES.");
    Serial.println("Cheira a nivel logico marginal no RO ou fio comprido.");
  }
  Serial.println();
}

// =====================================================================
//  MODO 2 - Escuta
//
//  So ouve. Serve para ver se aparece qualquer coisa na linha, e para
//  espiar a conversa se voce ligar o software do fabricante em paralelo
//  -- nesse caso da para LER o quadro que ele manda e copiar o endereco
//  do registrador direto de la. E o jeito mais rapido de achar o mapa.
// =====================================================================
static void escutar() {
  Serial.println();
  Serial.println("== ESCUTA ==");
  Serial.print("Ouvindo em "); Serial.print(baudAtual);
  Serial.print(" "); Serial.print(PARIDADES[paridadeAtual].nome);
  Serial.println(". Qualquer tecla encerra.");
  Serial.println();

  abrirLinha();
  modoEscuta();
  while (rs.available()) rs.read();

  uint8_t buf[256];
  size_t n = 0;
  uint32_t ultimoUs = micros();

  while (!Serial.available()) {
    while (rs.available() && n < sizeof(buf)) {
      buf[n++] = (uint8_t)rs.read();
      ultimoUs = micros();
    }
    if (n && micros() - ultimoUs > usEntreQuadros()) {
      Serial.print(millis());
      Serial.print(" ms  ");
      Serial.print((int)n);
      Serial.print(" bytes  ");
      hexLinha(buf, n);
      Serial.print(crcConfere(buf, n) ? "  <- CRC OK" : "  <- CRC nao bate");
      Serial.println();
      n = 0;
    }
  }
  while (Serial.available()) Serial.read();
  Serial.println("(fim da escuta)");
}

// =====================================================================
//  MODO 3 - Procurar o driver
//
//  Varre velocidade x paridade x endereco. Reporta qualquer sinal de
//  vida, inclusive:
//
//    - resposta valida        -> achou
//    - resposta de EXCECAO    -> ACHOU TAMBEM. O escravo respondeu
//                                "esse registrador nao existe", o que
//                                prova que ele esta la e falando.
//    - bytes com CRC ruim     -> a velocidade esta perto, a paridade
//                                provavelmente errada
// =====================================================================
static void procurar(uint8_t idMax) {
  Serial.println();
  Serial.println("== PROCURANDO O DRIVER ==");
  Serial.print("Enderecos 1 a "); Serial.print((int)idMax);
  Serial.println(", todas as velocidades e paridades. Pode demorar.");
  Serial.println();

  uint8_t achados = 0;
  for (uint8_t b = 0; b < N_BAUDS; b++) {
    for (uint8_t p = 0; p < N_PARIDADES; p++) {
      baudAtual = BAUDS[b];
      paridadeAtual = p;
      abrirLinha();
      Serial.print("  "); Serial.print(baudAtual);
      Serial.print(" "); Serial.print(PARIDADES[p].nome); Serial.print("  ");

      bool algoAqui = false;
      for (uint8_t id = 1; id <= idMax; id++) {
        uint8_t r[64];
        // Funcao 3 e depois 4: a posicao pode estar em holding ou em
        // input register, e drivers recusam uma e aceitam a outra.
        for (uint8_t f = 3; f <= 4; f++) {
          const size_t n = lerRegistradores(id, f, 0, 1, r, sizeof(r), 80);
          if (!n) continue;
          algoAqui = true;
          Serial.println();
          Serial.print("    id "); Serial.print((int)id);
          Serial.print(" func "); Serial.print((int)f);
          Serial.print("  <- "); hexLinha(r, n);

          if (crcConfere(r, n) && r[0] == id) {
            if (r[1] & 0x80) {
              Serial.print("  *** ACHOU (excecao ");
              Serial.print((int)r[2]);
              Serial.print("): o driver esta AI e respondeu. So o");
              Serial.println(" registrador 0 que nao serve.");
            } else {
              Serial.println("  *** ACHOU, resposta valida ***");
            }
            achados++;
            idAtual = id; funcAtual = f;
          } else {
            Serial.println("  (CRC nao bate: velocidade perto, paridade suspeita)");
          }
          Serial.print("  ");
        }
      }
      if (!algoAqui) Serial.println("silencio");
    }
  }

  Serial.println();
  if (achados) {
    Serial.print("Achado. Ultima combinacao boa: ");
    Serial.print(baudAtual); Serial.print(" ");
    Serial.print(PARIDADES[paridadeAtual].nome);
    Serial.print("  id "); Serial.print((int)idAtual);
    Serial.print("  funcao "); Serial.println((int)funcAtual);
    Serial.println("Agora use o modo 4 para achar o registrador da posicao.");
  } else {
    Serial.println("SILENCIO ABSOLUTO em todas as combinacoes.");
    Serial.println("Nesta ordem, e o que costuma ser:");
    Serial.println("  1. Rode o modo 1. Se ele falhar, o problema nem chegou");
    Serial.println("     na linha RS485.");
    Serial.println("  2. Troque A com B. E a causa numero um, e o teste");
    Serial.println("     custa 30 segundos.");
    Serial.println("  3. Confira no manual QUAIS pinos do RJ45 sao A e B.");
    Serial.println("  4. Veja nos parametros do driver se o RS485 esta");
    Serial.println("     habilitado e qual o endereco e a velocidade.");
    Serial.println("     Muito driver sai de fabrica com a porta desligada.");
    Serial.println("  5. GND do ESP32 comum com o do driver.");
  }
  Serial.println();
}

// =====================================================================
//  MODO 4 - Varrer registradores
//
//  Com o driver ja respondendo, le faixa por faixa e mostra o que tem
//  dentro. Para achar o encoder: rode uma vez, GIRE O EIXO A MAO, rode
//  de novo e compare. O registrador que mudou junto com o eixo e o da
//  posicao. E assim que se acha sem manual.
// =====================================================================
static void varrerRegistradores(uint16_t inicio, uint16_t fim) {
  Serial.println();
  Serial.println("== VARREDURA DE REGISTRADORES ==");
  Serial.print("id "); Serial.print((int)idAtual);
  Serial.print("  funcao "); Serial.print((int)funcAtual);
  Serial.print("  "); Serial.print(baudAtual);
  Serial.print(" "); Serial.println(PARIDADES[paridadeAtual].nome);
  Serial.println();

  abrirLinha();
  uint8_t r[64];
  uint16_t lidos = 0;

  for (uint32_t a = inicio; a <= fim; a += 8) {
    const uint16_t quantos = (uint16_t)((fim - a + 1) < 8 ? (fim - a + 1) : 8);
    const size_t n = lerRegistradores(idAtual, funcAtual, (uint16_t)a,
                                      quantos, r, sizeof(r), 150);
    if (!n) continue;
    if (!crcConfere(r, n)) continue;
    if (r[1] & 0x80) continue;                  // excecao: faixa nao existe
    if ((size_t)r[2] + 5 > n) continue;

    Serial.print("  0x");
    if (a < 0x1000) Serial.print("0");
    if (a < 0x100)  Serial.print("0");
    if (a < 0x10)   Serial.print("0");
    Serial.print((unsigned)a, HEX);
    Serial.print(" (");
    Serial.print((unsigned)a);
    Serial.print(")  ");
    for (uint8_t k = 0; k < r[2] / 2; k++) {
      const uint16_t v = (uint16_t)((r[3 + k * 2] << 8) | r[4 + k * 2]);
      Serial.print(v);
      Serial.print("  ");
    }
    Serial.println();
    lidos++;
  }

  Serial.println();
  if (!lidos) {
    Serial.println("Nada respondeu nesta faixa. Tente outra, ou a funcao 4.");
  } else {
    Serial.println("Agora GIRE O EIXO A MAO e rode a mesma varredura.");
    Serial.println("O registrador que mudou junto com o eixo e a posicao.");
    Serial.println("Posicao costuma ocupar DOIS registradores (32 bits):");
    Serial.println("valor = (alto << 16) | baixo, ou o contrario -- gire");
    Serial.println("bastante e veja qual dos dois anda mais devagar.");
  }
  Serial.println();
}

// =====================================================================
//  MODO 5 - Quadro cru
//
//  Voce digita os bytes em hexadecimal SEM o CRC (ele e calculado
//  aqui) e ve a resposta crua. Serve para o dia em que voce tiver o
//  manual na mao.
// =====================================================================
static uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  return 0xFF;
}

static void quadroCru(const String& texto) {
  uint8_t q[64];
  size_t n = 0;
  uint8_t alto = 0xFF;

  for (size_t i = 0; i < texto.length() && n < sizeof(q) - 2; i++) {
    const uint8_t v = hexNibble(texto[i]);
    if (v == 0xFF) { continue; }
    if (alto == 0xFF) { alto = v; }
    else { q[n++] = (uint8_t)((alto << 4) | v); alto = 0xFF; }
  }
  if (n < 2) {
    Serial.println("Poucos bytes. Exemplo: 01 03 00 00 00 02");
    return;
  }

  const uint16_t c = crc16(q, n);
  q[n++] = (uint8_t)(c & 0xFF);
  q[n++] = (uint8_t)(c >> 8);

  Serial.print("  -> "); hexLinha(q, n); Serial.println();

  abrirLinha();
  uint8_t r[64];
  const size_t nr = trocar(q, n, r, sizeof(r), 300);
  if (!nr) { Serial.println("  <- silencio"); return; }
  Serial.print("  <- "); hexLinha(r, nr);
  Serial.println(crcConfere(r, nr) ? "  CRC OK" : "  CRC nao bate");
}

// =====================================================================
static void menu() {
  Serial.println();
  Serial.println("=====================================================");
  Serial.println(" DIAGNOSTICO RS485 / MODBUS RTU -- RoboCNC");
  Serial.println("=====================================================");
  Serial.print(" atual: "); Serial.print(baudAtual);
  Serial.print(" "); Serial.print(PARIDADES[paridadeAtual].nome);
  Serial.print("  id "); Serial.print((int)idAtual);
  Serial.print("  funcao "); Serial.println((int)funcAtual);
  Serial.println();
  Serial.println(" 1              autoteste do modulo (nao precisa do driver)");
  Serial.println(" 2              escutar a linha");
  Serial.println(" 3              procurar o driver (id 1..16)");
  Serial.println(" 3 247          procurar em todos os enderecos");
  Serial.println(" 4              varrer registradores 0..255");
  Serial.println(" 4 4096 4351    varrer a faixa que voce quiser");
  Serial.println(" 5 01 03 00 00 00 02    mandar quadro cru (CRC automatico)");
  Serial.println(" b 19200        fixar a velocidade");
  Serial.println(" p 1            paridade: 0=8N1 1=8E1 2=8O1");
  Serial.println(" i 2            fixar o endereco do escravo");
  Serial.println(" f 4            funcao: 3=holding 4=input registers");
  Serial.println(" ?              este menu");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(400);

  pinMode(PIN_DE, OUTPUT);
  pinMode(PIN_RE, OUTPUT);
  modoEscuta();
  abrirLinha();

  Serial.println();
  Serial.println("Pronto. Comece pelo modo 1 (autoteste).");
  menu();
}

void loop() {
  if (!Serial.available()) return;

  String linha = Serial.readStringUntil('\n');
  linha.trim();
  if (linha.length() == 0) return;

  const char c = linha[0];
  // Resto da linha depois do primeiro espaco, como texto.
  String resto = "";
  for (size_t i = 1; i < linha.length(); i++) {
    if (i == 1 && linha[i] == ' ') continue;
    resto += linha[i];
  }
  const long numero = resto.length() ? atol(resto.c_str()) : 0;

  switch (c) {
    case '1': autoteste(); break;
    case '2': escutar(); break;
    case '3': procurar(numero > 0 && numero < 248 ? (uint8_t)numero : 16); break;
    case '4': {
      uint16_t ini = 0, fim = 255;
      if (resto.length()) {
        ini = (uint16_t)numero;
        const char* esp = strchr(resto.c_str(), ' ');
        fim = esp ? (uint16_t)atol(esp + 1) : (uint16_t)(ini + 255);
      }
      varrerRegistradores(ini, fim);
      break;
    }
    case '5': quadroCru(resto); break;
    case 'b': if (numero >= 1200) { baudAtual = (uint32_t)numero; abrirLinha(); } break;
    case 'p': if (numero >= 0 && numero < N_PARIDADES) { paridadeAtual = (uint8_t)numero; abrirLinha(); } break;
    case 'i': if (numero > 0 && numero < 248) idAtual = (uint8_t)numero; break;
    case 'f': if (numero == 3 || numero == 4) funcAtual = (uint8_t)numero; break;
    default: break;
  }
  menu();
}
