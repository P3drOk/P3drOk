#include "calibracao.h"
#include "estado.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include "encoder.h"
#include <math.h>   // fabsf/lroundf das contas de curso

bool calibAtiva() { return estadoCalib != CAL_INATIVO; }

// =====================================================================
//  CALIBRAR SAO QUATRO MARCAS
//
//  Limite positivo do eixo 1, limite negativo do eixo 1, e o mesmo no
//  eixo 2. Nada digitado. O operador pode chegar em cada limite como
//  preferir: com torque, no jog; ou com os motores soltos, empurrando o
//  braco com a mao -- nos dois casos o que se grava e onde a junta ESTA.
//
//  Do que foi marcado sai:
//    - o CURSO de cada junta, em passos;
//    - o ZERO, que passa a ser o MEIO do curso. E a unica escolha que
//      nao pede numero nenhum, e a que deixa a area util centrada;
//    - a ESCALA DO ENCODER em contagens por grau, quando ele leu as duas
//      marcas. Ela sai de graca: entre as duas marcas ha um tanto de
//      contagens e um tanto de graus, e a divisao e a escala.
//
//  O que NAO sai daqui, e continua declarado: a REDUCAO do redutor. Ela
//  e mecanica, esta escrita no que o operador comprou, e com um sensor
//  so antes do redutor nenhuma medida a revela.
// =====================================================================
static long    marcaP[2] = {0, 0}, marcaN[2] = {0, 0};
static bool    temP[2]   = {false, false}, temN[2] = {false, false};
static int32_t encP[2]   = {0, 0}, encN[2] = {0, 0};
static bool    encPok[2] = {false, false}, encNok[2] = {false, false};

uint8_t calibEixoAtivo() {
  switch (estadoCalib) {
    case CAL_J1_POS: case CAL_J1_NEG: return 1;
    case CAL_J2_POS: case CAL_J2_NEG: return 2;
    default: return 0;
  }
}

// A primeira etapa e a unica em que nada foi medido ainda: e ali, e so
// ali, que trocar o sentido de um eixo nao contradiz uma marca ja feita.
bool calibNaPrimeiraEtapa() { return estadoCalib == CAL_J1_POS; }

// ---------------------------------------------------------------------
void calibIniciar() {
  // Nao se exige mais torque para comecar.
  //
  // A calibracao passou a ser "leve o eixo ate o batente e marque", e a
  // maneira mais segura de chegar num batente e com o motor SOLTO,
  // empurrando com a mao -- que era justamente o que a exigencia
  // antiga proibia. Com torque tambem funciona: o jog leva ate la.
  soldaDesligar();
  pararSuave();
  jogZerar();

  for (uint8_t i = 0; i < 2; i++) {
    temP[i] = temN[i] = false;
    encPok[i] = encNok[i] = false;
  }

  // Enquanto nao houver calibracao nova, a protecao de curso fica
  // desativada de proposito: e o operador que esta definindo os limites,
  // e proteger com os limites velhos o impediria de chegar nos novos.
  J1.calibrada = false;
  J2.calibrada = false;

  modoAtual   = MODO_CALIBRANDO;
  estadoCalib = CAL_J1_POS;
  aplicarVelocidadeManual();
  definirMensagem("Leve a junta 1 ate o limite POSITIVO e marque");
}

void calibCancelar() {
  pararSuave();
  jogZerar();
  estadoCalib = CAL_INATIVO;
  modoAtual   = MODO_MANUAL;

  carregarConfiguracoes();   // restaura a calibracao anterior

  // Nao ha mais o que desfazer na contagem: o zero so e deslocado no
  // fim, quando as quatro marcas ja existem. Cancelar no meio nao mexeu
  // em origem nenhuma.
  aplicarVelocidadeManual();
  aplicarAceleracao();
  definirMensagem("Calibracao cancelada");
}

// Marcas da afericao avulsa. Vivem no core 1, como todo o resto daqui.
static long  marcaPassos[2] = {0, 0};
static bool  marcaFeita[2]  = {false, false};

// Contagem do encoder no instante da marca. Guardada junto com os passos
// para que aferirPelosEncoder() possa comparar as duas coisas.
static int32_t marcaEncoder[2] = {0, 0};
static bool    marcaEncoderBoa[2] = {false, false};

void aferirMarcar(uint8_t junta) {
  if (junta != 1 && junta != 2) return;
  const uint8_t i = junta - 1;
  marcaPassos[i] = (junta == 1) ? posicaoJ1() : posicaoJ2();
  marcaFeita[i]  = true;

  // Se o encoder estiver lendo, a marca guarda a contagem dele tambem --
  // e ai da para aferir sem transferidor nenhum.
  const LeituraEncoder L = encoderLer(junta);
  marcaEncoderBoa[i] = L.valido && L.idadeMs <= ENC_IDADE_MAX_MS;
  marcaEncoder[i]    = L.bruto;

  if (marcaEncoderBoa[i])
    definirMensagem("Junta %u marcada. Mova o eixo bastante e afira -- "
                    "com transferidor, ou pelo encoder", (unsigned)junta);
  else
    definirMensagem("Junta %u marcada. Mova o eixo, meca com transferidor e informe os graus",
                    (unsigned)junta);
}

// ---------------------------------------------------------------------
// Ensina a escala do encoder pela propria maquina. Ver calibracao.h.
// ---------------------------------------------------------------------
bool ensinarEscalaEncoder(uint8_t junta, float grausAndados) {
  if (junta != 1 && junta != 2) return false;
  const uint8_t i = junta - 1;

  if (!marcaFeita[i] || !marcaEncoderBoa[i]) {
    definirMensagem("Marque o inicio da junta %u com o encoder lendo, "
                    "antes de ensinar a escala", (unsigned)junta);
    return false;
  }
  if (motoresEmMovimento()) {
    definirMensagem("Espere o braco parar para ensinar a escala");
    return false;
  }
  // Movimento curto mede mais ruido de leitura que engrenagem. Cinco
  // graus ja poem o erro de uma contagem tres ordens de grandeza abaixo
  // do que se esta medindo.
  if (fabsf(grausAndados) < 5.0f) {
    definirMensagem("Mova pelo menos 5 graus: %.1f e curto demais para medir",
                    (double)grausAndados);
    return false;
  }

  const LeituraEncoder L = encoderLer(junta);
  if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) {
    definirMensagem("Sem leitura do encoder na junta %u agora", (unsigned)junta);
    return false;
  }

  // Complemento de dois: a volta do contador de 32 bits sai certa sozinha.
  const int32_t dCont = (int32_t)((uint32_t)L.bruto - (uint32_t)marcaEncoder[i]);
  if (labs((long)dCont) < 50) {
    definirMensagem("A contagem mal mudou (%ld) -- o encoder esta lendo "
                    "este eixo?", (long)dCont);
    return false;
  }

  // O SINAL fica: encoder que conta para tras enquanto a junta avanca da
  // escala negativa, e o angulo sai certo sem chave de inversao.
  const float escala = (float)dCont / grausAndados;

  configEncoder.contagensPorGrau[i] = escala;
  encoderPendente = configEncoder;
  encoderReconfigurar();
  salvarConfiguracoes();

  definirMensagem("Junta %u: %.1f contagens por grau (%ld contagens em %.1f graus). "
                  "O angulo na tela passa a ser o do braco",
                  (unsigned)junta, (double)escala, (long)dCont, (double)grausAndados);
  return true;
}

// ---------------------------------------------------------------------
bool aferirPelosEncoder(uint8_t junta) {
  if (junta != 1 && junta != 2) return false;
  const uint8_t i = junta - 1;
  Junta& j = (junta == 1) ? J1 : J2;

  if (!marcaFeita[i]) {
    definirMensagem("Marque o inicio antes de aferir a junta %u", (unsigned)junta);
    return false;
  }
  if (!marcaEncoderBoa[i]) {
    definirMensagem("A junta %u nao tinha leitura do encoder quando foi marcada",
                    (unsigned)junta);
    return false;
  }
  if (motoresEmMovimento()) {
    definirMensagem("Espere o braco parar para aferir");
    return false;
  }

  const LeituraEncoder L = encoderLer(junta);
  if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) {
    definirMensagem("Sem leitura do encoder na junta %u agora", (unsigned)junta);
    return false;
  }

  const float cv = configEncoder.contagensPorVolta[i];
  if (cv < 1.0f) {
    definirMensagem("Informe as contagens por volta do encoder antes de aferir");
    return false;
  }

  const long  passos = labs(aferirPassosDesde(junta));
  // Complemento de dois: a volta do contador de 32 bits sai certa sozinha.
  const int32_t dCont = (int32_t)((uint32_t)L.bruto - (uint32_t)marcaEncoder[i]);
  const float voltas  = fabsf((float)dCont) / cv;

  // Medida curta mede mais o ruido que a engrenagem. Um quarto de volta
  // do motor ja da margem de sobra para o erro de leitura sumir.
  if (voltas < 0.25f || passos < 100) {
    definirMensagem("Movimento curto demais: %.2f volta do motor em %ld passos. Mova mais",
                    (double)voltas, passos);
    return false;
  }

  const float ppv = (float)passos / voltas;
  // Engrenagem eletronica fora de qualquer faixa plausivel quer dizer que
  // alguma outra coisa esta errada -- registrador do encoder, contagens
  // por volta, acoplamento. Gravar isso estragaria a maquina em silencio.
  if (ppv < 100.0f || ppv > 500000.0f) {
    definirMensagem("Resultado implausivel (%.0f passos/volta): confira o encoder",
                    (double)ppv);
    return false;
  }

  const uint32_t antes = j.passosPorVolta;
  j.passosPorVolta = (uint32_t)lroundf(ppv);
  // A reducao continua sendo a declarada: o encoder conta ANTES do
  // redutor e nao tem como enxerga-la.
  j.passosPorGrau = (float)j.passosPorVolta * j.reducao / 360.0f;

  recalcularResolucao();
  salvarConfiguracoes();
  aplicarVelocidadeManual();
  aplicarAceleracao();
  marcaFeita[i] = false;

  definirMensagem("Junta %u: %lu passos/volta pelo encoder (era %lu), reducao %.3f mantida",
                  (unsigned)junta, (unsigned long)j.passosPorVolta,
                  (unsigned long)antes, (double)j.reducao);
  return true;
}

// ---------------------------------------------------------------------
// Voltas do MOTOR desde a marca, pelo encoder. Este e o numero que o
// encoder da de graca e com precisao -- e o que falta ao lado da junta e
// uma referencia angular, uma so. Ver a nota longa em calibracao.h.
// ---------------------------------------------------------------------
float aferirVoltasDesde(uint8_t junta) {
  if (junta != 1 && junta != 2) return 0.0f;
  const uint8_t i = junta - 1;
  if (!marcaFeita[i] || !marcaEncoderBoa[i]) return 0.0f;

  const LeituraEncoder L = encoderLer(junta);
  if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) return 0.0f;

  const float cv = configEncoder.contagensPorVolta[i];
  if (cv < 1.0f) return 0.0f;

  // Complemento de dois: a volta do contador de 32 bits sai certa sozinha.
  const int32_t d = (int32_t)((uint32_t)L.bruto - (uint32_t)marcaEncoder[i]);
  return (float)d / cv;
}

bool aferirTemMarcaBoa(uint8_t junta) {
  if (junta != 1 && junta != 2) return false;
  return marcaFeita[junta - 1] && marcaEncoderBoa[junta - 1];
}

// ---------------------------------------------------------------------
bool aferirReducaoPeloEncoder(uint8_t junta, float grausReais) {
  if (junta != 1 && junta != 2) return false;
  const uint8_t i = junta - 1;
  Junta& j = (junta == 1) ? J1 : J2;

  if (!marcaFeita[i]) {
    definirMensagem("Marque o inicio antes de aferir a junta %u", (unsigned)junta);
    return false;
  }
  if (!marcaEncoderBoa[i]) {
    definirMensagem("A junta %u nao tinha leitura do encoder quando foi marcada",
                    (unsigned)junta);
    return false;
  }
  if (motoresEmMovimento()) {
    definirMensagem("Espere o braco parar para aferir");
    return false;
  }
  if (configEncoder.contagensPorVolta[i] < 1.0f) {
    definirMensagem("Informe as contagens por volta do encoder antes de aferir");
    return false;
  }

  const float voltas = fabsf(aferirVoltasDesde(junta));
  const float g      = fabsf(grausReais);

  // Um quarto de volta do motor e cinco graus de junta: abaixo disso a
  // medida diz mais sobre o erro de leitura do que sobre a engrenagem.
  if (voltas < 0.25f) {
    definirMensagem("Junta %u: so %.2f volta do motor. Mova bem mais antes de aferir",
                    (unsigned)junta, (double)voltas);
    return false;
  }
  if (g < 5.0f) {
    definirMensagem("Angulo de referencia curto demais (%.1f graus): use 90 graus "
                    "de esquadro, ou o curso inteiro", (double)g);
    return false;
  }

  const float red = voltas * 360.0f / g;
  // Redutor abaixo de 1 seria multiplicador, e acima de 1000 nao existe
  // nesta classe de maquina. Numero fora disso quer dizer que a
  // referencia informada nao bate com o que o eixo andou -- gravar seria
  // estragar a maquina em silencio.
  if (red < 0.5f || red > 1000.0f) {
    definirMensagem("Resultado implausivel: %.2f volta do motor para %.1f graus "
                    "dariam reducao %.1f", (double)voltas, (double)g, (double)red);
    return false;
  }

  const float antes = j.reducao;
  j.reducao = red;
  j.passosPorGrau = (float)j.passosPorVolta * j.reducao / 360.0f;

  recalcularResolucao();
  salvarConfiguracoes();
  aplicarVelocidadeManual();
  aplicarAceleracao();
  marcaFeita[i] = false;

  definirMensagem("Junta %u: reducao %.3f:1 medida pelo encoder (era %.3f). "
                  "%.3f volta do motor para %.1f graus de junta",
                  (unsigned)junta, (double)red, (double)antes,
                  (double)voltas, (double)g);
  return true;
}

long aferirPassosDesde(uint8_t junta) {
  if (junta != 1 && junta != 2) return 0;
  const uint8_t i = junta - 1;
  if (!marcaFeita[i]) return 0;
  return ((junta == 1) ? posicaoJ1() : posicaoJ2()) - marcaPassos[i];
}

bool aferirAplicar(uint8_t junta, float grausReais) {
  if (junta != 1 && junta != 2) return false;
  const uint8_t i = junta - 1;
  Junta& j = (junta == 1) ? J1 : J2;

  if (!marcaFeita[i]) {
    definirMensagem("Marque o inicio antes de aferir a junta %u", (unsigned)junta);
    return false;
  }
  const long pulsos = labs(aferirPassosDesde(junta));
  const float g = fabsf(grausReais);
  if (g < 1.0f || pulsos < 10) {
    definirMensagem("Medida curta demais na junta %u: %ld pulsos em %.2f graus",
                    (unsigned)junta, pulsos, g);
    return false;
  }

  const float antes = j.passosPorGrau;
  const float ppg   = (float)pulsos / g;
  j.passosPorGrau = ppg;
  if (j.passosPorVolta > 0) j.reducao = ppg * 360.0f / (float)j.passosPorVolta;

  // Os limites em graus vem de passosMin/Max divididos pela resolucao:
  // com ela corrigida, eles se ajustam sozinhos.
  recalcularResolucao();
  salvarConfiguracoes();
  aplicarVelocidadeManual();
  aplicarAceleracao();
  marcaFeita[i] = false;

  Serial.printf("[AFERIR] Junta %u: %ld pulsos em %.2f graus -> %.3f pulsos/grau "
                "(era %.3f), reducao %.4f\n",
                (unsigned)junta, pulsos, g, ppg, antes, j.reducao);
  definirMensagem("Junta %u aferida: %.2f pulsos por grau, reducao %.3f : 1",
                  (unsigned)junta, ppg, j.reducao);
  return true;
}

// ---------------------------------------------------------------------
void calibReferenciar() {
  pararSuave();
  jogZerar();
  zerarPosicoes();     // a contagem volta a zero, que le grausHome
  definirMensagem("Referenciado: junta 1 em %.1f, junta 2 em %.1f graus",
                  passosParaGraus(J1, 0), passosParaGraus(J2, 0));
}

void calibApagar() {
  pararSuave();
  jogZerar();
  soldaDesligar();

  Junta* js[2] = { &J1, &J2 };
  for (uint8_t i = 0; i < 2; i++) {
    js[i]->calibrada = false;
    js[i]->passosMin = 0;
    js[i]->passosMax = 0;
    js[i]->grausHome = 0.0f;
    temP[i] = temN[i] = false;
    encPok[i] = encNok[i] = false;
  }
  estadoCalib = CAL_INATIVO;
  if (modoAtual == MODO_CALIBRANDO) modoAtual = MODO_MANUAL;

  recalcularResolucao();
  salvarConfiguracoes();
  aplicarVelocidadeManual();
  aplicarAceleracao();

  Serial.println("[CAL] Calibracao apagada do NVS.");
  // Sem limites a maquina NAO para de funcionar: ela so deixa de ter
  // protecao de curso. Dizer "calibre antes de executar" era a versao
  // antiga, em que quase tudo ficava trancado.
  definirMensagem("Limites apagados. A maquina segue operando, sem protecao de curso");
}

// ---------------------------------------------------------------------
// Grava uma marca: onde a junta ESTA agora, em passos, e o que o encoder
// estava lendo no mesmo instante.
// ---------------------------------------------------------------------
static void gravarMarca(uint8_t k, bool positivo) {
  const uint8_t i = k - 1;
  const long pos = (k == 1) ? posicaoJ1() : posicaoJ2();
  const LeituraEncoder L = encoderLer(k);
  const bool lendo = L.valido && L.idadeMs <= ENC_IDADE_MAX_MS;

  if (positivo) { marcaP[i] = pos; temP[i] = true; encP[i] = L.bruto; encPok[i] = lendo; }
  else          { marcaN[i] = pos; temN[i] = true; encN[i] = L.bruto; encNok[i] = lendo; }
}

// ---------------------------------------------------------------------
// Fecha uma junta a partir das duas marcas dela.
//
// O ZERO passa a ser o MEIO do curso. Antes o zero era um ponto que o
// operador declarava com um angulo digitado; aqui ele sai da propria
// medida, e sai centrado -- os limites viram -curso/2 e +curso/2, e
// nenhuma postura nasce encostada num deles.
// ---------------------------------------------------------------------
static bool fecharJunta(Junta& j, uint8_t k) {
  const uint8_t i = k - 1;
  if (!temP[i] || !temN[i]) return false;
  if (j.passosPorGrau <= 0.0f) return false;

  long lo = marcaN[i], hi = marcaP[i];
  if (lo > hi) { const long t = lo; lo = hi; hi = t; }

  const float curso = (float)(hi - lo) / j.passosPorGrau;
  if (curso < CURSO_MINIMO_GRAUS) return false;

  const long meio = lo + (hi - lo) / 2;
  const long agora = (k == 1) ? posicaoJ1() : posicaoJ2();
  ajustarContagem(j, agora - meio);
  j.passosMin = lo - meio;
  j.passosMax = hi - meio;
  j.grausHome = 0.0f;
  j.calibrada = true;

  // A ESCALA DO ENCODER SAI DE GRACA.
  //
  // Entre as duas marcas ha um tanto de contagens (o encoder viu) e um
  // tanto de graus (a contagem de passos, dividida pela resolucao). A
  // divisao das duas E a escala, com sinal: encoder que conta para tras
  // enquanto a junta avanca da escala negativa, e o angulo sai certo sem
  // chave de inversao nenhuma.
  //
  // Isto era uma tela separada, com marca, movimento e um numero
  // digitado. Agora e uma consequencia de ter calibrado.
  if (encPok[i] && encNok[i]) {
    // Complemento de dois: a volta do contador de 32 bits sai sozinha.
    const int32_t dCont = (int32_t)((uint32_t)encP[i] - (uint32_t)encN[i]);
    const float dGraus  = (float)(marcaP[i] - marcaN[i]) / j.passosPorGrau;
    if (fabsf(dGraus) >= CURSO_MINIMO_GRAUS && labs((long)dCont) >= 50) {
      configEncoder.contagensPorGrau[i] = (float)dCont / dGraus;
      Serial.printf("[CAL] Junta %u: escala do encoder medida em %.2f "
                    "contagens por grau (%ld contagens em %.1f graus)\n",
                    (unsigned)k, (double)configEncoder.contagensPorGrau[i],
                    (long)dCont, (double)dGraus);
    }
  }
  return true;
}

static void concluir() {
  const bool ok1 = fecharJunta(J1, 1);
  const bool ok2 = fecharJunta(J2, 2);

  if (!ok1 && !ok2) {
    estadoCalib = CAL_INATIVO;
    modoAtual   = MODO_MANUAL;
    aplicarVelocidadeManual();
    aplicarAceleracao();
    definirMensagem("Nada medido: o curso ficou menor que %.0f graus nas duas "
                    "juntas. Leve o eixo ate os batentes de verdade",
                    (double)CURSO_MINIMO_GRAUS);
    return;
  }

  recalcularResolucao();   // converte o curso medido para graus

  // O encoder passa a ler o mesmo angulo que a contagem: a referencia
  // dele e reancorada na posicao atual, ja com a escala nova.
  encoderPendente = configEncoder;
  encoderReconfigurar();
  encoderDefinirZero(1, passosParaGraus(J1, posicaoJ1()));
  encoderDefinirZero(2, passosParaGraus(J2, posicaoJ2()));

  salvarConfiguracoes();

  estadoCalib = CAL_INATIVO;
  modoAtual   = MODO_MANUAL;
  aplicarVelocidadeManual();
  aplicarAceleracao();

  if (ok1 && ok2)
    definirMensagem("Calibrado: J1 %.1f a %.1f, J2 %.1f a %.1f graus. "
                    "O zero e o meio do curso",
                    J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax);
  else
    definirMensagem("Junta %u calibrada: %.1f a %.1f graus. A outra nao teve "
                    "curso suficiente e ficou sem limites",
                    ok1 ? 1u : 2u,
                    ok1 ? J1.grausMin : J2.grausMin,
                    ok1 ? J1.grausMax : J2.grausMax);
}

// ---------------------------------------------------------------------
// Os dois numeros continuam na assinatura porque a fila de comandos os
// carrega, mas a calibracao nao usa mais nenhum: ela nao pergunta nada.
void calibConfirmar(float, float) {
  switch (estadoCalib) {
    case CAL_J1_POS:
      jogZerar(); pararSuave();
      gravarMarca(1, true);
      estadoCalib = CAL_J1_NEG;
      definirMensagem("Limite positivo da junta 1 marcado. Agora o NEGATIVO");
      break;

    case CAL_J1_NEG:
      jogZerar(); pararSuave();
      gravarMarca(1, false);
      estadoCalib = CAL_J2_POS;
      definirMensagem("Junta 1 medida. Leve a junta 2 ate o limite POSITIVO");
      break;

    case CAL_J2_POS:
      jogZerar(); pararSuave();
      gravarMarca(2, true);
      estadoCalib = CAL_J2_NEG;
      definirMensagem("Limite positivo da junta 2 marcado. Agora o NEGATIVO");
      break;

    case CAL_J2_NEG:
      jogZerar(); pararSuave();
      gravarMarca(2, false);
      estadoCalib = CAL_CONCLUIDO;
      concluir();
      break;

    default:
      break;
  }
}

// ---------------------------------------------------------------------
// CALIBRAR COM OS MOTORES SOLTOS
//
// O jeito mais seguro de chegar num batente e com o motor sem torque,
// empurrando o braco com a mao. So que ai o gerador de pulso nao anda, e
// a contagem -- que e o que a marca grava -- ficaria parada nos quatro
// limites.
//
// Entao, enquanto a calibracao esta aberta, cada junta SEM torque tem a
// contagem puxada pelo encoder. Nao e o seguimento de eixo solto de
// sempre: aquele escreve uma posicao ABSOLUTA e por isso exige o zero
// absoluto ja ensinado. Aqui basta o DELTA, e delta nao precisa de
// origem nenhuma.
//
// A conversao de contagem para passo dispensa a reducao: os dois numeros
// sao por volta do MOTOR, e o redutor cancela na divisao.
//
//     passos por contagem = passosPorVolta / contagensPorVolta
//
// Com a escala ja ensinada, usa-se ela, que e medida em vez de digitada.
// ---------------------------------------------------------------------
static int32_t ultimoBruto[2] = {0, 0};
static bool    temUltimo[2]   = {false, false};

static void puxarPelaMao(uint8_t k) {
  const uint8_t i = k - 1;
  Junta& j = (k == 1) ? J1 : J2;

  if (j.habilitado) { temUltimo[i] = false; return; }
  if (configEncoder.reg[i] == 0) { temUltimo[i] = false; return; }

  const LeituraEncoder L = encoderLer(k);
  if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) { temUltimo[i] = false; return; }

  if (!temUltimo[i]) { ultimoBruto[i] = L.bruto; temUltimo[i] = true; return; }

  // Complemento de dois: a volta do contador de 32 bits sai sozinha.
  const int32_t d = (int32_t)((uint32_t)L.bruto - (uint32_t)ultimoBruto[i]);
  if (d == 0) return;
  ultimoBruto[i] = L.bruto;

  float passosPorContagem;
  const float cpg = configEncoder.contagensPorGrau[i];
  if (j.passosPorGrau > 0.0f && (cpg > 0.0001f || cpg < -0.0001f)) {
    passosPorContagem = j.passosPorGrau / cpg;
  } else {
    const float cv = configEncoder.contagensPorVolta[i];
    if (cv < 1.0f || j.passosPorVolta == 0) return;
    passosPorContagem = (float)j.passosPorVolta / cv;
  }

  const long passos = lroundf((float)d * passosPorContagem);
  if (passos == 0) return;
  const long agora = (k == 1) ? posicaoJ1() : posicaoJ2();
  ajustarContagem(j, agora + passos);
}

// Nao ha mais etapa automatica: entre uma marca e a outra quem move o
// braco e o operador -- com o jog, ou com a mao.
void calibAtualizar() {
  if (modoAtual != MODO_CALIBRANDO) { temUltimo[0] = temUltimo[1] = false; return; }
  if (motoresEmMovimento()) return;
  puxarPelaMao(1);
  puxarPelaMao(2);
}
