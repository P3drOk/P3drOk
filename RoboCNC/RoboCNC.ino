// =====================================================================
//  RoboCNC 2DOF - braco de solda com gravacao de trajetoria
//
//  Divisao de trabalho entre os nucleos:
//    core 0 -> servidor web. So enfileira Comando e le Snapshot.
//    core 1 -> loop(). Unico dono dos motores, da solda e do estado.
//  Nunca chame motores.h / solda.h de dentro de um handler HTTP.
// =====================================================================

#include "config.h"
#include "estado.h"
#include "motores.h"
#include "cinematica.h"
#include "trajetoria.h"
#include "solda.h"
#include "calibracao.h"
#include "programa.h"
#include "armazenamento.h"
#include "servidor_web.h"
#include "rede.h"
#include <math.h>

static bool     emergenciaAtiva  = false;
static bool     conexaoPerdida   = false;
static uint32_t ultimaPublicacao = 0;


// ---------------------------------------------------------------------
// Core 0: servidor web. Nao toca em motor, rele ou estado -- so
// enfileira Comando.
static void tarefaRede(void* p) {
  (void)p;
  for (;;) {
    servidorAtender();
    redeAtender();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// ---------------------------------------------------------------------
// Porta unica de posicionamento. Valida, nesta ordem: calibracao, servos,
// a postura de DESTINO e o INTERIOR do caminho ate ela.
//
// O interior importa: moverCoordenado() interpola nas juntas, e o
// envelope cartesiano nao e convexo nesse espaco - da para ir de um ponto
// permitido a outro raspando a ponta na mesa no meio do trajeto.
static bool irParaPassos(long p1, long p2) {
  if (!J1.calibrada || !J2.calibrada) {
    definirMensagem("Calibre as juntas antes de usar posicionamento");
    return false;
  }
  if (!servosLigados) {
    definirMensagem("Habilite os servos antes de mover");
    return false;
  }

  const char* motivo = nullptr;
  if (!posturaValidaPassos(p1, p2, &motivo)) {
    definirMensagem("Movimento recusado: %s", motivo ? motivo : "postura invalida");
    return false;
  }
  if (!caminhoJuntasValidoPassos(posicaoJ1(), posicaoJ2(), p1, p2, &motivo)) {
    definirMensagem("Movimento recusado: o caminho passa por %s",
                    motivo ? motivo : "postura invalida");
    return false;
  }

  moverCoordenado(p1, p2, velAuto);
  modoAtual = MODO_POSICIONANDO;
  return true;
}

static void irParaAngulos(float t1, float t2) {
  if (irParaPassos(grausParaPassos(J1, t1), grausParaPassos(J2, t2))) {
    definirMensagem("Indo para %.1f / %.1f graus", t1, t2);
  }
}

// ---------------------------------------------------------------------
static const char* NOME_CMD[] = {
  "JOG","PARAR","PRECISAO","SERVOS","GRAVAR_INI","GRAVAR_FIM","REPRODUZIR",
  "TRAJ_LIMPAR","SOLDA","TESTE_RELE","PONTO_GRAVAR","PONTO_REMOVER",
  "PONTO_SOLDA","PROG_LIMPAR","PROG_EXECUTAR","PROG_PARAR","IR_PARA_PONTO",
  "APLICAR_CONFIG","RESTAURAR_PADROES","MOVER_ANGULOS","IR_HOME",
  "CALIB_INI","CALIB_CONF","CALIB_CANC","CALIB_APAGAR",
  "REFERENCIAR","AFERIR_MARCAR","AFERIR_APLICAR","INVERTER_EIXO","JOG_XY",
  "ARQ_SALVAR_PROG","ARQ_APLICAR_PROG","ARQ_SALVAR_TRAJ",
  "ARQ_CARREGAR_TRAJ","ARQ_LIBERAR_TRAJ","ARQ_SALVAR_CONFIG"
};

// ---------------------------------------------------------------------
// Encerramento unico. Todo caminho de parada passa por aqui: a parada do
// operador, o alarme de driver, a emergencia e a perda de conexao.
//
// A versao anterior parava trajetoria e gravacao mas nunca o programa de
// solda: a fase ficava congelada em FASE_SOLDANDO com o modo de volta em
// MANUAL, e como e progParar() quem restaura a aceleracao, o jog seguinte
// rodava com o valor 4x de prepararReta().
// ---------------------------------------------------------------------
static void pararTudo(const char* motivo) {
  soldaDesligar();
  jogZerar();

  // Sem isto, os heartbeats de jog que ja estavam na fila sao processados
  // logo depois da parada e o braco volta a andar no mesmo ciclo.
  limparFilaComandos();

  if (progRodando())      progParar();
  if (trajGravando())     trajPararGravacao();
  if (trajReproduzindo()) trajPararReproducao();
  if (calibAtiva())       calibCancelar();

  pararSuave();
  aplicarAceleracao();
  aplicarVelocidadeManual();

  // FALHA so sai por rearme explicito, depois de o alarme sumir.
  if (modoAtual != MODO_FALHA) modoAtual = MODO_MANUAL;

  if (motivo) definirMensagem("%s", motivo);
}

// ---------------------------------------------------------------------
static void processarComando(const Comando& c) {
  // Log de tudo que chega: se um comando some, da para ver se ele
  // chegou ao core 1 e por que foi descartado.
  if (c.tipo != CMD_JOG) {
    Serial.printf("[CMD] %s (a=%ld b=%ld) modo=%d\n",
                  NOME_CMD[c.tipo], (long)c.a, (long)c.b, (int)modoAtual);
  }

  // Em falha, so parada e reconhecimento passam.
  if (modoAtual == MODO_FALHA && c.tipo != CMD_PARAR && c.tipo != CMD_SERVOS) {
    Serial.println("[CMD] descartado: sistema em FALHA");
    definirMensagem("Sistema em falha: rearme antes de comandar");
    return;
  }

  switch (c.tipo) {
    case CMD_JOG:
      if (modoAtual == MODO_MANUAL || modoAtual == MODO_GRAVANDO ||
          modoAtual == MODO_CALIBRANDO) {
        jogDefinir((uint8_t)c.a, (int8_t)c.b);
      }
      break;

    // Joystick: os dois eixos num comando so. f1 e f2 vao de -1 a +1;
    // o sinal e a direcao e o modulo e a fracao da velocidade.
    case CMD_JOG_XY:
      if (modoAtual == MODO_MANUAL || modoAtual == MODO_GRAVANDO ||
          modoAtual == MODO_CALIBRANDO) {
        const float f[2] = { c.f1, c.f2 };
        for (uint8_t i = 0; i < 2; i++) {
          const float mag = fabsf(f[i]);
          // A zona morta e aplicada aqui, no core 1, e nao so no
          // navegador: comando que chega de fora tambem passa por ela.
          if (mag < JOY_ZONA_MORTA) { jogDefinir(i + 1, 0, 0.0f); continue; }
          // Reescala para que o movimento comece do zero na borda da
          // zona morta, em vez de dar um salto ao sair dela.
          const float frac = (mag - JOY_ZONA_MORTA) / (1.0f - JOY_ZONA_MORTA);
          jogDefinir(i + 1, (f[i] > 0.0f) ? 1 : -1, frac);
        }
      }
      break;

    case CMD_PARAR:
      pararTudo("PARADA: movimento interrompido e solda desligada");
      break;

    case CMD_PRECISAO:
      modoPrecisao = (c.a < 0) ? !modoPrecisao : (c.a != 0);
      aplicarVelocidadeManual();
      definirMensagem("Modo precisao %s", modoPrecisao ? "ligado" : "desligado");
      break;

    case CMD_SERVOS:
      // Emergencia e condicao de NIVEL, nao evento: enquanto o botao
      // estiver acionado nao existe rearme. A versao anterior so reagia
      // na borda, entao bastava mandar CMD_SERVOS depois para religar o
      // torque com a emergencia pressionada.
      if (c.a != 0 && emergenciaAtiva) {
        definirMensagem("Emergencia acionada: solte o botao antes de rearmar");
        break;
      }
      servosHabilitar(c.a != 0);
      if (c.a != 0 && modoAtual == MODO_FALHA && !motoresLerAlarmes()) {
        modoAtual = MODO_MANUAL;
        definirMensagem("Falha reconhecida, sistema liberado");
      }
      break;

    case CMD_GRAVAR_INICIAR:
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("So e possivel gravar a partir do modo manual");
        break;
      }
      pararSuave();
      trajIniciarGravacao();
      modoAtual = MODO_GRAVANDO;
      break;

    case CMD_GRAVAR_PARAR:
      if (modoAtual == MODO_GRAVANDO) {
        jogZerar();
        soldaDesligar();
        trajPararGravacao();
        modoAtual = MODO_MANUAL;
      }
      break;

    case CMD_REPRODUZIR: {
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Robo ocupado");
        break;
      }
      const char* motivo = nullptr;
      if (!trajIniciarReproducao(&motivo)) {
        definirMensagem("Reproducao recusada: %s", motivo ? motivo : "erro");
        break;
      }
      modoAtual = MODO_REPRODUZINDO;
      break;
    }

    case CMD_TRAJ_LIMPAR:
      if (modoAtual == MODO_MANUAL) trajLimpar();
      break;

    case CMD_APLICAR_CONFIG:
      // A aplicacao acontece AQUI, no core 1, e nao dentro do handler
      // HTTP. recalcularResolucao() altera passosPorGrau, grausMin e
      // grausMax - campos que posturaValida() le a cada ciclo de
      // jogAtualizar(). O handler so preenche a area de preparo.
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Ajustes so com o robo parado no modo manual");
        break;
      }
      aplicarConfigPendente();
      salvarConfiguracoes();
      aplicarVelocidadeManual();
      aplicarAceleracao();
      aplicarSentido();
      definirMensagem("Ajustes salvos");
      break;

    case CMD_RESTAURAR_PADROES:
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Restauracao so com o robo parado no modo manual");
        break;
      }
      restaurarPadroes();
      aplicarVelocidadeManual();
      aplicarAceleracao();
      aplicarSentido();
      definirMensagem("Ajustes de fabrica restaurados");
      break;

    case CMD_PONTO_GRAVAR: {
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Grave pontos com o robo parado, no modo manual");
        break;
      }
      const char* motivo = nullptr;
      if (!progAdicionarPonto(posicaoJ1(), posicaoJ2(), &motivo)) {
        definirMensagem("Ponto recusado: %s", motivo ? motivo : "erro");
      }
      break;
    }

    case CMD_PONTO_REMOVER:
      if (modoAtual == MODO_MANUAL) progRemoverPonto((uint8_t)c.a);
      break;

    case CMD_PONTO_SOLDA:
      if (modoAtual == MODO_MANUAL) progDefinirSolda((uint8_t)c.a, c.b != 0);
      break;

    case CMD_PROG_LIMPAR:
      if (modoAtual == MODO_MANUAL) progLimpar();
      break;

    case CMD_IR_PARA_PONTO: {
      if (modoAtual != MODO_MANUAL) break;
      if (c.a < 0 || c.a >= progQuantidade()) break;
      // O ponto foi validado quando gravado. Desde entao as protecoes
      // podem ter sido ligadas, os elos remedidos ou a calibracao
      // refeita: revalida destino e caminho antes de sair do lugar.
      const Ponto& p = progLista()[c.a];
      if (irParaPassos(p.p1, p.p2)) {
        definirMensagem("Indo para o ponto %ld", (long)(c.a + 1));
      }
      break;
    }

    case CMD_PROG_EXECUTAR: {
      if (modoAtual != MODO_MANUAL) { definirMensagem("Robo ocupado"); break; }
      const bool ensaio = (c.a != 0);
      // progIniciar() exige servos para os dois modos: o ensaio tambem
      // percorre o programa inteiro com o braco.
      const char* motivo = nullptr;
      if (!progIniciar(ensaio, &motivo)) {
        definirMensagem("Execucao recusada: %s", motivo ? motivo : "erro");
        break;
      }
      modoAtual = MODO_EXECUTANDO;
      logEvento("%s iniciado: %u pontos, cordao a %.1f mm/s",
                ensaio ? "ensaio" : "PROGRAMA COM ARCO",
                (unsigned)progQuantidade(), velCordaoMmS);
      break;
    }

    case CMD_PROG_PARAR:
      progParar();
      modoAtual = MODO_MANUAL;
      break;

    case CMD_TESTE_RELE:
      if (modoAtual == MODO_MANUAL) soldaTestar(2000);
      else definirMensagem("Teste de saida so no modo manual");
      break;

    case CMD_SOLDA:
      // Acionamento manual do arco: so no manual ou durante a gravacao,
      // porque na reproducao quem manda no rele e a trajetoria.
      if (modoAtual == MODO_MANUAL || modoAtual == MODO_GRAVANDO) {
        soldaDefinir(c.a != 0);
      }
      break;

    case CMD_MOVER_ANGULOS:
      if (modoAtual == MODO_MANUAL) irParaAngulos(c.f1, c.f2);
      break;

    case CMD_IR_HOME:
      if (modoAtual == MODO_MANUAL) irParaAngulos(0.0f, 0.0f);
      break;

    case CMD_CALIB_INICIAR:
      if (modoAtual == MODO_MANUAL) calibIniciar();
      break;

    case CMD_CALIB_CONFIRMAR:
      // f1/f2 mudam de sentido conforme a etapa: angulo da referencia no
      // HOME, curso real medido na conclusao. Ver calibracao.h.
      if (modoAtual == MODO_CALIBRANDO) calibConfirmar(c.f1, c.f2);
      break;

    case CMD_CALIB_CANCELAR:
      if (modoAtual == MODO_CALIBRANDO) calibCancelar();
      break;

    case CMD_REFERENCIAR:
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Referencie com o robo parado no modo manual");
        break;
      }
      calibReferenciar();
      logEvento("referenciado na posicao atual");
      break;

    case CMD_AFERIR_MARCAR:
      if (modoAtual == MODO_MANUAL) aferirMarcar((uint8_t)c.a);
      else definirMensagem("Afira com o robo parado no modo manual");
      break;

    case CMD_AFERIR_APLICAR:
      if (modoAtual == MODO_MANUAL) aferirAplicar((uint8_t)c.a, c.f1);
      else definirMensagem("Afira com o robo parado no modo manual");
      break;

    case CMD_INVERTER_EIXO: {
      // Sentido do eixo. Vale em manual e TAMBEM na primeira etapa da
      // calibracao: e ali que o operador descobre que o braco vai para o
      // lado errado, e mandar cancelar o assistente para consertar era
      // pedir para ele desistir.
      //
      // So na etapa HOME: dali em diante ja ha limite medido, e trocar o
      // sinal do eixo depois inverteria o significado do que foi medido.
      const bool naReferencia = (modoAtual == MODO_CALIBRANDO &&
                                 estadoCalib == CAL_HOME);
      if (modoAtual != MODO_MANUAL && !naReferencia) {
        definirMensagem("Troque o sentido em manual ou na etapa de referencia");
        break;
      }
      if (motoresEmMovimento()) {
        definirMensagem("Espere o braco parar para trocar o sentido");
        break;
      }
      Junta& j = (c.a == 2) ? J2 : J1;
      j.inverterDir = (c.b != 0);
      aplicarSentido();
      salvarConfiguracoes();
      definirMensagem("Junta %d: sentido %s", (c.a == 2) ? 2 : 1,
                      j.inverterDir ? "invertido" : "normal");
      break;
    }

    case CMD_CALIB_APAGAR:
      // Vale tambem no meio do assistente: e a saida de quem quer comecar
      // do zero sem herdar nada da medicao anterior.
      if (modoAtual == MODO_MANUAL || modoAtual == MODO_CALIBRANDO) {
        calibApagar();
        logEvento("calibracao apagada pelo operador");
      } else {
        definirMensagem("Apague a calibracao com o robo parado");
      }
      break;

    // -----------------------------------------------------------------
    // ARQUIVOS. O core 1 nunca encosta no SPI do cartao: ele so prepara
    // ou consome as areas de troca e delega a tarefa do core 0.
    // -----------------------------------------------------------------
    case CMD_ARQ_SALVAR_PROG:
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Salve arquivos com o robo parado no modo manual");
        break;
      }
      if (progQuantidade() < 2) {
        definirMensagem("Nada para salvar: grave pelo menos 2 pontos");
        break;
      }
      // Copia rapida para a area de troca; a gravacao e da tarefa de SD.
      armStagingDefinir(progLista(), progQuantidade());
      if (!armSolicitar(TAR_SALVAR_PROG, c.nome)) {
        definirMensagem("Cartao ocupado ou ausente");
      }
      break;

    case CMD_ARQ_APLICAR_PROG: {
      // Postado pela tarefa de SD depois de ler e validar a sintaxe.
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Carregue programas com o robo no modo manual");
        break;
      }
      const char* motivo = nullptr;
      if (!progCarregarDe(armStagingPontos(), armStagingN(), &motivo)) {
        definirMensagem("Programa recusado: %s", motivo ? motivo : "invalido");
        break;
      }
      // Os pontos sao gravados em graus. Com outro comprimento de elo, o
      // mesmo par de angulos aponta para outro lugar da chapa.
      const float e1 = armStagingElo1(), e2 = armStagingElo2();
      if (e1 > 0.0f && e2 > 0.0f &&
          (fabsf(e1 - elo1Mm) > 0.5f || fabsf(e2 - elo2Mm) > 0.5f)) {
        definirMensagem("Programa \"%s\" carregado, mas foi feito com elos %.0f+%.0f mm (agora %.0f+%.0f). Ensaie antes de soldar",
                        c.nome, e1, e2, elo1Mm, elo2Mm);
      } else {
        definirMensagem("Programa \"%s\" carregado: %u pontos",
                        c.nome, (unsigned)progQuantidade());
      }
      logEvento("programa carregado: %s (%u pontos)", c.nome,
                (unsigned)progQuantidade());
      break;
    }

    case CMD_ARQ_SALVAR_TRAJ:
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Salve arquivos com o robo parado no modo manual");
        break;
      }
      if (trajPontos() < 2) { definirMensagem("Nenhuma trajetoria gravada"); break; }
      // Empresta o buffer: enquanto a tarefa de SD estiver lendo, gravar
      // ou reproduzir fica recusado.
      if (!trajEmprestar()) { definirMensagem("Trajetoria em uso"); break; }
      if (!armSolicitar(TAR_SALVAR_TRAJ, c.nome)) {
        trajDevolver();
        definirMensagem("Cartao ocupado ou ausente");
      }
      break;

    case CMD_ARQ_CARREGAR_TRAJ:
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Carregue arquivos com o robo no modo manual");
        break;
      }
      if (!trajEmprestar()) { definirMensagem("Trajetoria em uso"); break; }
      if (!armSolicitar(TAR_CARREGAR_TRAJ, c.nome)) {
        trajDevolver();
        definirMensagem("Cartao ocupado ou ausente");
      }
      break;

    case CMD_ARQ_LIBERAR_TRAJ:
      trajDevolver();
      break;

    case CMD_ARQ_SALVAR_CONFIG:
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Salve arquivos com o robo parado no modo manual");
        break;
      }
      // A area de preparo ja e a forma canonica da configuracao viva.
      prepararConfigPendente();
      if (!armSolicitar(TAR_SALVAR_CONFIG, c.nome)) {
        definirMensagem("Cartao ocupado ou ausente");
      }
      break;
  }
}

// ---------------------------------------------------------------------
// Supervisao de seguranca: roda antes de qualquer coisa, todo ciclo.
// ---------------------------------------------------------------------
static void supervisionar() {
  const bool alarme = motoresLerAlarmes();

  bool estop = false;
  if (ESTOP_FISICO_INSTALADO) {
    estop = (digitalRead(PIN_ESTOP) == LOW);
  }

  const bool semConexao =
      (ultimoContatoOperadorMs != 0) &&
      (millis() - ultimoContatoOperadorMs > TIMEOUT_CONEXAO_MS);

  if (alarme && modoAtual != MODO_FALHA) {
    pararTudo(nullptr);
    modoAtual = MODO_FALHA;
    definirMensagem("ALARME do driver (J1:%d J2:%d). Verifique e rearme",
                    (int)J1.alarme, (int)J2.alarme);
    logEvento("ALARME driver J1=%d J2=%d em t1=%.1f t2=%.1f",
              (int)J1.alarme, (int)J2.alarme,
              passosParaGraus(J1, posicaoJ1()), passosParaGraus(J2, posicaoJ2()));
  }

  // Emergencia por NIVEL. A borda dispara a parada completa; enquanto o
  // botao continuar acionado, o torque e mantido desligado a cada ciclo.
  // Reagir so na borda deixava um CMD_SERVOS posterior religar tudo com
  // a emergencia pressionada.
  if (estop) {
    if (!emergenciaAtiva) {
      emergenciaAtiva = true;
      pararTudo("EMERGENCIA acionada no botao fisico");
      logEvento("EMERGENCIA acionada no botao fisico");
    }
    if (servosLigados) servosHabilitar(false);
  } else if (emergenciaAtiva) {
    emergenciaAtiva = false;
    definirMensagem("Emergencia liberada. Rearme os servos");
  }

  if (semConexao && !conexaoPerdida) {
    conexaoPerdida = true;
    pararTudo("Conexao perdida: movimento e solda interrompidos");
    logEvento("conexao perdida: movimento e arco cortados");
  } else if (!semConexao) {
    conexaoPerdida = false;
  }

  // Portao unico de movimento (estado.h). Escrito aqui, consultado por
  // jogAtualizar() e por todo caminho que possa mover um motor.
  movimentoLiberado = servosLigados && !alarme && !estop &&
                      !emergenciaAtiva && !semConexao &&
                      modoAtual != MODO_FALHA;

  // Intertravamento do rele: o mesmo portao, mais a calibracao.
  soldaPermitir(movimentoLiberado && modoAtual != MODO_CALIBRANDO);
  soldaAtualizar();

  if (PIN_LED_STATUS != 255) {
    digitalWrite(PIN_LED_STATUS, motoresEmMovimento() ? HIGH : LOW);
  }
}

// ---------------------------------------------------------------------
static void publicar() {
  if (millis() - ultimaPublicacao < 40) return;
  ultimaPublicacao = millis();

  Snapshot s;
  s.modo  = (uint8_t)modoAtual;
  s.calib = (uint8_t)estadoCalib;
  s.p1 = posicaoJ1();
  s.p2 = posicaoJ2();
  s.t1 = passosParaGraus(J1, s.p1);
  s.t2 = passosParaGraus(J2, s.p2);

  float xc, yc;
  cinematicaDireta(s.t1, s.t2, xc, yc, s.x, s.y);

  // Velocidade real das juntas e da ponta. A ponta sai do jacobiano do
  // braco 2R: cada junta contribui com raio x velocidade angular.
  s.v1Hz = velocidadeJ1Hz();
  s.v2Hz = velocidadeJ2Hz();
  {
    const float w1 = (J1.passosPorGrau > 0.0f) ? s.v1Hz / J1.passosPorGrau : 0.0f;
    const float w2 = (J2.passosPorGrau > 0.0f) ? s.v2Hz / J2.passosPorGrau : 0.0f;
    const float k  = (float)M_PI / 180.0f;
    const float a1 = s.t1 * k, a12 = (s.t1 + s.t2) * k;
    const float vx = -elo1Mm * sinf(a1) * (w1 * k)
                     - elo2Mm * sinf(a12) * ((w1 + w2) * k);
    const float vy =  elo1Mm * cosf(a1) * (w1 * k)
                     + elo2Mm * cosf(a12) * ((w1 + w2) * k);
    s.vPontaMmS = sqrtf(vx * vx + vy * vy);
  }

  s.precisao      = modoPrecisao;
  s.solda         = soldaLigada();
  s.servosLigados = servosLigados;
  s.alarme1       = J1.alarme;
  s.alarme2       = J2.alarme;
  s.calibrada1    = J1.calibrada;
  s.calibrada2    = J2.calibrada;
  s.emMovimento   = motoresEmMovimento();
  s.trajPontos    = trajPontos();
  s.trajDuracaoMs = trajDuracaoMs();
  s.trajProgresso = trajProgresso();
  strncpy(s.mensagem, ultimaMensagem, sizeof(s.mensagem) - 1);
  s.mensagem[sizeof(s.mensagem) - 1] = '\0';

  publicarSnapshot(s);
}

// ---------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  if (PIN_LED_STATUS != 255) {
    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, LOW);
  }
  if (ESTOP_FISICO_INSTALADO) pinMode(PIN_ESTOP, INPUT_PULLUP);

  soldaIniciar();          // rele desligado antes de qualquer outra coisa
  carregarConfiguracoes();

  filaComandos = xQueueCreate(24, sizeof(Comando));

  if (!motoresIniciar()) {
    modoAtual = MODO_FALHA;
    definirMensagem("Falha ao iniciar os geradores de pulso");
  }

  // A ordem importa: o socket TCP so pode ser aberto depois que a
  // interface de rede existe. Wi-Fi primeiro, servidor depois.
  redeIniciar();
  servidorIniciar();

  xTaskCreatePinnedToCore(tarefaRede, "rede", 8192, nullptr, 1, nullptr, 0);

  // Cartao por ultimo: ele le o contador de partidas no NVS (core 1) e
  // so entao cria a propria tarefa no core 0.
  armIniciar();

  // Ocupacao do flash no boot. A pasta do sketch traz um partitions.csv
  // com 3 MB de app; se alguem gravar com a particao errada, isto
  // aparece antes de o problema virar "trava do nada".
  {
    const uint32_t usado = ESP.getSketchSize();
    const uint32_t livre = ESP.getFreeSketchSpace();
    const uint32_t total = usado + livre;
    Serial.printf("[FLASH] sketch %u kB de %u kB de particao (%u%% usado)\n",
                  (unsigned)(usado / 1024), (unsigned)(total / 1024),
                  total ? (unsigned)((uint64_t)usado * 100 / total) : 0u);
    if (livre < 64UL * 1024UL) {
      Serial.println("[FLASH] ATENCAO: menos de 64 kB livres na particao de app.");
      Serial.println("[FLASH] Use Tools > Partition Scheme > Huge APP (3MB No OTA).");
    }
    Serial.printf("[RAM]   %u kB livres\n", (unsigned)(ESP.getFreeHeap() / 1024));
  }

  definirMensagem("Pronto. Habilite os servos para comecar");
  logEvento("sistema iniciado");
}

// ---------------------------------------------------------------------
void loop() {
  supervisionar();

  // A parada NAO passa pela fila. A fila e compartilhada com o heartbeat
  // de jog (100 ms por eixo) e xQueueSend descarta quando enche - uma
  // parada de emergencia nao pode depender de haver espaco em buffer.
  if (pedidoParada) {
    pedidoParada = false;
    pararTudo("PARADA: movimento interrompido e solda desligada");
  }

  Comando c;
  while (xQueueReceive(filaComandos, &c, 0) == pdTRUE) {
    processarComando(c);
  }

  switch (modoAtual) {
    case MODO_MANUAL:
      jogAtualizar();
      break;

    case MODO_GRAVANDO:
      jogAtualizar();
      trajAmostrar(posicaoJ1(), posicaoJ2(), soldaLigada());
      break;

    case MODO_REPRODUZINDO:
      trajAtualizarReproducao();
      if (!trajReproduzindo()) modoAtual = MODO_MANUAL;
      break;

    case MODO_EXECUTANDO:
      progAtualizar();
      if (!progRodando()) modoAtual = MODO_MANUAL;
      break;

    case MODO_POSICIONANDO:
      if (!motoresEmMovimento()) {
        aplicarVelocidadeManual();
        aplicarAceleracao();
        modoAtual = MODO_MANUAL;
        definirMensagem("Posicionamento concluido");
      }
      break;

    case MODO_CALIBRANDO:
      // O retorno automatico ao zero nao pode ser interrompido pelo jog.
      if (calibEixoAtivo() == 0 ||
          estadoCalib == CAL_J1_NEG || estadoCalib == CAL_J1_POS ||
          estadoCalib == CAL_J2_NEG || estadoCalib == CAL_J2_POS) {
        jogAtualizar();
      }
      calibAtualizar();
      break;

    case MODO_FALHA:
      pararSuave();
      break;
  }

  publicar();
  vTaskDelay(pdMS_TO_TICKS(1));
}
