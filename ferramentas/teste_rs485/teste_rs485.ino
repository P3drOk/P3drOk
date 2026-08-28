// =====================================================================
//  Diagnostico de RS485 / Modbus RTU  --  Robo2dof
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

  // O autoteste passeia por todas as velocidades. Sem guardar o que
  // estava valendo, ele termina deixando a linha na ULTIMA combinacao da
  // varredura (115200 8O1), e o menu passa a mentir logo depois de dizer
  // "MODULO OK" -- foi exatamente o que apareceu no log da maquina.
  const bool     guardado   = ouvirEco;
  const uint32_t baudGuard  = baudAtual;
  const uint8_t  parGuard   = paridadeAtual;
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

  ouvirEco      = guardado;
  baudAtual     = baudGuard;
  paridadeAtual = parGuard;
  abrirLinha();                 // a linha volta a ser a que o operador escolheu

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
//  Varre velocidade x paridade x endereco e ANOTA todos os achados numa
//  tabela. Reporta qualquer sinal de vida:
//
//    - resposta valida        -> achou
//    - resposta de EXCECAO    -> ACHOU TAMBEM. O escravo respondeu
//                                "esse registrador nao existe", o que
//                                prova que ele esta la e falando.
//    - bytes com CRC ruim     -> a velocidade esta perto, a paridade
//                                provavelmente errada
// =====================================================================
struct Achado {
  uint32_t baud;
  uint8_t  paridade;
  uint8_t  id;
  uint8_t  func;
  bool     excecao;
  uint8_t  codigo;      // codigo da excecao, quando for o caso
};
static const uint8_t MAX_ACHADOS = 12;
static Achado achados[MAX_ACHADOS];
static uint8_t nAchados = 0;

static const char* textoExcecao(uint8_t c) {
  switch (c) {
    case 1: return "funcao ilegal (tente f 3 ou f 4)";
    case 2: return "endereco de registrador ilegal";
    case 3: return "valor ilegal (quantidade de registradores)";
    case 4: return "falha no escravo";
    case 6: return "escravo ocupado";
    default: return "codigo nao padronizado";
  }
}

static void procurar(uint8_t idMax) {
  Serial.println();
  Serial.println("== PROCURANDO O DRIVER ==");
  Serial.print("Enderecos 1 a "); Serial.print((int)idMax);
  Serial.println(", todas as velocidades e paridades. Pode demorar.");
  Serial.println();

  nAchados = 0;
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
            const bool exc = (r[1] & 0x80) != 0;
            if (exc) {
              Serial.print("  *** ACHOU (excecao ");
              Serial.print((int)r[2]);
              Serial.print(": "); Serial.print(textoExcecao(r[2]));
              Serial.println(") ***");
            } else {
              Serial.println("  *** ACHOU, resposta valida ***");
            }
            if (nAchados < MAX_ACHADOS) {
              achados[nAchados].baud     = BAUDS[b];
              achados[nAchados].paridade = p;
              achados[nAchados].id       = id;
              achados[nAchados].func     = f;
              achados[nAchados].excecao  = exc;
              achados[nAchados].codigo   = exc ? r[2] : 0;
              nAchados++;
            }
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
  if (!nAchados) {
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
    Serial.println();
    return;
  }

  Serial.println("=== TUDO QUE RESPONDEU ===");
  for (uint8_t k = 0; k < nAchados; k++) {
    Serial.print("  ");
    Serial.print(achados[k].baud);
    Serial.print(" ");
    Serial.print(PARIDADES[achados[k].paridade].nome);
    Serial.print("  id "); Serial.print((int)achados[k].id);
    Serial.print("  funcao "); Serial.print((int)achados[k].func);
    if (achados[k].excecao) {
      Serial.print("   excecao ");
      Serial.print((int)achados[k].codigo);
      Serial.print(" (");
      Serial.print(textoExcecao(achados[k].codigo));
      Serial.print(")");
    } else {
      Serial.print("   resposta valida");
    }
    Serial.println();
  }

  // Prefere uma resposta VALIDA; se so houve excecao, fica com a
  // primeira -- excecao tambem prova que o escravo esta la.
  uint8_t escolha = 0;
  for (uint8_t k = 0; k < nAchados; k++) {
    if (!achados[k].excecao) { escolha = k; break; }
  }
  baudAtual     = achados[escolha].baud;
  paridadeAtual = achados[escolha].paridade;
  idAtual       = achados[escolha].id;
  funcAtual     = achados[escolha].func;
  abrirLinha();

  Serial.println();
  Serial.print("Configuracao adotada: ");
  Serial.print(baudAtual); Serial.print(" ");
  Serial.print(PARIDADES[paridadeAtual].nome);
  Serial.print("  id "); Serial.print((int)idAtual);
  Serial.print("  funcao "); Serial.println((int)funcAtual);
  Serial.println("Agora use o modo 7 para cacar o registrador do encoder.");
  Serial.println();
}

// =====================================================================
//  MODO 4 - Varrer registradores
//
//  Le faixa por faixa e mostra o que tem dentro. Excecao NAO e escondida:
//  se o driver recusa uma faixa inteira, isso aparece -- tela vazia nao
//  ensina nada a ninguem.
//
//  Ler oito de uma vez e mais rapido, mas muito driver so aceita ler
//  registradores que existem de verdade e recusa o bloco inteiro se um
//  deles nao existir. Por isso, quando o bloco e recusado, ele volta e
//  tenta um por um.
// =====================================================================

// Le UM registrador. Devolve: 0 = silencio, 1 = valor em 'valor',
// 2 = excecao (codigo em 'codigo').
static uint8_t lerUm(uint16_t endereco, uint16_t& valor, uint8_t& codigo) {
  uint8_t r[16];
  const size_t n = lerRegistradores(idAtual, funcAtual, endereco, 1,
                                    r, sizeof(r), 150);
  if (!n || !crcConfere(r, n)) return 0;
  if (r[1] & 0x80) { codigo = (n > 2) ? r[2] : 0; return 2; }
  if (n < 7 || r[2] < 2) return 0;
  valor = (uint16_t)((r[3] << 8) | r[4]);
  return 1;
}

static void varrerRegistradores(uint16_t inicio, uint16_t fim) {
  Serial.println();
  Serial.println("== VARREDURA DE REGISTRADORES ==");
  Serial.print("id "); Serial.print((int)idAtual);
  Serial.print("  funcao "); Serial.print((int)funcAtual);
  Serial.print("  "); Serial.print(baudAtual);
  Serial.print(" "); Serial.println(PARIDADES[paridadeAtual].nome);
  Serial.println();

  abrirLinha();
  uint16_t lidos = 0, excecoes = 0, mudos = 0;
  uint8_t ultimaExcecao = 0;

  for (uint32_t a = inicio; a <= fim; a++) {
    uint16_t v = 0;
    uint8_t  c = 0;
    const uint8_t st = lerUm((uint16_t)a, v, c);
    if (st == 0) { mudos++; continue; }
    if (st == 2) { excecoes++; ultimaExcecao = c; continue; }

    Serial.print("  0x");
    if (a < 0x1000) Serial.print("0");
    if (a < 0x100)  Serial.print("0");
    if (a < 0x10)   Serial.print("0");
    Serial.print((unsigned)a, HEX);
    Serial.print(" (");
    Serial.print((unsigned)a);
    Serial.print(")  =  ");
    Serial.println(v);
    lidos++;
  }

  Serial.println();
  Serial.print("Resumo: "); Serial.print(lidos); Serial.print(" leram, ");
  Serial.print(excecoes); Serial.print(" recusados, ");
  Serial.print(mudos); Serial.println(" sem resposta.");

  if (!lidos && excecoes) {
    Serial.print("Faixa inteira recusada com excecao ");
    Serial.print((int)ultimaExcecao);
    Serial.print(": "); Serial.println(textoExcecao(ultimaExcecao));
    Serial.println("O driver ESTA respondendo -- so nao ha registrador aqui.");
    Serial.println("Tente outra faixa (4 4096 4351, 4 8192 8447) ou a outra");
    Serial.println("funcao (f 3 / f 4).");
  } else if (!lidos && mudos) {
    Serial.println("Ninguem respondeu. Confira a configuracao com o modo 3.");
  } else if (lidos) {
    Serial.println("Agora use o modo 7 para achar qual deles e o encoder.");
  }
  Serial.println();
}

// =====================================================================
//  MODO 6 - Monitorar um registrador ao vivo
//
//  Le o registrador e o seguinte, sem parar, e imprime. Gire o eixo a
//  mao e veja o numero andar. E o jeito mais direto de confirmar que
//  achou a posicao.
// =====================================================================
static void monitorar(uint16_t endereco) {
  Serial.println();
  Serial.println("== MONITOR ==");
  Serial.print("Registradores "); Serial.print(endereco);
  Serial.print(" e "); Serial.print(endereco + 1);
  Serial.println(". GIRE O EIXO A MAO. Qualquer tecla encerra.");
  Serial.println();
  Serial.println("  reg      +1       32b alto|baixo   32b baixo|alto   unid/s");
  Serial.println("  --------------------------------------------------------");

  abrirLinha();
  bool     primeiro = true;
  int32_t  antesAB = 0, antesBA = 0;
  uint32_t antesMs = millis();
  uint8_t  falhas  = 0;

  while (!Serial.available()) {
    uint16_t v0 = 0, v1 = 0;
    uint8_t  c = 0;
    const uint8_t a = lerUm(endereco, v0, c);
    const uint8_t b = lerUm((uint16_t)(endereco + 1), v1, c);

    if (a != 1) {
      // Nao inunda a tela: avisa uma vez a cada dez falhas seguidas.
      if ((falhas++ % 10) == 0) Serial.println("  (sem leitura)");
      delay(200);
      continue;
    }
    falhas = 0;

    // As duas montagens possiveis de 32 bits. Muito driver Modbus manda a
    // palavra BAIXA primeiro; olhar so uma das duas faz a posicao parecer
    // que pula sem sentido. A que crescer suave ao girar o eixo e a certa.
    const int32_t altoBaixo = (int32_t)(((uint32_t)v0 << 16) | v1);
    const int32_t baixoAlto = (int32_t)(((uint32_t)v1 << 16) | v0);

    const uint32_t agora = millis();
    const float dt = (agora - antesMs) / 1000.0f;
    if (primeiro) { antesAB = altoBaixo; antesBA = baixoAlto; primeiro = false; }

    // Velocidade pela montagem que variou MENOS em modulo: a montagem
    // errada da saltos enormes, a certa varia suave.
    const int32_t dAB = altoBaixo - antesAB;
    const int32_t dBA = baixoAlto - antesBA;
    const int32_t dBom = (labs(dAB) <= labs(dBA)) ? dAB : dBA;
    const float vel = (dt > 0.001f) ? (dBom / dt) : 0.0f;

    Serial.print("  ");   Serial.print(v0);
    Serial.print("\t");   Serial.print((b == 1) ? (int)v1 : -1);
    Serial.print("\t");   Serial.print(altoBaixo);
    Serial.print("\t");   Serial.print(baixoAlto);
    Serial.print("\t");   Serial.println(vel);

    antesAB = altoBaixo; antesBA = baixoAlto; antesMs = agora;
    delay(100);
  }
  while (Serial.available()) Serial.read();
  Serial.println();
  Serial.println("A coluna que crescer SUAVE ao girar o eixo e a montagem");
  Serial.println("certa. A outra da saltos de dezenas de milhares -- e o");
  Serial.println("sinal classico de palavra alta e baixa trocadas.");
  Serial.println("(fim do monitor)");
  Serial.println();
}

// =====================================================================
//  MODO 7 - Cacar o encoder
//
//  Le a faixa, espera voce girar o eixo, le de novo e mostra SO os
//  registradores que mudaram. E o que responde a pergunta "qual deles e
//  a posicao" sem voce comparar duas telas a olho.
// =====================================================================
static const uint16_t MAX_CACA = 512;
static uint16_t cacaAntes[MAX_CACA];
// Leitura do MEIO, depois do primeiro giro. Sem ela nao da para saber se
// um registrador anda SEMPRE PARA O MESMO LADO quando o eixo anda sempre
// para o mesmo lado -- e e so isso que separa posicao de ruido.
static uint16_t cacaMeio[MAX_CACA];
static uint8_t  cacaValido[MAX_CACA];

static void cacar(uint16_t inicio, uint16_t fim) {
  if ((uint32_t)(fim - inicio) + 1 > MAX_CACA) fim = (uint16_t)(inicio + MAX_CACA - 1);

  Serial.println();
  Serial.println("== CACAR O ENCODER ==");
  Serial.print("Faixa "); Serial.print(inicio);
  Serial.print(" a "); Serial.println(fim);
  Serial.println("Lendo o estado inicial...");
  abrirLinha();

  uint16_t n = 0;
  for (uint32_t a = inicio; a <= fim; a++) {
    uint16_t v = 0; uint8_t c = 0;
    cacaValido[a - inicio] = (lerUm((uint16_t)a, v, c) == 1) ? 1 : 0;
    cacaAntes[a - inicio]  = v;
    if (cacaValido[a - inicio]) n++;
  }
  Serial.print(n); Serial.println(" registradores lidos.");
  if (!n) {
    Serial.println("Nenhum registrador nesta faixa. Tente outra, ou f 3 / f 4.");
    Serial.println();
    return;
  }

  Serial.println();
  Serial.println(">>> GIRE O EIXO A MAO, num SO sentido, e aperte uma tecla.");
  while (!Serial.available()) { }
  while (Serial.available()) Serial.read();

  // Segunda leitura, guardada para o crivo do sentido mais abaixo.
  for (uint32_t a = inicio; a <= fim; a++) {
    const uint16_t i = (uint16_t)(a - inicio);
    if (!cacaValido[i]) continue;
    uint16_t v = 0; uint8_t c = 0;
    if (lerUm((uint16_t)a, v, c) == 1) cacaMeio[i] = v;
    else cacaValido[i] = 0;
  }

  Serial.println();
  Serial.println(">>> Agora gire MAIS, no MESMO sentido, e aperte de novo.");
  Serial.println("    (e este segundo giro que separa a posicao do ruido)");
  while (!Serial.available()) { }
  while (Serial.available()) Serial.read();

  Serial.println();
  Serial.println("Comparando...");
  Serial.println();

  uint16_t mudaram = 0;
  for (uint32_t a = inicio; a <= fim; a++) {
    if (!cacaValido[a - inicio]) continue;
    uint16_t v = 0; uint8_t c = 0;
    if (lerUm((uint16_t)a, v, c) != 1) continue;
    const uint16_t antes = cacaAntes[a - inicio];
    if (v == antes) continue;

    const int32_t d = (int32_t)v - (int32_t)antes;
    Serial.print("  0x");
    if (a < 0x1000) Serial.print("0");
    if (a < 0x100)  Serial.print("0");
    if (a < 0x10)   Serial.print("0");
    Serial.print((unsigned)a, HEX);
    Serial.print(" (");
    Serial.print((unsigned)a);
    Serial.print(")   ");
    Serial.print(antes);
    Serial.print("  ->  ");
    Serial.print(v);
    Serial.print("   (variou ");
    Serial.print(d);
    Serial.print(")");
    // Com sinal, quando faz diferenca. 65535 nao e um salto de +65535:
    // e -1. Sem esta coluna, erro de seguimento parece encoder.
    if (antes >= 32768 || v >= 32768) {
      Serial.print("   com sinal: ");
      Serial.print((int)(int16_t)antes);
      Serial.print(" -> ");
      Serial.print((int)(int16_t)v);
    }
    Serial.println();
    mudaram++;
  }

  Serial.println();
  if (!mudaram) {
    Serial.println("Nada mudou nesta faixa. Ou o encoder nao esta aqui, ou");
    Serial.println("o eixo nao girou o bastante. Tente outra faixa ou gire mais.");
    Serial.println();
    return;
  }

  Serial.print(mudaram); Serial.println(" registrador(es) mudaram.");
  Serial.println();

  // -------------------------------------------------------------------
  // Qual deles e a POSICAO?
  //
  // Listar o que mudou nao basta: um driver mexe em varias coisas quando
  // o eixo gira -- erro de seguimento, velocidade, contador de voltas.
  // No log da maquina apareceram cinco de uma vez, e dois deles (92/93)
  // andavam JUNTOS, o que confundia.
  //
  // O par da posicao tem uma assinatura que nenhum outro tem: montado
  // como 32 bits com a palavra BAIXA primeiro, o numero anda uma
  // quantidade que faz sentido para uma girada de mao. Montado ao
  // contrario, ele salta centenas de milhoes. E so procurar o par r/r+1
  // em que a montagem certa e MUITO mais mansa que a errada.
  // -------------------------------------------------------------------
  //
  // O CRIVO: girei sempre para o MESMO LADO, entao a posicao tem de ter
  // andado sempre para o mesmo lado. Duas vezes.
  //
  // Isto e o que separa a posicao do resto, e nenhum criterio de
  // "quem variou mais" faz isso. Na maquina do operador o registrador
  // 94 foi de 0 para 65535 -- que parece o maior salto de todos, mas com
  // sinal e -1, e o vizinho 95 pulava para os dois lados. Era erro de
  // seguimento, e a ferramenta apontou ele. Erro de seguimento OSCILA;
  // posicao nao.
  //
  uint16_t melhorReg = 0;
  int64_t  melhorAndou = 0;
  bool     achouPar = false;

  for (uint32_t a = inicio; a + 1 <= fim; a++) {
    const uint16_t i = (uint16_t)(a - inicio);
    if (!cacaValido[i] || !cacaValido[i + 1]) continue;
    uint16_t lo2 = 0, hi2 = 0; uint8_t c = 0;
    if (lerUm((uint16_t)a, lo2, c) != 1) continue;
    if (lerUm((uint16_t)(a + 1), hi2, c) != 1) continue;

    const uint32_t v0 = ((uint32_t)cacaAntes[i + 1] << 16) | cacaAntes[i];
    const uint32_t v1 = ((uint32_t)cacaMeio [i + 1] << 16) | cacaMeio [i];
    const uint32_t v2 = ((uint32_t)hi2 << 16) | lo2;

    // Diferenca em complemento de dois: a volta da palavra baixa sai
    // certa sozinha, sem caso especial.
    const int32_t d1 = (int32_t)(v1 - v0);
    const int32_t d2 = (int32_t)(v2 - v1);
    if (d1 == 0 || d2 == 0) continue;                  // parou: nao e posicao
    if ((d1 > 0) != (d2 > 0)) continue;                // voltou: e ruido

    // Montagem invertida tem de ser MUITO pior, senao o par nao e de 32
    // bits de verdade -- e so dois registradores vizinhos quaisquer.
    const uint32_t t0 = ((uint32_t)cacaAntes[i] << 16) | cacaAntes[i + 1];
    const uint32_t t2 = ((uint32_t)lo2 << 16) | hi2;
    const int64_t  dCerto  = (int64_t)d1 + d2;
    const int64_t  dTorto  = (int64_t)(int32_t)(t2 - t0);
    const int64_t  mCerto  = dCerto < 0 ? -dCerto : dCerto;
    const int64_t  mTorto  = dTorto < 0 ? -dTorto : dTorto;
    if (mCerto * 8 > mTorto) continue;

    if (!achouPar || mCerto > melhorAndou) {
      achouPar = true; melhorReg = (uint16_t)a; melhorAndou = mCerto;
    }
  }
  const uint32_t melhorBaixa = (uint32_t)melhorAndou;

  if (achouPar) {
    Serial.print("=== O PAR DA POSICAO E ");
    Serial.print(melhorReg); Serial.print(" (baixa) e ");
    Serial.print(melhorReg + 1); Serial.println(" (alta) ===");
    Serial.println("Montado com a palavra BAIXA primeiro, o numero andou");
    Serial.print(melhorBaixa);
    Serial.println(" contagens, SEMPRE PARA O MESMO LADO nos dois giros.");
    Serial.println("E isso que separa posicao de ruido: erro de seguimento e");
    Serial.println("velocidade oscilam, voltam para perto de zero. Posicao");
    Serial.println("nao volta. Montado ao contrario, saltaria centenas de");
    Serial.println("milhoes, que nao e giro nenhum.");
    Serial.println();
    Serial.print("No painel do robo: funcao "); Serial.print((int)funcAtual);
    Serial.print(", registrador "); Serial.print(melhorReg);
    Serial.println(", 32 bits, palavra baixa primeiro.");
    Serial.println();
    Serial.print("Confira ao vivo com:  6 "); Serial.println(melhorReg);
    Serial.print("E meca as contagens por volta com:  8 "); Serial.println(melhorReg);
  } else {
    Serial.println("Mudou coisa, mas nenhum PAR andou para o MESMO LADO nos");
    Serial.println("dois giros. Isso e a assinatura de erro de seguimento e");
    Serial.println("velocidade: eles oscilam e voltam para perto de zero.");
    Serial.println("Repita girando SEMPRE PARA O MESMO LADO, e bastante --");
    Serial.println("a posicao nao volta, e ai ela aparece sozinha.");
  }
  Serial.println();
}

// =====================================================================
//  MODO 8 - Medir as CONTAGENS POR VOLTA
//
//  E o unico numero do encoder que nao da para descobrir olhando: sem
//  ele a leitura chega em contagens cruas e nao vira grau nenhum. O
//  catalogo diz 17 bits (131072), mas catalogo nao e medicao -- e a
//  engrenagem eletronica do driver pode estar dividindo.
//
//  Medir e simples: marca, o operador da UMA VOLTA COMPLETA no eixo do
//  motor, marca de novo. A diferenca E o numero.
// =====================================================================
static bool lerPar32(uint16_t base, int32_t& valor) {
  uint16_t lo = 0, hi = 0; uint8_t c = 0;
  if (lerUm(base, lo, c) != 1) return false;
  if (lerUm((uint16_t)(base + 1), hi, c) != 1) return false;
  valor = (int32_t)(((uint32_t)hi << 16) | lo);
  return true;
}

static void medirContagensPorVolta(uint16_t base) {
  Serial.println();
  Serial.println("== CONTAGENS POR VOLTA ==");
  Serial.print("Par 32 bits em "); Serial.print(base);
  Serial.print(" / "); Serial.print(base + 1);
  Serial.println(", palavra baixa primeiro.");
  Serial.println();
  abrirLinha();

  int32_t antes = 0;
  if (!lerPar32(base, antes)) {
    Serial.println("Nao consegui ler esse par. Confira o endereco e a funcao.");
    Serial.println();
    return;
  }
  Serial.print("Marcado em "); Serial.println((long)antes);
  Serial.println();
  Serial.println(">>> De UMA VOLTA COMPLETA no eixo do MOTOR, no sentido");
  Serial.println(">>> que faz o numero CRESCER, e aperte qualquer tecla.");
  Serial.println(">>> (marque o eixo com fita para saber onde fechou)");
  while (!Serial.available()) { }
  while (Serial.available()) Serial.read();

  int32_t depois = 0;
  if (!lerPar32(base, depois)) {
    Serial.println("Perdi a leitura no meio. Repita.");
    Serial.println();
    return;
  }

  const int32_t d = depois - antes;
  Serial.print("Agora em "); Serial.print((long)depois);
  Serial.print("  ->  uma volta = "); Serial.print((long)(d < 0 ? -d : d));
  Serial.println(" contagens.");
  Serial.println();

  const int32_t m = d < 0 ? -d : d;
  if (m < 100) {
    Serial.println("Muito pouco para ser uma volta. O eixo girou mesmo?");
  } else {
    // Encaixa no valor redondo mais proximo, se estiver perto: e mais
    // provavel que a volta a mao tenha ficado torta do que o encoder ter
    // um numero quebrado.
    const int32_t COMUNS[] = {1024, 2500, 4096, 10000, 16384, 32768, 65536, 131072, 262144, 524288};
    for (int32_t c : COMUNS) {
      const int32_t erro = m > c ? m - c : c - m;
      if ((int64_t)erro * 100 < (int64_t)c * 3) {     // dentro de 3%
        Serial.print("Isso e praticamente "); Serial.print((long)c);
        Serial.println(" -- use esse numero redondo, a volta a mao nunca");
        Serial.println("fecha exata.");
        break;
      }
    }
    Serial.println();
    Serial.println("Ponha em \"contagens por volta\" da junta, no painel.");
    Serial.print("Se voce girou o eixo do motor e a junta tem reducao, o");
    Serial.println(" painel ja desconta a reducao sozinho.");
  }
  Serial.println();
}

// =====================================================================
//  MODO 9 - Gravar a posicao em CSV
//
//  Despeja "ms,contagem" continuamente. Serve para colar numa planilha e
//  VER a curva: e assim que se enxerga passo perdido, folga e ruido, que
//  numero na tela nao mostra.
// =====================================================================
static void gravarCsv(uint16_t base) {
  Serial.println();
  Serial.println("== CSV DA POSICAO ==");
  Serial.print("Par "); Serial.print(base); Serial.print("/");
  Serial.print(base + 1);
  Serial.println(". Qualquer tecla encerra. Copie tudo para uma planilha.");
  Serial.println();
  Serial.println("ms,contagem,delta");
  abrirLinha();

  const uint32_t t0 = millis();
  int32_t anterior = 0;
  bool    primeiro = true;

  while (!Serial.available()) {
    int32_t v = 0;
    if (!lerPar32(base, v)) { delay(50); continue; }
    Serial.print(millis() - t0); Serial.print(",");
    Serial.print((long)v); Serial.print(",");
    Serial.println(primeiro ? 0L : (long)(v - anterior));
    anterior = v; primeiro = false;
    delay(50);
  }
  while (Serial.available()) Serial.read();
  Serial.println("(fim do CSV)");
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
// =====================================================================
//  ESCREVER -- funcao 06 (um registrador) e 16 (bloco de um)
//
//  ATE AQUI ESTE SKETCH SO LIA. Escrever e outra categoria de risco: ler
//  no registrador errado da um numero errado na tela, escrever no
//  registrador errado num servo drive pode trocar a engrenagem
//  eletronica, o modo de controle, o sentido do eixo ou o limite de
//  torque -- e o eixo pode sair andando.
//
//  ESTE SKETCH E AVULSO DE PROPOSITO. Ele nao faz parte do firmware da
//  maquina: o firmware le e so le. Enquanto nao se souber COM CERTEZA
//  qual registrador e o que, escrever e experimento de bancada, com o
//  motor desacoplado da mecanica, e nao funcao de painel.
//
//  A resposta da funcao 06 e o ECO do proprio pedido; a da 16 e endereco
//  + quantidade. Conferir o eco separa "o driver aceitou" de "alguem
//  respondeu qualquer coisa". E depois se RELE, porque driver que
//  responde "aceitei" e guarda outra coisa existe.
// =====================================================================
static uint8_t escreverUm(uint16_t endereco, uint16_t valor, bool usar16,
                          uint8_t& codigo) {
  uint8_t q[11];
  size_t  nq;
  q[0] = idAtual;
  q[2] = (uint8_t)(endereco >> 8);
  q[3] = (uint8_t)(endereco & 0xFF);
  if (!usar16) {
    q[1] = 6;
    q[4] = (uint8_t)(valor >> 8);
    q[5] = (uint8_t)(valor & 0xFF);
    nq = 6;
  } else {
    q[1] = 16;
    q[4] = 0; q[5] = 1;     // quantidade = 1 registrador
    q[6] = 2;               // 2 bytes de dado
    q[7] = (uint8_t)(valor >> 8);
    q[8] = (uint8_t)(valor & 0xFF);
    nq = 9;
  }
  const uint16_t c = crc16(q, nq);
  q[nq]     = (uint8_t)(c & 0xFF);
  q[nq + 1] = (uint8_t)(c >> 8);

  Serial.print("  enviado: "); hexLinha(q, nq + 2);

  uint8_t r[16];
  const size_t n = trocar(q, nq + 2, r, sizeof(r), 300);
  Serial.print("  voltou : ");
  if (n) hexLinha(r, n); else Serial.println("(silencio)");

  if (!n) return 0;
  if (!crcConfere(r, n)) { Serial.println("  CRC nao bate."); return 0; }
  if (r[1] & 0x80) { codigo = (n > 2) ? r[2] : 0; return 2; }
  if (n < 8 || r[1] != q[1] || r[2] != q[2] || r[3] != q[3]) {
    Serial.println("  resposta fora do formato esperado.");
    return 0;
  }
  return 1;
}

// Escreve e CONFERE relendo. Devolve true so quando o valor entrou.
static bool escreverEConferir(uint16_t endereco, uint16_t valor, bool usar16) {
  uint8_t cod = 0;
  const uint8_t r = escreverUm(endereco, valor, usar16, cod);
  if (r == 2) {
    Serial.print("  EXCECAO ao escrever: "); Serial.println(textoExcecao(cod));
    Serial.println("  Pode ser registrador que nao existe, escrita bloqueada,");
    Serial.println("  ou funcao errada -- ha driver que so aceita a 16 (use W).");
    return false;
  }
  if (r != 1) { Serial.println("  o driver NAO aceitou."); return false; }

  delay(30);
  uint16_t lido = 0; uint8_t cod2 = 0;
  const uint8_t rl = lerUm(endereco, lido, cod2);
  if (rl != 1) {
    Serial.println("  escreveu, mas a releitura falhou: confira no painel.");
    return false;
  }
  Serial.print("  releitura: reg "); Serial.print(endereco);
  Serial.print(" vale "); Serial.println(lido);
  if (lido == valor) { Serial.println("  CONFERE: o valor entrou."); return true; }
  Serial.print("  NAO CONFERE: pedi "); Serial.print(valor);
  Serial.print(" e voltou "); Serial.println(lido);
  Serial.println("  Pode ser registrador de so-leitura, ou valor fora da faixa.");
  return false;
}

static void escrever(const String& resto, bool usar16) {
  int esp = resto.indexOf(' ');
  if (esp <= 0) {
    Serial.println("uso: w <registrador> <valor>   (W usa a funcao 16)");
    return;
  }
  const long endereco = atol(resto.substring(0, esp).c_str());
  const long valor    = atol(resto.substring(esp + 1).c_str());
  if (endereco < 0 || endereco > 65535 || valor < 0 || valor > 65535) {
    Serial.println("registrador e valor vao de 0 a 65535.");
    return;
  }
  Serial.println();
  Serial.println("== ESCREVER ==");
  Serial.print("driver "); Serial.print((int)idAtual);
  Serial.print(", registrador "); Serial.print(endereco);
  Serial.print(", valor "); Serial.print(valor);
  Serial.print(", funcao "); Serial.println(usar16 ? 16 : 6);
  Serial.println("MOTOR DESACOPLADO DA MECANICA? Digite S para confirmar.");
  while (!Serial.available()) { }
  const String conf = Serial.readStringUntil('\n');
  if (conf.length() == 0 || (conf[0] != 'S' && conf[0] != 's')) {
    Serial.println("cancelado.");
    return;
  }
  escreverEConferir((uint16_t)endereco, (uint16_t)valor, usar16);
}

// =====================================================================
//  MODO d -- achar um parametro SEM ESCREVER
//
//  Tira uma foto da faixa, voce muda o parametro NO PAINEL do driver, e
//  a segunda foto mostra o que mudou. E o mesmo raciocinio do modo 7
//  (cacar o encoder movendo o eixo), so que o que se mexe e um parametro
//  em vez do braco. Zero escrita: se o endereco estiver errado, o pior
//  que acontece e nao achar nada.
// =====================================================================
static uint16_t fotoValor[512];
static bool     fotoTem[512];
static uint16_t fotoIni = 0, fotoFim = 0;
static bool     fotoTirada = false;

static uint16_t tirarFoto(uint16_t ini, uint16_t fim,
                          uint16_t* destino, bool* tem) {
  uint16_t achados = 0;
  for (uint16_t a = ini; a <= fim; a++) {
    const uint16_t i = (uint16_t)(a - ini);
    uint16_t v = 0; uint8_t cod = 0;
    tem[i] = (lerUm(a, v, cod) == 1);
    if (tem[i]) { destino[i] = v; achados++; }
    if (a == 0xFFFF) break;
  }
  return achados;
}

static void diferenca(uint16_t ini, uint16_t fim, bool comparar) {
  if (fim < ini) { const uint16_t t = ini; ini = fim; fim = t; }
  if ((uint32_t)(fim - ini) >= 512) fim = (uint16_t)(ini + 511);

  Serial.println();
  if (!comparar) {
    Serial.println("== FOTO DA FAIXA (nada e escrito) ==");
    fotoIni = ini; fotoFim = fim;
    const uint16_t n = tirarFoto(ini, fim, fotoValor, fotoTem);
    fotoTirada = (n > 0);
    if (!fotoTirada) {
      Serial.println("nenhum registrador respondeu. Rode o modo 3 antes.");
      return;
    }
    Serial.print("foto de "); Serial.print(n);
    Serial.print(" registradores entre "); Serial.print(ini);
    Serial.print(" e "); Serial.println(fim);
    Serial.println();
    Serial.println("AGORA va ao PAINEL do driver e mude o parametro que voce");
    Serial.println("quer achar (P098, por exemplo). Volte e digite:  d2");
    Serial.println("Deixe o eixo PARADO: a posicao muda sozinha e apareceria");
    Serial.println("na lista junto com o parametro.");
    return;
  }

  if (!fotoTirada) { Serial.println("tire a foto primeiro (d)."); return; }
  Serial.println("== O QUE MUDOU DESDE A FOTO ==");
  uint16_t agora[512];
  bool     tem[512];
  tirarFoto(fotoIni, fotoFim, agora, tem);

  uint16_t mudaram = 0;
  for (uint16_t a = fotoIni; a <= fotoFim; a++) {
    const uint16_t i = (uint16_t)(a - fotoIni);
    if (!fotoTem[i] || !tem[i] || agora[i] == fotoValor[i]) continue;
    mudaram++;
    Serial.print("  reg "); Serial.print(a);
    Serial.print(" : "); Serial.print(fotoValor[i]);
    Serial.print(" -> "); Serial.print(agora[i]);
    Serial.print("   (0x"); Serial.print(fotoValor[i], HEX);
    Serial.print(" -> 0x"); Serial.print(agora[i], HEX);
    Serial.println(")");
    if (a == 0xFFFF) break;
  }
  Serial.println();
  if (mudaram == 0) {
    Serial.println("Nada mudou. O parametro nao esta nesta faixa, ou a");
    Serial.println("mudanca ainda nao foi confirmada no painel do driver.");
  } else if (mudaram == 1) {
    Serial.println("UM registrador so mudou: e esse o endereco do parametro.");
  } else {
    Serial.print(mudaram);
    Serial.println(" mudaram. Se o eixo se mexeu, o par da posicao entrou");
    Serial.println("na conta -- repita com o eixo parado.");
  }
}

// =====================================================================
//  MODO s -- testar o SON (habilita) por RS485, com o motor desacoplado
//
//  O QUE ISTO NAO E: nao e um jeito de habilitar o motor em operacao. No
//  Robo2dof o habilita e um FIO -- GPIO 23, por optoacoplador, no SON dos
//  dois drivers. Fio de SON rompido desabilita o motor; fio de RS485
//  rompido nao desabilita nada, deixa o eixo como estava. Um e falha
//  segura, o outro nao, e por isso o firmware nao escreve o habilita.
//
//  O QUE ISTO E: a bancada onde se descobre SE o driver aceita mexer no
//  habilita por Modbus, em qual registrador e com quais valores -- antes
//  de decidir qualquer coisa sobre o firmware.
//
//  MOTOR DESACOPLADO DA MECANICA. Se o registrador estiver certo, o eixo
//  vai ENERGIZAR. Se estiver errado, pode fazer outra coisa qualquer.
// =====================================================================
static void testarSon(uint16_t reg, uint16_t vLiga, uint16_t vDesliga,
                      bool usar16) {
  Serial.println();
  Serial.println("== TESTE DE SON (habilita) ==");
  Serial.print("driver "); Serial.print((int)idAtual);
  Serial.print(", registrador "); Serial.print(reg);
  Serial.print(", habilita="); Serial.print(vLiga);
  Serial.print(", desabilita="); Serial.println(vDesliga);
  Serial.println();
  Serial.println("O EIXO PODE ENERGIZAR E GIRAR.");
  Serial.println("Motor desacoplado da mecanica e area livre?");
  Serial.println("Digite S para confirmar, qualquer outra coisa cancela.");
  while (!Serial.available()) { }
  const String conf = Serial.readStringUntil('\n');
  if (conf.length() == 0 || (conf[0] != 'S' && conf[0] != 's')) {
    Serial.println("cancelado.");
    return;
  }

  uint16_t antes = 0; uint8_t cod = 0;
  if (lerUm(reg, antes, cod) == 1) {
    Serial.print("valor atual do registrador: "); Serial.println(antes);
  } else {
    Serial.println("nao consegui ler o registrador antes de escrever.");
    Serial.println("Continuo mesmo assim -- mas sem valor antigo para voltar.");
  }

  Serial.println();
  Serial.println("-- HABILITANDO --");
  const bool ligou = escreverEConferir(reg, vLiga, usar16);
  Serial.println("Encoste no eixo: ele deve estar TRAVADO (com torque).");
  Serial.println("Aperte qualquer tecla para desabilitar.");
  while (Serial.available()) Serial.read();
  while (!Serial.available()) { }
  while (Serial.available()) Serial.read();

  Serial.println();
  Serial.println("-- DESABILITANDO --");
  const bool desligou = escreverEConferir(reg, vDesliga, usar16);
  Serial.println("Encoste no eixo: ele deve girar SOLTO.");

  Serial.println();
  Serial.println("== RESULTADO ==");
  if (ligou && desligou) {
    Serial.print("O registrador "); Serial.print(reg);
    Serial.println(" aceita escrita e a releitura confere nos dois sentidos.");
    Serial.println("Se o eixo travou e soltou junto, e o SON. Anote:");
    Serial.print("  reg="); Serial.print(reg);
    Serial.print("  habilita="); Serial.print(vLiga);
    Serial.print("  desabilita="); Serial.print(vDesliga);
    Serial.print("  funcao="); Serial.println(usar16 ? 16 : 6);
    Serial.println("Se o eixo NAO mudou, o registrador nao e o SON -- e");
    Serial.println("voce acabou de escrever noutro parametro. Confira o");
    Serial.println("painel do driver antes de seguir.");
  } else {
    Serial.println("A escrita nao confirmou. Nada garante que algo mudou");
    Serial.println("no driver -- e nada garante que nao mudou. Confira o");
    Serial.println("painel antes de repetir.");
  }
}

static void menu() {
  Serial.println();
  Serial.println("=====================================================");
  Serial.println(" DIAGNOSTICO RS485 / MODBUS RTU -- Robo2dof");
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
  Serial.println(" 6 100          monitorar um registrador ao vivo");
  Serial.println(" 7              cacar o encoder na faixa 0..255");
  Serial.println(" 7 4096 4351    cacar na faixa que voce quiser");
  Serial.println(" 8 90           medir contagens por volta no par 90/91");
  Serial.println(" 9 90           gravar a posicao em CSV (para planilha)");
  Serial.println();
  Serial.println(" -- acham e mexem em PARAMETRO (nao em posicao) --");
  Serial.println(" d              foto da faixa 0..255 (nada e escrito)");
  Serial.println(" d 4096 4351    foto da faixa que voce quiser");
  Serial.println(" d2             comparar: mostra o que mudou desde a foto");
  Serial.println(" w 98 1         ESCREVER valor 1 no registrador 98 (funcao 06)");
  Serial.println(" W 98 1         o mesmo pela funcao 16");
  Serial.println(" s 98           testar SON: habilita, voce confere, desabilita");
  Serial.println(" s 98 1 0       o mesmo dizendo os valores de habilita/desabilita");
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
    case 'd': {
      if (resto.length() && resto[0] == '2') { diferenca(0, 0, true); break; }
      uint16_t ini = 0, fim = 255;
      if (resto.length()) {
        const int esp = resto.indexOf(' ');
        if (esp > 0) {
          ini = (uint16_t)atol(resto.substring(0, esp).c_str());
          fim = (uint16_t)atol(resto.substring(esp + 1).c_str());
        } else {
          ini = (uint16_t)numero;
          fim = (uint16_t)(ini + 255);
        }
      }
      diferenca(ini, fim, false);
      break;
    }
    case 'w': escrever(resto, false); break;
    case 'W': escrever(resto, true);  break;
    case 's': {
      if (!resto.length()) { Serial.println("uso: s <registrador> [liga] [desliga]"); break; }
      uint16_t reg = 0, vL = 1, vD = 0;
      int p1 = resto.indexOf(' ');
      reg = (uint16_t)atol((p1 > 0 ? resto.substring(0, p1) : resto).c_str());
      if (p1 > 0) {
        const String r2 = resto.substring(p1 + 1);
        const int p2 = r2.indexOf(' ');
        vL = (uint16_t)atol((p2 > 0 ? r2.substring(0, p2) : r2).c_str());
        if (p2 > 0) vD = (uint16_t)atol(r2.substring(p2 + 1).c_str());
      }
      if (vL == vD) { Serial.println("habilita e desabilita nao podem ser iguais."); break; }
      testarSon(reg, vL, vD, false);
      break;
    }
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
    case '6': monitorar((uint16_t)numero); break;
    case '7': {
      uint16_t ini = 0, fim = 255;
      if (resto.length()) {
        ini = (uint16_t)numero;
        const char* esp = strchr(resto.c_str(), ' ');
        fim = esp ? (uint16_t)atol(esp + 1) : (uint16_t)(ini + 255);
      }
      cacar(ini, fim);
      break;
    }
    case '8': medirContagensPorVolta((uint16_t)numero); break;
    case '9': gravarCsv((uint16_t)numero); break;
    case 'b': if (numero >= 1200) { baudAtual = (uint32_t)numero; abrirLinha(); } break;
    case 'p': if (numero >= 0 && numero < N_PARIDADES) { paridadeAtual = (uint8_t)numero; abrirLinha(); } break;
    case 'i': if (numero > 0 && numero < 248) idAtual = (uint8_t)numero; break;
    case 'f': if (numero == 3 || numero == 4) funcAtual = (uint8_t)numero; break;
    default: break;
  }
  menu();
}
