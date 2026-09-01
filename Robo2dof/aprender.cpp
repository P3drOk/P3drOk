#include "aprender.h"
#include "motores.h"
#include "solda.h"
#include "programa.h"
#include "cinematica.h"
#include <string.h>

// Tudo aqui roda no CORE 1: o modo corta torque e grava ponto, e as duas
// coisas sao do dono dos motores. A web nao chama nada deste arquivo --
// ela enfileira CMD_APRENDER.

static ResumoAprender r;
static bool ligado = false;

// ---------------------------------------------------------------------
// Leitura do botao, filtrada.
//
// Contato mecanico repica por alguns milissegundos. Sem filtro, um unico
// toque viraria meia duzia de pontos no programa -- e o operador so
// descobriria isso na hora de soldar.
// ---------------------------------------------------------------------
static bool     nivelEstavel    = false;   // true = apertado
static bool     nivelCru        = false;
static uint32_t mudouEm         = 0;
static uint32_t apertadoEm      = 0;
static bool     gestoTratado    = false;   // um gesto por aperto
static bool     primeiraLeitura = true;

static void dizer(const char* m) {
  strncpy(r.motivo, m ? m : "", sizeof(r.motivo) - 1);
  r.motivo[sizeof(r.motivo) - 1] = '\0';
}

// ---------------------------------------------------------------------
// Da para soltar o braco?
//
// So se TODA JUNTA QUE SE MEXE for acompanhada pelo encoder. O SON e um
// fio unico para os dois drivers: nao existe soltar so uma. Uma junta
// solta e nao medida cai pelo proprio peso e a contagem dela nao anda --
// o ponto gravado sairia certo num eixo e errado no outro, que e pior do
// que sair errado nos dois, porque parece plausivel.
//
// A REGRA E SOBRE AS JUNTAS QUE EXISTEM, nao sobre o numero dois.
//
// A versao anterior exigia encoder e zero nas DUAS, e com isso uma
// maquina de um eixo so -- ou com o segundo driver ainda na bancada --
// nunca soltava o braco: o modo entrava com torque e o operador tinha de
// posicionar pelas setas, para gravar um caminho a mao. Junta que nao
// esta no barramento nao tem peso proprio para cair nem contagem para
// desencontrar; recusar por causa dela era proteger uma junta que nao
// existe.
//
// O que continua valendo, e e o que importa: junta PRESENTE tem de estar
// medida. E tem de haver pelo menos uma -- sem nenhuma nao ha o que
// gravar.
// ---------------------------------------------------------------------
static bool podeSoltarOBraco(uint8_t& culpada) {
  culpada = 0;
  if (!configEncoder.ativo) return false;
  uint8_t presentes = 0;
  for (uint8_t k = 1; k <= 2; k++) {
    if (configEncoder.reg[k - 1] == 0) continue;   // junta ausente
    presentes++;
    if (!configZero.ensinado[k - 1]) {
      culpada = k;
      return false;
    }
  }
  return presentes > 0;
}

// ---------------------------------------------------------------------
bool aprenderAtivo() { return ligado; }

ResumoAprender aprenderResumo() {
  ResumoAprender s = r;
  s.instalado = APRENDER_BOTAO_INSTALADO;
  s.ativo     = ligado;
  return s;
}

// ---------------------------------------------------------------------
bool aprenderEntrar(const char** motivo) {
  if (ligado) return true;

  if (modoAtual != MODO_MANUAL) {
    if (motivo) *motivo = "so a partir do modo manual";
    return false;
  }
  if (motoresEmMovimento()) {
    if (motivo) *motivo = "espere o braco parar";
    return false;
  }
  // CALIBRAR E OPCIONAL, e ensinar pontos nao depende disso.
  //
  // O argumento antigo era que sem calibracao nao ha angulo, so contagem
  // de pulsos. Mas um ponto ensinado e gravado na mesma regua com que
  // sera reproduzido: se a regua nao mudar, o braco volta exatamente
  // para onde estava. O que a calibracao acrescenta e a PROTECAO DE
  // CURSO -- saber onde estao os batentes --, e isso e outra coisa.

  // Ninguem ensina caminho com o arco aberto.
  soldaDesligar();
  jogZerar();

  ligado       = true;
  r.gravados   = 0;
  r.recusados  = 0;
  dizer("");

  uint8_t culpada = 0;
  if (podeSoltarOBraco(culpada)) {
    servosHabilitar(false);
    r.bracoSolto = true;
    definirMensagem("Aprendizado: braco solto. Leve a ponta e toque o botao "
                    "para gravar");
  } else {
    // Recusar o modo inteiro aqui seria trocar uma comodidade por nada:
    // com torque ele funciona igual, so que o operador posiciona pelas
    // setas. O que nao pode e soltar o braco fingindo que o encoder
    // acompanha.
    r.bracoSolto = false;
    if (culpada == 0) {
      definirMensagem("Aprendizado com torque: o encoder esta desligado. "
                      "Posicione pelas setas e toque para gravar");
    } else {
      definirMensagem("Aprendizado com torque: junta %u sem zero ensinado. "
                      "Posicione pelas setas e toque para gravar",
                      (unsigned)culpada);
    }
  }
  return true;
}

// ---------------------------------------------------------------------
void aprenderSair(const char* motivo) {
  if (!ligado) return;
  const unsigned n = r.gravados;
  ligado = false;
  r.bracoSolto = false;

  if (motivo) {
    definirMensagem("%s", motivo);
    return;
  }
  // O torque NAO volta sozinho. Habilitar servo e acao explicita em todo
  // o resto do sistema; aqui, com a mao do operador ainda dentro da area
  // do braco, muito mais.
  if (!servosLigados) {
    definirMensagem("Aprendizado encerrado: %u ponto%s. Habilite os servos "
                    "para operar", n, (n == 1) ? "" : "s");
  } else {
    definirMensagem("Aprendizado encerrado: %u ponto%s", n, (n == 1) ? "" : "s");
  }
}

// ---------------------------------------------------------------------
bool aprenderGravarPonto() {
  if (!ligado) {
    definirMensagem("Segure o botao 1,5 s para entrar no aprendizado");
    return false;
  }
  // Com torque, o operador pode tocar o botao com o eixo ainda andando
  // pelas setas -- e ai o ponto sai um pedaco adiante do que ele viu.
  if (motoresEmMovimento()) {
    if (r.recusados < 255) r.recusados++;
    dizer("o braco ainda estava andando");
    definirMensagem("Espere o braco parar para gravar o ponto");
    return false;
  }

  const char* motivo = nullptr;
  if (!progAdicionarPonto(posicaoJ1(), posicaoJ2(), &motivo)) {
    if (r.recusados < 255) r.recusados++;
    dizer(motivo);
    definirMensagem("Ponto recusado: %s", motivo ? motivo : "erro");
    return false;
  }
  if (r.gravados < 255) r.gravados++;
  dizer("");
  // progAdicionarPonto() ja anuncia "Ponto N gravado" -- a contagem que
  // o operador quer ver e a do programa, nao a desta sessao.
  return true;
}

// ---------------------------------------------------------------------
void aprenderIniciar() {
  memset(&r, 0, sizeof(r));
  ligado = false;
  nivelEstavel = nivelCru = false;
  mudouEm = apertadoEm = 0;
  gestoTratado = false;
  primeiraLeitura = true;
  if (APRENDER_BOTAO_INSTALADO) pinMode(PIN_APRENDER, INPUT_PULLUP);
}

// ---------------------------------------------------------------------
void aprenderAtualizar() {
  // O modo vive no manual. Executar programa, reproduzir, calibrar ou
  // cair em falha sao situacoes em que ninguem esta ensinando nada -- e
  // em que um toque no botao nao pode gravar ponto.
  if (ligado && modoAtual != MODO_MANUAL) {
    aprenderSair("Aprendizado encerrado: o robo saiu do modo manual");
  }
  // O operador religou o torque pela tela no meio do aprendizado. O modo
  // continua (ele ainda grava pontos), mas o braco nao esta mais solto e
  // a tela precisa parar de dizer que esta.
  if (ligado && r.bracoSolto && servosLigados) {
    r.bracoSolto = false;
    definirMensagem("Torque religado: o braco nao esta mais solto. "
                    "Posicione pelas setas");
  }

  if (!APRENDER_BOTAO_INSTALADO) return;

  const uint32_t agora = millis();
  const bool cru = (digitalRead(PIN_APRENDER) == LOW);   // pull-up: LOW = apertado
  if (cru != nivelCru) { nivelCru = cru; mudouEm = agora; }

  if (primeiraLeitura) {
    // No boot nao existe borda: so se registra em que estado o pino
    // esta. Botao preso (ou fio em curto) nao pode valer como gesto.
    primeiraLeitura = false;
    nivelEstavel = cru;
    gestoTratado = cru;
    r.apertado   = cru;
    return;
  }

  if (cru != nivelEstavel && (uint32_t)(agora - mudouEm) >= APRENDER_DEBOUNCE_MS) {
    nivelEstavel = cru;
    if (cru) {
      apertadoEm   = agora;
      gestoTratado = false;
    } else if (!gestoTratado) {
      // Soltou antes de completar o segurar: foi toque curto.
      aprenderGravarPonto();
    }
  }

  // Segurar dispara no instante em que completa o tempo, com o botao
  // ainda apertado. Esperar soltar deixaria o operador em duvida sobre
  // se ja deu tempo -- assim o braco solta na mao dele, e ele sabe.
  if (nivelEstavel && !gestoTratado &&
      (uint32_t)(agora - apertadoEm) >= APRENDER_SEGURAR_MS) {
    gestoTratado = true;
    if (ligado) {
      aprenderSair(nullptr);
    } else {
      const char* motivo = nullptr;
      if (!aprenderEntrar(&motivo)) {
        dizer(motivo);
        definirMensagem("Aprendizado recusado: %s", motivo ? motivo : "erro");
      }
    }
  }

  r.apertado = nivelEstavel;
}

#ifdef ROBO2DOF_TESTE
void aprenderReiniciarTeste() { aprenderIniciar(); }
#endif
