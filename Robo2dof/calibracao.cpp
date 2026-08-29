#include "calibracao.h"
#include "estado.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include "encoder.h"
#include <math.h>   // fabsf/lroundf das contas de curso

bool calibAtiva() { return estadoCalib != CAL_INATIVO; }

// Posicao dos contadores no instante em que o HOME foi gravado. Cancelar
// a calibracao depois disso precisa DESFAZER o zerarPosicoes(): senao os
// limites recuperados do NVS se referem ao zero antigo e passam a
// proteger a regiao errada, com erro igual a distancia entre os dois.
static long origemAntesDoZero1 = 0;
static long origemAntesDoZero2 = 0;
static bool origemFoiDeslocada = false;

uint8_t calibEixoAtivo() {
  switch (estadoCalib) {
    case CAL_J1_NEG: case CAL_J1_VOLTA_NEG:
    case CAL_J1_POS: case CAL_J1_VOLTA_POS: return 1;
    case CAL_J2_NEG: case CAL_J2_VOLTA_NEG:
    case CAL_J2_POS: case CAL_J2_VOLTA_POS: return 2;
    default: return 0;
  }
}

// ---------------------------------------------------------------------
void calibIniciar() {
  // O assistente pede que o operador leve o braco ate os limites: sem
  // torque nos drivers nao ha o que medir, so contador correndo solto.
  if (!servosLigados) {
    definirMensagem("Habilite os servos antes de calibrar");
    return;
  }

  soldaDesligar();
  pararSuave();
  jogZerar();

  origemFoiDeslocada = false;

  // Enquanto nao houver calibracao valida, a protecao de postura fica
  // desativada de proposito: e o operador que esta definindo os limites.
  J1.calibrada = false;
  J2.calibrada = false;

  modoAtual   = MODO_CALIBRANDO;
  estadoCalib = CAL_HOME;
  aplicarVelocidadeManual();
  definirMensagem("Calibracao: leve o braco ate a posicao de referencia");
}

void calibCancelar() {
  pararSuave();
  jogZerar();
  estadoCalib = CAL_INATIVO;
  modoAtual   = MODO_MANUAL;

  carregarConfiguracoes();   // restaura a calibracao anterior

  // Desfaz o zerarPosicoes() do CAL_HOME. Depois do zero, a posicao 0
  // corresponde ao ponto fisico que valia origemAntesDoZero: a posicao
  // atual na referencia antiga e origemAntesDoZero + posicao atual.
  if (origemFoiDeslocada) {
    if (J1.motor) J1.motor->setCurrentPosition(posicaoJ1() + origemAntesDoZero1);
    if (J2.motor) J2.motor->setCurrentPosition(posicaoJ2() + origemAntesDoZero2);
    origemFoiDeslocada = false;
    Serial.println("[CAL] Origem anterior restaurada.");
  }

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
  }
  origemFoiDeslocada = false;
  estadoCalib = CAL_INATIVO;
  if (modoAtual == MODO_CALIBRANDO) modoAtual = MODO_MANUAL;

  recalcularResolucao();
  salvarConfiguracoes();
  aplicarVelocidadeManual();
  aplicarAceleracao();

  Serial.println("[CAL] Calibracao apagada do NVS.");
  definirMensagem("Calibracao apagada. O jog esta livre; calibre antes de executar programa");
}

// ---------------------------------------------------------------------
static void voltarParaZero(Junta& j) {
  if (!j.motor) return;
  j.motor->setSpeedInHz(grausPorSegParaHz(j, velAuto));
  j.motor->moveTo(0);
}

// Sanidade do que foi medido. Se o operador percorreu uma etapa no
// sentido contrario (ou o pino DIR esta invertido), min e max saem
// trocados e o intervalo nao contem o zero - que e justamente onde o
// assistente deixa o braco. Sem esta checagem o robo trava assim que a
// calibracao termina.
static bool ajustarCurso(Junta& j, uint8_t numero) {
  if (j.passosMin > j.passosMax) {
    const long t = j.passosMin;
    j.passosMin = j.passosMax;
    j.passosMax = t;
    Serial.printf("[CAL] Junta %u: limites invertidos, corrigidos.\n",
                  (unsigned)numero);
  }
  if (j.passosMin > 0) j.passosMin = 0;
  if (j.passosMax < 0) j.passosMax = 0;

  // Em GRAUS, nao em passos. O criterio antigo (> 10 passos) aceitava
  // 0,4 grau na resolucao padrao - menos que os 2 x MARGEM_LIMITE_GRAUS
  // que posturaValida() desconta. O resultado era um intervalo util
  // negativo: nenhuma postura passava e os dois eixos travavam.
  if (j.passosPorGrau <= 0.0f) return false;
  const float curso = (float)(j.passosMax - j.passosMin) / j.passosPorGrau;
  return curso >= CURSO_MINIMO_GRAUS;
}

// ---------------------------------------------------------------------
// AFERICAO DA RESOLUCAO
//
// A calibracao mede o curso em PULSOS. Para virar graus, o firmware
// divide por passosPorGrau -- que vem de passosPorVolta x reducao, dois
// numeros DIGITADOS. Se qualquer um estiver errado, o braco de verdade
// fica numa posicao e o da tela em outra, proporcionalmente.
//
// O assistente acabou de varrer o curso inteiro da junta: e a maior
// base de medida que a maquina tem. Se o operador medir esse curso com
// um transferidor e informar quantos graus foram de fato, sai a
// resolucao real, sem precisar de encoder.
//
//     passosPorGrau = pulsos contados / graus medidos
//
// A reducao e reescrita para explicar essa resolucao com a engrenagem
// eletronica informada, para o painel de ajustes continuar coerente e o
// valor sobreviver a um recalculo.
// ---------------------------------------------------------------------
static bool aferirResolucao(Junta& j, float cursoRealGraus, uint8_t numero) {
  if (cursoRealGraus <= 0.0f) return false;          // nao aferir

  const long pulsos = j.passosMax - j.passosMin;
  if (pulsos < 10 || cursoRealGraus < CURSO_MINIMO_GRAUS) {
    Serial.printf("[CAL] Junta %u: afericao ignorada (%ld pulsos, %.1f graus).\n",
                  (unsigned)numero, pulsos, cursoRealGraus);
    return false;
  }

  const float antes = j.passosPorGrau;
  const float ppg   = (float)pulsos / cursoRealGraus;
  j.passosPorGrau = ppg;
  if (j.passosPorVolta > 0) {
    j.reducao = ppg * 360.0f / (float)j.passosPorVolta;
  }
  Serial.printf("[CAL] Junta %u aferida: %ld pulsos em %.2f graus -> "
                "%.3f pulsos/grau (era %.3f), reducao %.4f\n",
                (unsigned)numero, pulsos, cursoRealGraus, ppg, antes, j.reducao);
  return true;
}

static void concluir() {
  const bool ok1 = ajustarCurso(J1, 1);
  const bool ok2 = ajustarCurso(J2, 2);

  if (!ok1 || !ok2) {
    estadoCalib = CAL_INATIVO;
    modoAtual   = MODO_MANUAL;
    J1.calibrada = false;
    J2.calibrada = false;
    aplicarVelocidadeManual();
    aplicarAceleracao();
    definirMensagem("Calibracao descartada: junta %s com curso menor que %.0f graus. Refaca movendo ate os limites reais.",
                    !ok1 ? "1" : "2", CURSO_MINIMO_GRAUS);
    return;
  }

  J1.calibrada = true;
  J2.calibrada = true;
  recalcularResolucao();   // converte o curso medido para graus
  salvarConfiguracoes();

  estadoCalib = CAL_INATIVO;
  modoAtual   = MODO_MANUAL;
  aplicarVelocidadeManual();
  aplicarAceleracao();

  definirMensagem("Calibrado: J1 %.1f a %.1f, J2 %.1f a %.1f graus",
                  J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax);
}

// ---------------------------------------------------------------------
void calibConfirmar(float f1, float f2) {
  switch (estadoCalib) {
    case CAL_HOME:
      jogZerar();
      pararSuave();
      origemAntesDoZero1 = posicaoJ1();
      origemAntesDoZero2 = posicaoJ2();
      origemFoiDeslocada = true;
      zerarPosicoes();
      J1.passosMin = J1.passosMax = 0;
      J2.passosMin = J2.passosMax = 0;
      // Onde o braco REALMENTE esta, em graus. O contador zera aqui, mas
      // zero passo nao e zero grau: a cinematica chama de zero o braco
      // esticado apontando para +X. Sem este offset o desenho na tela sai
      // girado em relacao a maquina.
      J1.grausHome = f1;
      J2.grausHome = f2;
      recalcularResolucao();
      estadoCalib = CAL_J1_NEG;
      if (f1 != 0.0f || f2 != 0.0f) {
        definirMensagem("Referencia gravada em %.1f / %.1f graus. Leve a junta 1 ao limite negativo",
                        f1, f2);
      } else {
        definirMensagem("Referencia gravada. Leve a junta 1 ao limite negativo");
      }
      break;

    case CAL_J1_NEG:
      jogZerar();
      J1.passosMin = posicaoJ1();
      voltarParaZero(J1);
      estadoCalib = CAL_J1_VOLTA_NEG;
      break;

    case CAL_J1_POS:
      jogZerar();
      J1.passosMax = posicaoJ1();
      voltarParaZero(J1);
      estadoCalib = CAL_J1_VOLTA_POS;
      break;

    case CAL_J2_NEG:
      jogZerar();
      J2.passosMin = posicaoJ2();
      voltarParaZero(J2);
      estadoCalib = CAL_J2_VOLTA_NEG;
      break;

    case CAL_J2_POS:
      jogZerar();
      J2.passosMax = posicaoJ2();
      voltarParaZero(J2);
      estadoCalib = CAL_J2_VOLTA_POS;
      break;

    case CAL_CONCLUIDO: {
      // A partir daqui o zero novo e o oficial: nao ha mais o que desfazer.
      origemFoiDeslocada = false;
      // Aferir ANTES de concluir: concluir() valida o curso minimo e
      // converte para graus, e as duas coisas dependem da resolucao.
      const bool a1 = aferirResolucao(J1, f1, 1);
      const bool a2 = aferirResolucao(J2, f2, 2);
      if (a1 || a2) recalcularResolucao();
      concluir();
      break;
    }

    default:
      break;   // estados de retorno automatico ignoram o botao
  }
}

// ---------------------------------------------------------------------
void calibAtualizar() {
  if (modoAtual != MODO_CALIBRANDO) return;

  switch (estadoCalib) {
    case CAL_J1_VOLTA_NEG:
      if (J1.motor && !J1.motor->isRunning()) {
        aplicarVelocidadeManual();
        estadoCalib = CAL_J1_POS;
        definirMensagem("Leve a junta 1 ao limite positivo");
      }
      break;

    case CAL_J1_VOLTA_POS:
      if (J1.motor && !J1.motor->isRunning()) {
        aplicarVelocidadeManual();
        estadoCalib = CAL_J2_NEG;
        definirMensagem("Junta 1 pronta. Leve a junta 2 ao limite negativo");
      }
      break;

    case CAL_J2_VOLTA_NEG:
      if (J2.motor && !J2.motor->isRunning()) {
        aplicarVelocidadeManual();
        estadoCalib = CAL_J2_POS;
        definirMensagem("Leve a junta 2 ao limite positivo");
      }
      break;

    case CAL_J2_VOLTA_POS:
      if (J2.motor && !J2.motor->isRunning()) {
        aplicarVelocidadeManual();
        estadoCalib = CAL_CONCLUIDO;
        definirMensagem("Curso medido. Confira os valores e conclua");
      }
      break;

    default:
      break;
  }
}
