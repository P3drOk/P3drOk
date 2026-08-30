#include "programa.h"
#include "estado.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static Ponto   pontos[MAX_PONTOS];
static uint8_t nPontos = 0;

enum FaseProg : uint8_t {
  FASE_PARADO,
  FASE_INDO_INICIO,
  FASE_ABRINDO_ARCO,
  FASE_DESLOCANDO,   // trecho sem solda: interpolacao nas juntas (rapido)
  FASE_SOLDANDO,     // trecho com solda: RETA no espaco cartesiano
  FASE_FECHANDO_ARCO,
  // Retomada de um cordao pausado: reabre o arco e volta para a reta sem
  // refazer prepararReta(), que zeraria o adiantamento do relogio.
  FASE_RETOMANDO
};

static FaseProg fase       = FASE_PARADO;
static bool     ensaio     = false;
static uint8_t  idx        = 0;
static uint32_t marcaTempo = 0;

// ---------------------------------------------------------------------
// Pausa. Guarda em que fase o programa estava e, quando era um cordao,
// a que fracao dele -- e o que permite retomar de onde parou em vez de
// refazer o trecho por cima do que ja foi soldado.
// ---------------------------------------------------------------------
static bool     pausado      = false;
// Marcado no instante em que o programa chega ao ultimo ponto. E o que
// separa "peca pronta" de "alguem apertou parar no meio" -- os dois
// passam por progParar(), e sem esta marca o contador de producao nao
// teria como distinguir.
static bool     concluiu     = false;
static FaseProg faseGuardada = FASE_PARADO;
static float    fracaoSeg    = 0.0f;

// ---------------------------------------------------------------------
// Desfazer, um nivel. Guarda o programa inteiro antes de cada alteracao.
// ---------------------------------------------------------------------
static Ponto   desfPontos[MAX_PONTOS];
static uint8_t desfN        = 0;
static bool    desfTem      = false;
static char    desfOque[40] = "";

static void guardarParaDesfazer(const char* oque) {
  memcpy(desfPontos, pontos, sizeof(Ponto) * nPontos);
  desfN   = nPontos;
  desfTem = true;
  snprintf(desfOque, sizeof(desfOque), "%s", oque ? oque : "alteracao");
}

// Estado da reta em curso
static float    xa, ya, xb, yb;
static uint32_t tSegIni, tSegTotal;
static uint32_t velSeg1, velSeg2;
static float    refT1, refT2;
static bool     ramoSeg;        // ramo do cotovelo travado para o trecho

// ---------------------------------------------------------------------
uint8_t      progQuantidade()  { return nPontos; }
const Ponto* progLista()       { return pontos; }
bool         progRodando()     { return fase != FASE_PARADO; }
bool         progEmEnsaio()    { return ensaio; }
uint8_t      progIndiceAtual() { return idx; }
#ifdef ROBO2DOF_TESTE
uint8_t      progFaseTeste()   { return (uint8_t)fase; }
#endif

uint8_t progProgresso() {
  if (fase == FASE_PARADO || nPontos < 2) return 0;
  const uint16_t p = (uint16_t)idx * 100 / (nPontos - 1);
  return p > 100 ? 100 : (uint8_t)p;
}

// ---------------------------------------------------------------------
static void pontoParaXY(const Ponto& p, float& x, float& y) {
  const float a1 = passosParaGraus(J1, p.p1);
  const float a2 = passosParaGraus(J2, p.p2);
  float xc, yc;
  cinematicaDireta(a1, a2, xc, yc, x, y);
}

// ---------------------------------------------------------------------
bool progAdicionarPonto(long p1, long p2, const char** motivo) {
  if (nPontos >= MAX_PONTOS) {
    if (motivo) *motivo = "limite de pontos do programa atingido";
    return false;
  }
  Violacao v;
  if (!posturaValidaDet(passosParaGraus(J1, p1), passosParaGraus(J2, p2), v)) {
    static char aviso[128];
    char det[112];
    violacaoTexto(v, det, sizeof(det));
    snprintf(aviso, sizeof(aviso), "%s", det);
    if (motivo) *motivo = aviso;
    return false;
  }
  guardarParaDesfazer("ponto gravado");
  pontos[nPontos].p1 = (int32_t)p1;
  pontos[nPontos].p2 = (int32_t)p2;
  pontos[nPontos].soldaAteProximo = 0;
  nPontos++;
  definirMensagem("Ponto %u gravado", (unsigned)nPontos);
  return true;
}

bool progCarregarDe(const Ponto* origem, uint8_t n, const char** motivo) {
  if (fase != FASE_PARADO) {
    if (motivo) *motivo = "pare o programa antes de carregar outro";
    return false;
  }
  if (!origem || n < 2) {
    if (motivo) *motivo = "arquivo com menos de 2 pontos";
    return false;
  }
  if (n > MAX_PONTOS) {
    if (motivo) *motivo = "arquivo com pontos demais";
    return false;
  }
  // Valida tudo ANTES de escrever: nada de programa carregado pela metade.
  // (o guarda de desfazer vem depois da validacao, la embaixo: guardar
  // aqui perderia o desfazer anterior por causa de um arquivo recusado)
  for (uint8_t i = 0; i < n; i++) {
    Violacao v;
    if (!posturaValidaDet(passosParaGraus(J1, origem[i].p1),
                          passosParaGraus(J2, origem[i].p2), v)) {
      static char aviso[144];
      char det[112];
      violacaoTexto(v, det, sizeof(det));
      snprintf(aviso, sizeof(aviso), "ponto %u do arquivo: %s",
               (unsigned)(i + 1), det);
      if (motivo) *motivo = aviso;
      return false;
    }
  }
  guardarParaDesfazer("carga de arquivo");
  memcpy(pontos, origem, (size_t)n * sizeof(Ponto));
  nPontos = n;
  // O ultimo ponto nao tem "proximo": solda ligada nele nao significa nada.
  pontos[nPontos - 1].soldaAteProximo = 0;
  return true;
}

bool progRemoverPonto(uint8_t indice) {
  if (indice >= nPontos) return false;
  guardarParaDesfazer("remocao de ponto");
  for (uint8_t i = indice; i + 1 < nPontos; i++) pontos[i] = pontos[i + 1];
  nPontos--;
  definirMensagem("Ponto %u removido", (unsigned)(indice + 1));
  return true;
}

void progDefinirSolda(uint8_t indice, bool ligar) {
  if (indice >= nPontos) return;
  pontos[indice].soldaAteProximo = ligar ? 1 : 0;
}

void progLimpar() {
  // Apagar um programa de trinta pontos ensinados a mao e o estrago que
  // mais custa caro nesta maquina. Ele guarda antes.
  if (nPontos) guardarParaDesfazer("programa apagado");
  nPontos = 0;
  fase    = FASE_PARADO;
  pausado = false;
  definirMensagem(desfTem ? "Programa apagado -- da para desfazer"
                          : "Programa apagado");
}

// ---------------------------------------------------------------------
bool progTemDesfazer()            { return desfTem; }
const char* progDescricaoDesfazer() { return desfOque; }

bool progDesfazer(const char** motivo) {
  if (!desfTem) {
    if (motivo) *motivo = "nao ha alteracao para desfazer";
    return false;
  }
  if (fase != FASE_PARADO) {
    if (motivo) *motivo = "pare o programa antes de desfazer";
    return false;
  }
  // Troca em vez de copiar: desfazer duas vezes seguidas volta ao que
  // estava, o que e o que o operador espera de um Ctrl+Z apertado sem
  // querer duas vezes.
  Ponto  troca[MAX_PONTOS];
  const uint8_t nTroca = nPontos;
  memcpy(troca, pontos, sizeof(Ponto) * nPontos);
  memcpy(pontos, desfPontos, sizeof(Ponto) * desfN);
  nPontos = desfN;
  memcpy(desfPontos, troca, sizeof(Ponto) * nTroca);
  desfN = nTroca;
  definirMensagem("Desfeito: %s (%u pontos)", desfOque, (unsigned)nPontos);
  return true;
}

// ---------------------------------------------------------------------
// Confere se a RETA entre dois pontos e percorrivel do inicio ao fim.
// Sem isso o robo descobriria um ponto inalcancavel no meio do cordao,
// com o arco ja aberto.
// ---------------------------------------------------------------------
static bool retaPercorrivelDet(uint8_t i, Violacao& v) {
  float x0, y0, x1, y1;
  pontoParaXY(pontos[i],     x0, y0);
  pontoParaXY(pontos[i + 1], x1, y1);
  return retaCartesianaValida(x0, y0, x1, y1,
                              passosParaGraus(J1, pontos[i].p1),
                              passosParaGraus(J2, pontos[i].p2), v);
}

// ---------------------------------------------------------------------
bool progConferirTrecho(uint8_t i, char* aviso, size_t tam) {
  if (aviso && tam) aviso[0] = '\0';
  if (i + 1 >= nPontos) return true;

  Violacao v;
  const bool ok = pontos[i].soldaAteProximo
                ? retaPercorrivelDet(i, v)
                : caminhoJuntasValidoDet(passosParaGraus(J1, pontos[i].p1),
                                         passosParaGraus(J2, pontos[i].p2),
                                         passosParaGraus(J1, pontos[i + 1].p1),
                                         passosParaGraus(J2, pontos[i + 1].p2), v);
  if (ok) return true;

  if (aviso && tam) {
    char det[112];
    violacaoTexto(v, det, sizeof(det));
    snprintf(aviso, tam, "%s %u->%u: %s",
             pontos[i].soldaAteProximo ? "cordao" : "deslocamento",
             (unsigned)(i + 1), (unsigned)(i + 2), det);
  }
  return false;
}

// ---------------------------------------------------------------------
bool progIniciar(bool modoEnsaio, const char** motivo) {
  // Todas as recusas passam por aqui: uma frase que diz o ponto ou o
  // trecho, a junta, o valor exigido e o limite. Recusa generica faz o
  // operador olhar dois pontos folgados e concluir que a maquina errou.
  static char aviso[208];
  aviso[0] = '\0';

  if (nPontos < 2) {
    if (motivo) *motivo = "grave pelo menos 2 pontos";
    return false;
  }
  // Executar nao exige calibracao: os pontos foram gravados nesta mesma
  // regua. O que a calibracao da e a protecao de curso.
  // Vale para o ensaio tambem: ele move o braco pelo percurso inteiro.
  if (!servosLigados) {
    if (motivo) *motivo = "habilite os servos antes de executar";
    return false;
  }

  Violacao v;
  char det[112];

  // A posicao ATUAL entra na conta: o braco parado fora da area util
  // reprovava o programa com uma mensagem que falava de junta, e o
  // operador ia procurar o defeito nos pontos.
  if (!posturaValidaDet(passosParaGraus(J1, posicaoJ1()),
                        passosParaGraus(J2, posicaoJ2()), v)) {
    violacaoTexto(v, det, sizeof(det));
    snprintf(aviso, sizeof(aviso),
             "o braco esta parado fora da area util (%s). Traga-o de volta com o jog", det);
    if (motivo) *motivo = aviso;
    return false;
  }

  for (uint8_t i = 0; i < nPontos; i++) {
    if (!posturaValidaDet(passosParaGraus(J1, pontos[i].p1),
                          passosParaGraus(J2, pontos[i].p2), v)) {
      violacaoTexto(v, det, sizeof(det));
      snprintf(aviso, sizeof(aviso), "ponto %u: %s", (unsigned)(i + 1), det);
      if (motivo) *motivo = aviso;
      return false;
    }
  }

  // Aproximacao ate o primeiro ponto: interpolada nas juntas.
  if (!caminhoJuntasValidoDet(passosParaGraus(J1, posicaoJ1()),
                              passosParaGraus(J2, posicaoJ2()),
                              passosParaGraus(J1, pontos[0].p1),
                              passosParaGraus(J2, pontos[0].p2), v)) {
    violacaoTexto(v, det, sizeof(det));
    snprintf(aviso, sizeof(aviso),
             "no caminho da posicao atual ate o ponto 1: %s", det);
    if (motivo) *motivo = aviso;
    return false;
  }

  // Cada trecho pelo que ele realmente percorre: reta cartesiana quando
  // ha solda, interpolacao nas juntas quando e so deslocamento.
  for (uint8_t i = 0; i + 1 < nPontos; i++) {
    if (!progConferirTrecho(i, aviso, sizeof(aviso))) {
      if (motivo) *motivo = aviso;
      return false;
    }
  }

  ensaio    = modoEnsaio;
  idx       = 0;
  concluiu  = false;
  pausado   = false;
  fracaoSeg = 0.0f;
  fase      = FASE_INDO_INICIO;

  soldaDesligar();
  moverCoordenado(pontos[0].p1, pontos[0].p2, velAuto);

  definirMensagem(ensaio ? "Ensaio iniciado, arco travado desligado"
                         : "Executando com arco");
  return true;
}

// ---------------------------------------------------------------------
static void prepararReta() {
  pontoParaXY(pontos[idx],     xa, ya);
  pontoParaXY(pontos[idx + 1], xb, yb);

  const float dist = sqrtf((xb - xa) * (xb - xa) + (yb - ya) * (yb - ya));
  const float vel  = (velCordaoMmS > 0.1f) ? velCordaoMmS : 1.0f;

  tSegTotal = (uint32_t)(dist / vel * 1000.0f);
  if (tSegTotal < 50) tSegTotal = 50;
  tSegIni   = millis();

  refT1 = passosParaGraus(J1, pontos[idx].p1);
  refT2 = passosParaGraus(J2, pontos[idx].p2);
  // Trava o ramo do cotovelo para o trecho inteiro. Reescolher o ramo a
  // cada 1,5 mm era o que fazia o braco largar a reta e dar uma volta
  // ate a postura espelhada quando os dois ramos quase coincidem.
  ramoSeg = ramoCotovelo(refT2);

  // Velocidade de seguimento dimensionada pelo PIOR passo da reta, nao
  // pela media. Num cordao perto da borda do alcance o mesmo 1,5 mm pede
  // dez vezes mais junta no meio do que nas pontas: com a media o motor
  // fica para tras exatamente ali, e a ponta corta caminho.
  float s1 = 0.0f, s2 = 0.0f;
  const int n = (int)(dist / PASSO_INTERP_MM) + 2;
  const float msPorPasso = (float)tSegTotal / (float)(n > 0 ? n : 1);
  if (retaMaiorSalto(xa, ya, xb, yb, refT1, refT2, s1, s2) && msPorPasso > 0.01f) {
    // graus/passo -> passos de motor por segundo, com 50% de folga.
    velSeg1 = (uint32_t)(s1 * J1.passosPorGrau * 1000.0f / msPorPasso * 1.5f) + 200;
    velSeg2 = (uint32_t)(s2 * J2.passosPorGrau * 1000.0f / msPorPasso * 1.5f) + 200;
  } else {
    const long d1 = labs((long)pontos[idx + 1].p1 - pontos[idx].p1);
    const long d2 = labs((long)pontos[idx + 1].p2 - pontos[idx].p2);
    velSeg1 = (uint32_t)((uint64_t)d1 * 1000 / tSegTotal) * 3 + 200;
    velSeg2 = (uint32_t)((uint64_t)d2 * 1000 / tSegTotal) * 3 + 200;
  }

  if (J1.motor) J1.motor->setAcceleration(grausPorSegParaHz(J1, J1.aceleracao * 4.0f));
  if (J2.motor) J2.motor->setAcceleration(grausPorSegParaHz(J2, J2.aceleracao * 4.0f));
}

// ---------------------------------------------------------------------
// Percorre a reta. Roda a CADA ciclo, inclusive com os motores andando:
// e um seguidor de setpoint, nao um "vai e espera chegar".
// ---------------------------------------------------------------------
static void atualizarReta() {
  const uint32_t decorrido = millis() - tSegIni;
  float a = (float)decorrido / (float)tSegTotal;
  if (a > 1.0f) a = 1.0f;

  const float x = xa + (xb - xa) * a;
  const float y = ya + (yb - ya) * a;

  float t1, t2;
  const char* m = nullptr;
  // Ramo travado: se ELE nao serve mais, o cordao acabou. Cair no outro
  // ramo daria a volta com o arco aberto.
  if (!resolverXYRamo(x, y, ramoSeg, t1, t2, &m)) {
    soldaDesligar();
    progParar();
    definirMensagem("Cordao %u->%u abortado a %.0f%% do trecho: %s",
                    (unsigned)(idx + 1), (unsigned)(idx + 2), a * 100.0f,
                    m ? m : "ponto invalido");
    return;
  }
  refT1 = t1; refT2 = t2;

  seguirSetpoint(grausParaPassos(J1, t1), grausParaPassos(J2, t2),
                 velSeg1, velSeg2);

  if (a >= 1.0f && !motoresEmMovimento()) {
    if (!ensaio && pontos[idx].soldaAteProximo) {
      fase       = FASE_FECHANDO_ARCO;
      marcaTempo = millis();
    } else {
      idx++;
      if (idx + 1 >= nPontos) {
        concluiu = true;
        progParar();
        definirMensagem(ensaio ? "Ensaio concluido" : "Programa concluido");
        return;
      }
      fase       = FASE_ABRINDO_ARCO;
      marcaTempo = millis();
    }
  }
}

// =====================================================================
//  Pausa e retomada
// =====================================================================
bool progPausado() { return pausado; }

uint8_t progFracaoTrecho() {
  if (!progRodando()) return 0;
  if (pausado) return (uint8_t)(fracaoSeg * 100.0f);
  if (fase != FASE_SOLDANDO || tSegTotal == 0) return 0;
  const uint32_t d = millis() - tSegIni;
  const uint32_t f = (uint64_t)d * 100 / tSegTotal;
  return (uint8_t)(f > 100 ? 100 : f);
}

bool progPausar(const char** motivo) {
  if (fase == FASE_PARADO) {
    if (motivo) *motivo = "nao ha programa em execucao";
    return false;
  }
  if (pausado) return true;

  // A fracao do cordao e a unica coisa que nao da para recalcular depois:
  // o relogio nao para junto com o braco. Guardar ANTES de qualquer outra
  // coisa.
  if (fase == FASE_SOLDANDO && tSegTotal > 0) {
    const uint32_t d = millis() - tSegIni;
    fracaoSeg = (float)d / (float)tSegTotal;
    if (fracaoSeg > 1.0f) fracaoSeg = 1.0f;
  } else {
    fracaoSeg = 0.0f;
  }

  // O ARCO FECHA. Arco aberto com o braco parado fura a chapa em
  // segundos -- nao existe pausa "segurando o arco".
  soldaDesligar();
  pararSuave();

  faseGuardada = fase;
  pausado      = true;
  definirMensagem("Programa pausado no trecho %u->%u, a %u%% dele",
                  (unsigned)(idx + 1), (unsigned)(idx + 2),
                  (unsigned)progFracaoTrecho());
  return true;
}

bool progRetomar(const char** motivo) {
  if (!pausado) {
    if (motivo) *motivo = "o programa nao esta pausado";
    return false;
  }
  if (!movimentoLiberado) {
    if (motivo) *motivo = "habilite os servos antes de retomar";
    return false;
  }

  pausado = false;

  if (faseGuardada == FASE_SOLDANDO) {
    // Recalcula a reta do trecho e "adianta o relogio" ate a fracao onde
    // parou: o seguidor de setpoint leva o braco de volta a esse ponto
    // sozinho, e o cordao continua de onde estava em vez de recomecar
    // por cima do que ja foi soldado.
    // prepararReta() aqui e so para recuperar xa/ya/xb/yb, o ramo do
    // cotovelo e as velocidades de seguimento do trecho. O relogio ela
    // zera, e por isso ele e readiantado logo em seguida.
    prepararReta();
    tSegIni = millis() - (uint32_t)(fracaoSeg * (float)tSegTotal);
    // Nao volta direto para FASE_SOLDANDO: o arco reabre com o mesmo
    // tempo de abertura de qualquer cordao, porque a poca esfriou na
    // pausa e retomar com o arco frio nao funde. Passar por
    // FASE_ABRINDO_ARCO tambem nao serve -- ela refaz prepararReta() e
    // perderia o adiantamento. Dai a fase propria.
    fase       = FASE_RETOMANDO;
    marcaTempo = millis();
  } else {
    // Deslocamento ou espera: refazer o trecho inteiro nao custa nada e
    // nao marca a peca.
    fase       = faseGuardada;
    marcaTempo = millis();
    if (fase == FASE_DESLOCANDO) {
      moverCoordenado(pontos[idx + 1].p1, pontos[idx + 1].p2, velAuto);
    } else if (fase == FASE_INDO_INICIO) {
      // A APROXIMACAO tem de ser reemitida. Sem isto o proximo ciclo ve o
      // braco parado, conclui "cheguei ao ponto 1" e abre o arco onde a
      // pausa pegou -- podendo ser dezenas de graus antes do inicio do
      // cordao -- e depois arrasta a ponta ate la com o arco aberto,
      // riscando a peca no caminho.
      moverCoordenado(pontos[0].p1, pontos[0].p2, velAuto);
    }
  }
  aplicarAceleracao();
  definirMensagem("Retomando o trecho %u->%u de onde parou",
                  (unsigned)(idx + 1), (unsigned)(idx + 2));
  return true;
}

// ---------------------------------------------------------------------
void progAtualizar() {
  if (fase == FASE_PARADO) return;
  if (pausado) return;

  // A fase de reta nao pode esperar os motores pararem.
  if (fase == FASE_SOLDANDO) { atualizarReta(); return; }

  if (motoresEmMovimento()) return;

  switch (fase) {
    case FASE_INDO_INICIO:
      idx        = 0;
      fase       = FASE_ABRINDO_ARCO;
      marcaTempo = millis();
      break;

    case FASE_ABRINDO_ARCO: {
      const bool comSolda = pontos[idx].soldaAteProximo;

      if (comSolda && !ensaio) {
        soldaDefinir(true);
        if (millis() - marcaTempo < DWELL_ABRE_ARCO_MS) return;
      }

      if (comSolda) {
        // Trecho de solda: RETA no espaco cartesiano.
        prepararReta();
        fase = FASE_SOLDANDO;
      } else {
        // Deslocamento: interpolacao nas juntas, mais rapida. O caminho
        // sai curvo, e nao ha problema nenhum nisso.
        moverCoordenado(pontos[idx + 1].p1, pontos[idx + 1].p2, velAuto);
        fase = FASE_DESLOCANDO;
      }
      break;
    }

    case FASE_DESLOCANDO:
      idx++;
      if (idx + 1 >= nPontos) {
        concluiu = true;
        progParar();
        definirMensagem(ensaio ? "Ensaio concluido" : "Programa concluido");
        return;
      }
      fase       = FASE_ABRINDO_ARCO;
      marcaTempo = millis();
      break;

    case FASE_RETOMANDO: {
      // Reabre o arco (se o trecho tem solda) e volta para a reta, com o
      // relogio ja adiantado ate a fracao onde a pausa pegou.
      const bool comSolda = pontos[idx].soldaAteProximo;
      if (comSolda && !ensaio) {
        soldaDefinir(true);
        if (millis() - marcaTempo < DWELL_ABRE_ARCO_MS) return;
        // O tempo de abertura nao pode contar como cordao andado: o
        // relogio da reta so recomeca agora.
        tSegIni = millis() - (uint32_t)(fracaoSeg * (float)tSegTotal);
      }
      fase = FASE_SOLDANDO;
      break;
    }

    case FASE_FECHANDO_ARCO:
      if (millis() - marcaTempo < DWELL_FECHA_ARCO_MS) return;
      soldaDesligar();
      idx++;
      if (idx + 1 >= nPontos) {
        concluiu = true;
        progParar();
        definirMensagem("Programa concluido");
        return;
      }
      fase       = FASE_ABRINDO_ARCO;
      marcaTempo = millis();
      break;

    default:
      break;
  }
}

void progParar() {
  if (fase == FASE_PARADO) return;

  // Ciclo = execucao COM ARCO que chegou ao fim. Ensaio nao conta: nao
  // gasta consumivel nem produz peca. Parado no meio conta como
  // abortado, e esse numero e tao util quanto o outro -- ele e que
  // mostra que a maquina esta sendo interrompida demais.
  if (!ensaio) producaoContarCiclo(concluiu);
  concluiu = false;

  fase    = FASE_PARADO;
  pausado = false;
  soldaDesligar();
  pararSuave();
  aplicarVelocidadeManual();
  aplicarAceleracao();
}
