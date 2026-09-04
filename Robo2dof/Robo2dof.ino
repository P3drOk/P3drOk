// =====================================================================
//  Robo2dof - braco de solda com gravacao de trajetoria
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
#include "encoder.h"
#include "correcao.h"
#include "aprender.h"
#include "ota.h"
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
// Porta unica de posicionamento. Valida, nesta ordem: seguranca, servos,
// a postura de DESTINO e o INTERIOR do caminho ate ela.
//
// O interior importa: moverCoordenado() interpola nas juntas, e o
// envelope cartesiano nao e convexo nesse espaco - da para ir de um ponto
// permitido a outro raspando a ponta na mesa no meio do trajeto.
//
// CALIBRACAO NAO E MAIS EXIGIDA EM LUGAR NENHUM.
//
// Ela era um portao: sem limites medidos, posicionar era recusado. Mas
// um ponto gravado e um par de contagens na MESMA regua com que ele vai
// ser perseguido -- se a regua nao mudou, o braco volta exatamente para
// onde estava. O que a calibracao acrescenta e a PROTECAO DE CURSO, e
// posturaValidaDet() ja trata "sem limites, nada e violacao". Quem
// protege ali sao os batentes da maquina e o operador, exatamente como
// no jog, que sempre rodou livre.
static bool irParaPassos(long p1, long p2, bool jaValidado = false) {
  if (!movimentoSeguro) {
    definirMensagem("Movimento bloqueado: intertravamento de seguranca");
    return false;
  }
  // Junta sem torque nao anda, e o alvo dela passa a ser onde ela esta.
  //
  // Sem isto o gerador de pulso contaria os passos do eixo parado e todo
  // limite de curso passaria a apontar para o lugar errado -- o mesmo
  // motivo por que o jog e por eixo. Com um driver so no barramento,
  // "ir para o zero" leva o eixo que tem torque e deixa o outro quieto.
  if (!J1.habilitado) p1 = posicaoJ1();
  if (!J2.habilitado) p2 = posicaoJ2();
  if (!J1.habilitado && !J2.habilitado) {
    definirMensagem("Habilite ao menos um servo antes de mover");
    return false;
  }

  // A VALIDACAO PODE JA TER ACONTECIDO EM GRAUS.
  //
  // Ir a um angulo calcula o destino em passos a partir da MEDIDA do
  // encoder, multiplicada pelo fator de escala -- e com fator diferente
  // de 1 esse numero de passos, lido de volta pela regua digitada, nao da
  // o angulo pedido. Validar ali seria conferir um angulo que ninguem
  // pediu. Quem chama de la ja conferiu os graus de verdade, com as
  // mesmas funcoes, e passa jaValidado.
  const char* motivo = nullptr;
  if (!jaValidado) {
    if (!posturaValidaPassos(p1, p2, &motivo)) {
      definirMensagem("Movimento recusado: %s", motivo ? motivo : "postura invalida");
      return false;
    }
    if (!caminhoJuntasValidoPassos(posicaoJ1(), posicaoJ2(), p1, p2, &motivo)) {
      definirMensagem("Movimento recusado: o caminho passa por %s",
                      motivo ? motivo : "postura invalida");
      return false;
    }
  }

  // UMA VELOCIDADE SO, e ela vem do campo em mm/s.
  //
  // Existia um modo PRECISAO, com uma segunda velocidade guardada a
  // parte, e um botao que alternava entre as duas. Com um controle unico
  // em mm/s isso perdeu o sentido: "precisao" virou digitar um numero
  // menor, que e mais direto do que lembrar em que modo a maquina esta.
  moverCoordenado(p1, p2, velAuto);
  // Movimento novo, assentamento novo: sem isto o "ja terminei" do
  // movimento anterior valeria para este, e a correcao rodaria uma vez
  // so na vida da maquina.
  correcaoNovoMovimento();
  modoAtual = MODO_POSICIONANDO;
  return true;
}

// =====================================================================
//  0 E 360 SAO O MESMO LUGAR, e o firmware nao sabia disso.
//
//  O angulo desta maquina e linear de ponta a ponta: sai de
//  (bruto - referencia) / escala + grausHome, e nao ha resto de divisao
//  em lugar nenhum. Isso e bom -- e o que deixa a maquina descrever um
//  eixo que da varias voltas sem se perder --, mas quem digita o destino
//  pensa em circulo. Da bancada, as duas caras do mesmo defeito:
//
//    "peco 0 grau, ele passa mesmo que minimamente, e ai se atrapalha
//     com o 360 e continua a trajetoria"
//    "esta em 340 e peco 5: ele vai pelo caminho mais dificil"
//
//  Com o braco em 359,95 e um pedido de 0, a conta da -359,95 e o eixo
//  da a volta inteira. De 340 para 5 ela da -335 onde o certo eram +25.
//
//  Esta funcao escolhe QUAL VOLTA do angulo pedido -- entre alvo + 360n
//  -- fica mais perto de onde o braco esta. E ela nao decide seguranca
//  nenhuma: so oferece um numero, do mais perto para o mais longe, e
//  para no primeiro que CABE no curso. O que nao cabe nunca e oferecido,
//  e se nada couber ela devolve o numero digitado -- deixando a recusa,
//  com o motivo certo, para posturaValida() logo abaixo.
//
//  A MARGEM E A MESMA DE posturaValidaDet(), e isso nao e detalhe:
//  cinematica.cpp avisa por escrito que, quando as duas contas divergem,
//  sobra uma faixa onde a postura e invalida e a gravidade e zero -- o
//  braco entra nela e nao sai mais.
//
//  Curso desligado ou junta sem calibracao: vale o mais perto, e ponto.
//  E o contrato que a maquina ja tem escrito -- ela nasce livre, e ali
//  quem protege sao os batentes e o operador.
// =====================================================================
static float voltaMaisProxima(uint8_t k, float alvo) {
  const Junta& j = (k == 1) ? J1 : J2;
  if (j.passosPorGrau <= 0.0f) return alvo;

  // ONDE O BRACO ESTA, na mesma regua do alvo. O encoder manda quando a
  // leitura serve; sem ela sobra a contagem, que e o que existe numa
  // maquina sem encoder.
  const float atual = leituraConfiavel(k)
                    ? encoderLer(k).graus
                    : passosParaGraus(j, (k == 1) ? posicaoJ1() : posicaoJ2());

  const long n = lroundf((atual - alvo) / 360.0f);
  if (n == 0) return alvo;              // o digitado ja e o mais perto

  // Do mais perto para o mais longe. O 0 no fim e o proprio numero
  // digitado: se nem ele couber, quem recusa e a validacao.
  const long ordem[3] = { n, (n > 0) ? n - 1 : n + 1, 0 };
  const bool cursoVale = protCurso && j.calibrada;
  for (uint8_t i = 0; i < 3; i++) {
    const float c = alvo + 360.0f * (float)ordem[i];
    if (!cursoVale) return c;
    if (c >= j.grausMin + MARGEM_LIMITE_GRAUS &&
        c <= j.grausMax - MARGEM_LIMITE_GRAUS) return c;
  }
  return alvo;
}

// Nomear um angulo: "ir para o zero" e "ir para um angulo" sao a mesma
// frase do operador, so muda o numero.
//
// O DESTINO SE CALCULA SOBRE A CONTAGEM, entao a contagem tem de
// descrever o braco ANTES da conta. Sem ancorar, mover a contagem ate o
// alvo deixava o braco parado no tanto do erro: na tela, o fantasma
// tracejado chegava ao angulo pedido e o braco desenhado nao. Ver
// ancorarNoEncoder() em correcao.h.
static void irParaAngulos(float t1, float t2) {
  // A ADOCAO AUTOMATICA DA ENGRENAGEM SAIU DAQUI, E NAO VOLTA.
  //
  // Ela existiu por uma rodada e o resultado na bancada foi o pior de
  // todos: "quando peco para ir a um angulo ele fica dando voltas sem
  // parar". A medida vem de uma divisao entre pulsos contados e voltas
  // lidas; num barramento com 7% de falha e 4,5 leituras por segundo,
  // uma leitura parada enquanto o eixo anda produz uma engrenagem
  // enorme, e a regua enorme transforma um pedido de 3 graus em
  // centenas de voltas. O teto que havia (2 milhoes de pulsos por volta)
  // e duzentas vezes o valor real: nao segurava nada.
  //
  // Duas medidas concordando tambem nao salvam: se a causa e leitura
  // parada, as duas concordam no mesmo numero errado.
  //
  // A medida continua sendo feita e continua aparecendo na tela, ao lado
  // do campo. Quem escreve a regua da maquina e uma pessoa.
  //
  // O que faz o braco CHEGAR sem depender da regua e o FATOR DE ESCALA,
  // que as viagens medem, mais o FREIO DE FUGA: o alvo fica guardado em
  // GRAUS e o encoder para o eixo se o destino calculado passar do ponto.
  // Se o encoder diz 3 graus, o braco esta em 3 graus -- e nenhuma regua
  // errada faz ele dar voltas.
  const Ancoragem anc = ancorarNoEncoder();

  // A VOLTA MAIS PROXIMA, antes de qualquer conta.
  //
  // Reescrever t1/t2 aqui faz a funcao INTEIRA trabalhar com a volta
  // escolhida -- a validacao em graus, o destino em passos pela medida e
  // o correcaoAlvoPedido() la embaixo. Isso e essencial e nao e
  // arrumacao: se o freio e o assentamento recebessem o 0 literal, eles
  // trariam o braco de volta a -360 depois de a rampa ja ter feito o
  // certo.
  const float pedido1 = t1, pedido2 = t2;
  t1 = voltaMaisProxima(1, t1);
  t2 = voltaMaisProxima(2, t2);

  // O DESTINO SAI DA MEDIDA, e nao da contagem convertida.
  //
  // Depois de ancorar, a contagem descreve o braco -- mas converter o
  // angulo pedido por grausParaPassos() usa a regua DIGITADA, e e ela que
  // costuma estar errada. O deslocamento que falta e conhecido em graus
  // do ENCODER; multiplicado pela escala que as viagens anteriores
  // mediram, ele vira o numero de pulsos que de fato leva o eixo ate la.
  //
  // Sem leitura confiavel nao ha medida de onde partir: ali vale a
  // conversao de sempre, e a mensagem la embaixo diz que foi assim.
  long p1 = grausParaPassos(J1, t1);
  long p2 = grausParaPassos(J2, t2);
  for (uint8_t k = 1; k <= 2; k++) {
    Junta& j = (k == 1) ? J1 : J2;
    if (!j.motor || j.passosPorGrau <= 0.0f) continue;
    if (!leituraConfiavel(k)) continue;
    const float alvoG = (k == 1) ? t1 : t2;
    const float falta = alvoG - encoderLer(k).graus;
    const long  agora = (k == 1) ? posicaoJ1() : posicaoJ2();
    const long  destino =
        agora + lroundf(falta * j.passosPorGrau * correcaoFatorEscala(k));
    if (k == 1) p1 = destino; else p2 = destino;
  }

  // VALIDACAO EM GRAUS, porque e em graus que o pedido existe.
  const char* motivo = nullptr;
  if (!posturaValida(t1, t2, &motivo)) {
    definirMensagem("Movimento recusado: %s", motivo ? motivo : "postura invalida");
    return;
  }
  if (!caminhoJuntasValido(passosParaGraus(J1, posicaoJ1()),
                           passosParaGraus(J2, posicaoJ2()), t1, t2, &motivo)) {
    definirMensagem("Movimento recusado: o caminho passa por %s",
                    motivo ? motivo : "postura invalida");
    return;
  }
  if (!irParaPassos(p1, p2, true)) return;

  // O ALVO EM GRAUS, guardado. Ele serve a duas coisas depois daqui: o
  // FREIO DE FUGA, enquanto o eixo anda, e o ASSENTAMENTO, quando ele
  // para -- os dois precisam do numero que o operador digitou, e nao da
  // contagem de destino convertida por uma regua que pode estar errada.
  //
  // Ir a um angulo e o unico movimento que tem angulo pedido. Ponto de
  // programa, DXF e trajetoria se dao em passos, sobre a mesma regua com
  // que foram gravados, e passam com valido=false.
  correcaoAlvoPedido(t1, t2, true);

  // A MENSAGEM DIZ EM QUE CONTA O MOVIMENTO SAIU.
  //
  // Ir por medida do encoder e ir pela contagem de pulsos sao duas
  // coisas diferentes, e so uma delas leva o BRACO ao angulo pedido
  // quando as duas divergem. Sair calado nos dois casos era o pior
  // pedaco do defeito: o operador pedia zero grau, o braco parava longe
  // dali, e a tela dizia a mesma frase de sempre.
  // A VOLTA ESCOLHIDA APARECE, quando nao e a digitada.
  //
  // Pedir 5 graus e ver o painel dizendo que o braco vai para 365 sem
  // nenhuma explicacao seria trocar um susto por outro. Esta frase vem
  // antes das outras porque e a que responde "por que ele foi por ali".
  if (fabsf(t1 - pedido1) > 0.5f || fabsf(t2 - pedido2) > 0.5f) {
    definirMensagem("Indo para %.1f / %.1f graus -- a volta mais proxima de "
                    "%.1f / %.1f, que e o mesmo lugar", t1, t2,
                    (double)pedido1, (double)pedido2);
  } else if (anc.semLeitura || anc.andando) {
    // Ha encoder configurado e ele nao serviu agora: este movimento NAO
    // e baseado na medida, e quem pediu tem de saber.
    const char* porque = anc.andando ? "o eixo ainda estava andando"
                                     : "sem leitura confiavel do encoder";
    definirMensagem("Indo para %.1f / %.1f graus PELA CONTAGEM: %s", t1, t2, porque);
  } else if (anc.ajuste > 0.5f) {
    // Reescrever a posicao da maquina em silencio seria a tela mudando de
    // numero sem ninguem entender por que. So avisa quando a correcao foi
    // grande o bastante para o operador ter visto o fantasma na tela.
    definirMensagem("Indo para %.1f / %.1f graus (a contagem estava %.2f "
                    "graus fora e foi acertada pelo encoder)",
                    t1, t2, (double)anc.ajuste);
  } else if (fabsf(correcaoFatorEscala(1) - 1.0f) > 0.05f ||
             fabsf(correcaoFatorEscala(2) - 1.0f) > 0.05f) {
    // A REGUA DIGITADA NAO BATE COM A MEDIDA, e o firmware esta
    // compensando. Dizer isso importa: quem escreve passosPorVolta e uma
    // pessoa, e ela nao tem outro jeito de descobrir que o numero esta
    // errado -- o braco chega no lugar certo justamente porque o
    // firmware esta segurando a diferenca.
    definirMensagem("Indo para %.1f / %.1f graus, medido pelo encoder "
                    "(a regua digitada esta %.2fx / %.2fx fora e o "
                    "movimento sai corrigido)", t1, t2,
                    (double)(1.0f / correcaoFatorEscala(1)),
                    (double)(1.0f / correcaoFatorEscala(2)));
  } else if (anc.comEncoder) {
    definirMensagem("Indo para %.1f / %.1f graus, medido pelo encoder", t1, t2);
  } else {
    // Maquina sem encoder nenhum: operar pela contagem e a escolha da
    // instalacao, nao uma falha, e nao ha o que avisar.
    definirMensagem("Indo para %.1f / %.1f graus", t1, t2);
  }
}

// ---------------------------------------------------------------------
static const char* NOME_CMD[] = {
  "JOG","PARAR","SERVOS","GRAVAR_INI","GRAVAR_FIM","REPRODUZIR",
  "TRAJ_LIMPAR","SOLDA","TESTE_RELE","PONTO_GRAVAR","PONTO_REMOVER",
  "PONTO_SOLDA","PROG_LIMPAR","PROG_EXECUTAR","PROG_PARAR","IR_PARA_PONTO",
  "APLICAR_CONFIG","RESTAURAR_PADROES","MOVER_ANGULOS","IR_HOME",
  "CALIB_INI","CALIB_CONF","CALIB_CANC","CALIB_APAGAR",
  "REFERENCIAR",
  "ENSINAR_ZERO","ESQUECER_ZERO","INVERTER_EIXO",
  "APLICAR_ENCODER","ENCODER_ZERAR","APRENDER",
  "PROG_PAUSAR","PROG_DESFAZER","PROG_REPETIR","MANUTENCAO_OK",
  "MESA_CANTO","MESA_LIMPAR","JOG_XY",
  "ARQ_SALVAR_PROG","ARQ_APLICAR_PROG","ARQ_SALVAR_TRAJ",
  "ARQ_CARREGAR_TRAJ","ARQ_LIBERAR_TRAJ","ARQ_SALVAR_CONFIG",
  "APAGAR_TUDO"
};

// Esta lista e indexada por c.tipo sem nenhuma conferencia de faixa: um
// comando novo no enum sem o nome correspondente aqui faz o log ler
// ponteiro fora do vetor -- e o crash aparece longe da causa, na
// primeira vez que aquele comando for usado. Ja aconteceu.
static_assert(sizeof(NOME_CMD) / sizeof(NOME_CMD[0]) == CMD_APAGAR_TUDO + 1,
              "NOME_CMD ficou fora de sincronia com TipoComando");

// ---------------------------------------------------------------------
// Encerramento unico. Todo caminho de parada passa por aqui: a parada do
// operador, a emergencia e a perda de conexao.
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
  // O retoque do encoder e movimento: parada de emergencia para ele
  // junto com todo o resto, senao o braco daria mais um passo depois do
  // botao vermelho.
  correcaoCancelar();

  pararSuave();
  aplicarAceleracao();
  aplicarVelocidadeManual();

  // FALHA so sai por rearme explicito do operador.
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

    case CMD_SERVOS:
      // Emergencia e condicao de NIVEL, nao evento: enquanto o botao
      // estiver acionado nao existe rearme. A versao anterior so reagia
      // na borda, entao bastava mandar CMD_SERVOS depois para religar o
      // torque com a emergencia pressionada.
      if (c.a != 0 && emergenciaAtiva) {
        definirMensagem("Emergencia acionada: solte o botao antes de rearmar");
        break;
      }
      servosHabilitar(c.a != 0, (uint8_t)c.b);
      if (c.a != 0 && modoAtual == MODO_FALHA) {
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

    case CMD_APAGAR_TUDO:
      // Nao volta desta chamada: ela reinicia a placa. Por isso o robo
      // tem de estar parado e sem torque -- reiniciar com o eixo
      // energizado deixa o driver sozinho por um segundo.
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Apagar tudo so com o robo parado no modo manual");
        break;
      }
      if (servosLigados) servosHabilitar(false);
      soldaDesligar();
      apagarTudo();
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
      // No aprendizado o caminho e o mesmo do botao fisico: a contagem
      // da sessao e o motivo da recusa tem de sair iguais, venha o toque
      // do dedo ou da tela.
      if (aprenderAtivo()) { aprenderGravarPonto(); break; }
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

    case CMD_PROG_PAUSAR: {
      const bool pausar = (c.a < 0) ? !progPausado() : (c.a != 0);
      const char* motivo = nullptr;
      if (pausar) {
        if (!progPausar(&motivo))
          definirMensagem("Pausa recusada: %s", motivo ? motivo : "erro");
      } else {
        if (!progRetomar(&motivo))
          definirMensagem("Retomada recusada: %s", motivo ? motivo : "erro");
      }
      break;
    }

    case CMD_PROG_DESFAZER: {
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Desfaca com o robo parado no modo manual");
        break;
      }
      const char* motivo = nullptr;
      if (!progDesfazer(&motivo))
        definirMensagem("Nada desfeito: %s", motivo ? motivo : "erro");
      break;
    }

    case CMD_PROG_REPETIR: {
      // "Mais uma peca": o caso normal de producao e repetir o mesmo
      // programa dezenas de vezes. Sem isto o operador tem de reabrir o
      // arquivo e reconfirmar o arco a cada peca.
      if (modoAtual != MODO_MANUAL) { definirMensagem("Robo ocupado"); break; }
      const char* motivo = nullptr;
      if (!progIniciar(false, &motivo)) {
        definirMensagem("Repeticao recusada: %s", motivo ? motivo : "erro");
        break;
      }
      modoAtual = MODO_EXECUTANDO;
      logEvento("repeticao: peca %lu do turno",
                (unsigned long)(producao.ciclosSessao + 1));
      break;
    }

    case CMD_MESA_CANTO: {
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Ensine os cantos da mesa com o robo parado no manual");
        break;
      }
      if (motoresEmMovimento()) {
        definirMensagem("Espere o braco parar para gravar o canto");
        break;
      }
      // O canto e onde a PONTA esta -- ela e a ferramenta, e a area util
      // e dela. O cotovelo passa por cima da mesa o tempo todo.
      float xc, yc, xp, yp;
      cinematicaDireta(passosParaGraus(J1, posicaoJ1()),
                       passosParaGraus(J2, posicaoJ2()), xc, yc, xp, yp);
      mesaEnsinarCanto(xp, yp);
      salvarConfiguracoes();
      if (areaMesa.definida) {
        definirMensagem("Canto %u gravado. Mesa: X de %.0f a %.0f, Y de %.0f a %.0f mm",
                        (unsigned)areaMesa.cantos, (double)areaMesa.xMin,
                        (double)areaMesa.xMax, (double)areaMesa.yMin,
                        (double)areaMesa.yMax);
      } else {
        definirMensagem("Canto %u gravado em X=%.0f Y=%.0f. Grave outro canto, "
                        "bem afastado deste", (unsigned)areaMesa.cantos,
                        (double)xp, (double)yp);
      }
      break;
    }

    case CMD_MESA_LIMPAR:
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("So com o robo parado no modo manual");
        break;
      }
      mesaLimpar();
      salvarConfiguracoes();
      definirMensagem("Area da mesa apagada: o braco volta a usar so o "
                      "Y minimo e o raio da base");
      break;

    case CMD_MANUTENCAO_OK:
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Registre a manutencao com o robo parado");
        break;
      }
      logEvento("manutencao registrada apos %lu ciclos",
                (unsigned long)producao.desdeManutencao);
      producaoZerarManutencao();
      definirMensagem("Manutencao registrada. Contador zerado");
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
      // A contagem do encoder tambem recomeca aqui: as duas medidas
      // tem de partir do mesmo ponto, senao o erro nasce torto.
      //
      // Esta chamada ficou anos INALCANCAVEL: a chave de fechar estava
      // depois dela, dentro do bloco de recusa e depois do break. O
      // compilador nao avisa de codigo morto assim, e o banco nao tinha
      // como perceber -- referenciar zerava a contagem de passos e
      // deixava a do encoder onde estava, que e exatamente o "erro nasce
      // torto" que a linha acima existe para evitar.
      encoderZerar(0);
      calibReferenciar();
      logEvento("referenciado na posicao atual");
      break;

    case CMD_ENSINAR_ZERO: {
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("Ensine o zero com o robo parado no modo manual");
        break;
      }
      const uint8_t k = (c.a == 2) ? 2 : 1;
      if (!encoderDefinirZero(k, c.f1)) {
        definirMensagem("Junta %u: sem leitura do encoder para ensinar o zero",
                        (unsigned)k);
        break;
      }
      configZero.ensinado[k - 1] = true;
      // A contagem passa a valer o angulo ensinado -- senao o painel
      // continuaria mostrando o valor antigo ate o proximo boot.
      Junta& jz = (k == 2) ? J2 : J1;
      ajustarContagem(jz, grausParaPassos(jz, c.f1));
      salvarConfiguracoes();
      definirMensagem("Junta %u: zero ensinado em %.2f graus. A maquina "
                      "se localiza sozinha ao ligar", (unsigned)k, (double)c.f1);
      break;
    }

    case CMD_ESQUECER_ZERO:
      if (modoAtual != MODO_MANUAL) {
        definirMensagem("So com o robo parado no modo manual");
        break;
      }
      for (uint8_t k = 1; k <= 2; k++)
        if (c.a == 0 || c.a == k) configZero.ensinado[k - 1] = false;
      salvarConfiguracoes();
      definirMensagem("Zero absoluto esquecido: a maquina volta a ligar como antes");
      break;

    case CMD_APLICAR_ENCODER:
      // Reabrir a UART e regravar o NVS com o braco andando nao e
      // perigoso, mas nao ha motivo: o encoder e leitura, e o operador
      // esta configurando, nao operando.
      if (modoAtual == MODO_MANUAL) aplicarEncoderPendente();
      else definirMensagem("Configure o encoder com o robo parado no modo manual");
      break;

    case CMD_ENCODER_ZERAR:
      // O zero do encoder anda junto com o zero da maquina: e o mesmo
      // instante em que se declara onde o braco esta.
      encoderZerar((uint8_t)c.a);
      definirMensagem("Encoder zerado na posicao atual");
      break;

    case CMD_APRENDER: {
      const bool entrar = (c.a < 0) ? !aprenderAtivo() : (c.a != 0);
      if (!entrar) { aprenderSair(nullptr); break; }
      const char* motivo = nullptr;
      if (!aprenderEntrar(&motivo)) {
        definirMensagem("Aprendizado recusado: %s", motivo ? motivo : "erro");
      }
      break;
    }

    case CMD_INVERTER_EIXO: {
      // Sentido do eixo. Vale em manual e TAMBEM na primeira etapa da
      // calibracao: e ali que o operador descobre que o braco vai para o
      // lado errado, e mandar cancelar o assistente para consertar era
      // pedir para ele desistir.
      //
      // So na etapa HOME: dali em diante ja ha limite medido, e trocar o
      // sinal do eixo depois inverteria o significado do que foi medido.
      const bool naReferencia = (modoAtual == MODO_CALIBRANDO &&
                                 calibNaPrimeiraEtapa());
      if (modoAtual != MODO_MANUAL && !naReferencia) {
        definirMensagem("Troque o sentido em manual ou na primeira etapa da calibracao");
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
  // O habilita mora no barramento desde que o fio do GPIO 23 saiu, e
  // quadro Modbus perdido nao se percebe sozinho. Esta e a unica hora em
  // que alguem confere o que o driver respondeu.
  bool habilitouAgora = false;
  if (servosSupervisionar(habilitouAgora) && modoAtual != MODO_FALHA) {
    // Desabilitar que nao confirmou: o eixo pode estar energizado, o
    // firmware nao sabe, e nao ha segundo caminho para cortar o torque.
    pararTudo(nullptr);
    modoAtual = MODO_FALHA;
    logEvento("SON: desabilitar nao confirmou no barramento");
  }

  bool estop = false;
  if (ESTOP_FISICO_INSTALADO) {
    // HIGH = emergencia: botao apertado (contato NC abre) OU fio
    // partido. Ver a nota de ligacao em config.h -- a polaridade e o que
    // faz um cabo rompido parar a maquina em vez de sumir em silencio.
    estop = (digitalRead(PIN_ESTOP) == ESTOP_NIVEL_ATIVO);
  }

  const bool semConexao =
      (ultimoContatoOperadorMs != 0) &&
      (millis() - ultimoContatoOperadorMs > TIMEOUT_CONEXAO_MS);

  // Emergencia por NIVEL. A borda dispara a parada completa; enquanto o
  // botao continuar acionado, o torque e mantido desligado a cada ciclo.
  // Reagir so na borda deixava um CMD_SERVOS posterior religar tudo com
  // a emergencia pressionada.
  if (estop) {
    if (!emergenciaAtiva) {
      emergenciaAtiva = true;
      pararTudo("EMERGENCIA acionada no botao fisico");
      // O botao vermelho encerra tudo, inclusive o aprendizado: depois
      // dele a maquina tem de estar num estado que ninguem precise
      // adivinhar.
      aprenderSair("EMERGENCIA acionada: aprendizado encerrado");
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
  // A parte que nao depende de torque, para o jog de um eixo poder
  // consultar sem depender do torque do outro.
  movimentoSeguro = !estop && !emergenciaAtiva && !semConexao &&
                    modoAtual != MODO_FALHA;
  // O portao coordenado: exige AS DUAS juntas. Programa, trajetoria e ir
  // ao zero movem os dois eixos -- com um sem torque sai desenho torto,
  // nao meio desenho.
  movimentoLiberado = movimentoSeguro && servosLigados;

  // Intertravamento do rele: o mesmo portao, mais a calibracao.
  soldaPermitir(movimentoLiberado && modoAtual != MODO_CALIBRANDO);
  soldaAtualizar();

  if (PIN_LED_STATUS != 255) {
    digitalWrite(PIN_LED_STATUS, motoresEmMovimento() ? HIGH : LOW);
  }
}

// ---------------------------------------------------------------------
static void publicar() {
  // O intervalo de 40 ms e uma economia de banda para valores que mudam
  // o tempo todo (posicao, velocidade). Modo e etapa do assistente nao
  // sao desses: o painel decide o que pode mandar olhando para eles, e
  // uma copia velha faz o servidor aceitar um comando que o core 1 vai
  // recusar logo em seguida -- o operador ve "ok" e depois a recusa.
  // Entao troca de modo ou de etapa publica na hora.
  static uint8_t modoPublicado  = 255;
  static uint8_t calibPublicada = 255;
  const bool mudouEstado = ((uint8_t)modoAtual != modoPublicado ||
                            (uint8_t)estadoCalib != calibPublicada);
  if (!mudouEstado && millis() - ultimaPublicacao < 40) return;
  ultimaPublicacao = millis();
  modoPublicado  = (uint8_t)modoAtual;
  calibPublicada = (uint8_t)estadoCalib;

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

  s.solda         = soldaLigada();
  s.servosLigados = servosLigados;
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
  if (ESTOP_FISICO_INSTALADO) {
    pinMode(PIN_ESTOP, INPUT_PULLUP);
    // Conferencia de linha no boot. Com o botao solto e o fio inteiro o
    // pino tem de estar em LOW; se ja nasce em nivel de emergencia, ou o
    // botao esta acionado ou o cabo nao chegou. Dizer isso no arranque
    // evita a cena de o operador passar meia hora achando que os servos
    // "nao ligam".
    delay(5);
    if (digitalRead(PIN_ESTOP) == ESTOP_NIVEL_ATIVO) {
      Serial.println("[ESTOP] Linha em nivel de emergencia no arranque: "
                     "botao acionado ou cabo interrompido.");
    }
  }

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

  // Encoder por Modbus: tarefa propria no core 0, so leitura.
  encoderIniciar();

  // Botao de aprendizado. Com APRENDER_BOTAO_INSTALADO=false nem o pino
  // e configurado: entrada solta le ruido, e ruido aqui gravaria ponto
  // sozinho no meio de um programa.
  aprenderIniciar();

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

  // Copia da configuracao no cartao. So marca -> grava; ver estado.h.
  // Fica aqui, no core 1, porque e ele o dono da configuracao viva.
  configCopiarParaCartaoSePreciso();

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
      // QUEM LEVA O EIXO E A RAMPA, ja programada na largada: ela
      // desacelera e para exatamente no passo comandado, sem precisar de
      // ninguem olhando. O freio so vigia -- se a medida disser, em duas
      // leituras seguidas, que o eixo passou do alvo com folga, ele
      // encurta a viagem. Quase sempre ele termina sem ter feito nada.
      //
      // O jog NAO roda aqui. Ele rodava porque a busca o usava para
      // andar; sem ela, o dedo do operador nao tem o que fazer no meio de
      // um posicionamento.
      correcaoFrearNoAlvo();
      if (!motoresEmMovimento()) {
        // O QUE A VIAGEM ENSINOU, antes de qualquer outra coisa.
        //
        // Aqui o eixo acabou de parar e as duas pontas do percurso ainda
        // valem. No movimento seguinte nao valeriam mais: ancorarNoEncoder()
        // reescreve a contagem de passos e apaga o percurso desta viagem.
        //
        // A conta espera uma leitura que seja de DEPOIS da parada -- num
        // barramento de 217 ms a ultima ainda e de antes, com o eixo
        // andando --, e por isso o robo segura o POSICIONANDO ate ela
        // chegar. O prazo, dentro da propria conta, evita que um
        // barramento mudo prenda a maquina aqui.
        correcaoAprenderDaViagem();
        if (correcaoAprendendo()) break;
        // Chegou pela conta de passos. Antes de liberar o jog, o encoder
        // diz onde o braco REALMENTE parou, e o sistema da um retoque se
        // precisar. E isto que faz sair de uma posicao e voltar cair no
        // mesmo lugar, em vez de acumular desvio a cada viagem.
        //
        // Continua em POSICIONANDO enquanto assenta: em MANUAL o jog
        // estaria liberado, e o operador brigaria com o retoque.
        if (correcaoResumo().estado == CORR_PARADA) correcaoIniciar();
        correcaoAtualizar();
        if (correcaoEmCurso()) break;

        aplicarVelocidadeManual();
        aplicarAceleracao();
        modoAtual = MODO_MANUAL;
        const ResumoCorrecao rc = correcaoResumo();
        // O FREIO TER AGIDO NAO PODE SUMIR NA MENSAGEM FINAL.
        //
        // Ele so age quando o destino calculado caiu longe do angulo
        // pedido, e isso quer dizer uma coisa so: passosPorVolta esta
        // errado. A tela ja mostra o fator medido ao lado do campo, mas
        // "cheguei, e tive de frear" e a unica pista no instante em que
        // acontece -- e sem ela o operador ve um posicionamento bem
        // sucedido e nunca descobre que a regua da maquina mente.
        const bool freou = correcaoFreou(1) || correcaoFreou(2);
        if (freou)
          definirMensagem("Posicionado -- mas o encoder teve de FREAR a "
                          "viagem: o destino calculado passou do ponto. "
                          "Confira pulsos por volta na aba Encoder");
        else if (rc.estado == CORR_PRONTA && rc.tentativas > 0)
          definirMensagem("Posicionado e conferido pelo encoder (%u retoque%s)",
                          (unsigned)rc.tentativas, rc.tentativas == 1 ? "" : "s");
        else if (rc.estado == CORR_DESISTIU || rc.estado == CORR_RECUSADA)
          definirMensagem("Posicionamento concluido -- %s", rc.motivo);
        else
          definirMensagem("Posicionamento concluido");
      }
      break;

    case MODO_CALIBRANDO:
      // Nao ha mais etapa automatica: em todas elas quem move o braco e
      // o operador, entao o jog vale sempre.
      jogAtualizar();
      calibAtualizar();
      break;

    case MODO_FALHA:
      pararSuave();
      break;
  }

  // Reinicio pedido pela atualizacao de firmware. Ele acontece AQUI, no
  // core 1, e nao dentro do handler HTTP: a resposta precisa chegar ao
  // navegador antes, senao o operador fica olhando para uma requisicao
  // que morreu sem saber se deu certo.
  if (otaPrecisaReiniciar()) {
    pararTudo("Reiniciando com o firmware novo");
    otaReiniciarAgora();
  }

  // A NUMERACAO VOLTA PARA A FAIXA DE TRABALHO, com o eixo parado.
  // Antes de zeroAtualizar(): assim a ida ao zero do boot ja sai da
  // numeracao certa, em vez de dar uma volta inteira para chegar a um
  // lugar onde o braco ja esta.
  normalizarVolta();
  zeroAtualizar();
  // A ordem importa: seguirEixoSolto() acerta a contagem com o braco
  // solto ANTES de o botao poder gravar um ponto neste mesmo ciclo.
  // Invertida, o primeiro ponto de cada toque sairia da contagem velha.
  seguirEixoSolto();
  aprenderAtualizar();
  // O ritmo do barramento decide quem leva o eixo: ver
  // encoderGuiaOMovimento() em correcao.h. Medido a cada ciclo, sempre --
  // inclusive parado, para a decisao ja estar tomada quando o proximo
  // movimento comecar.
  correcaoMedirRitmo();
  correcaoVigiar();

  // ---------------------------------------------------------------------
  // TRAVAMENTO NO MEIO DE UM MOVIMENTO AUTOMATICO
  //
  // O vigia para o EIXO -- continuar dando pulso contra o batente aquece
  // o servo. Mas parar o eixo nao basta: as maquinas de estado que rodam
  // por cima (programa, reproducao, posicionamento) esperam o movimento
  // acabar para seguir, e "parou" e exatamente o sinal delas de "cheguei".
  //
  // Sem isto, um travamento no caminho ate o primeiro ponto fazia o
  // programa concluir a aproximacao ali mesmo e ABRIR O ARCO onde o braco
  // tinha travado, dezenas de graus antes do inicio do cordao.
  //
  // Travou = a maquina nao esta onde acha que esta. Nao ha como continuar
  // um percurso automatico a partir dai.
  // ---------------------------------------------------------------------
  {
    static uint32_t travamentosVistos = 0;
    const Travamento tv = correcaoTravamento();
    if (tv.total != travamentosVistos) {
      travamentosVistos = tv.total;
      if (modoAtual == MODO_EXECUTANDO || modoAtual == MODO_REPRODUZINDO ||
          modoAtual == MODO_POSICIONANDO) {
        // nullptr preserva a mensagem que o vigia acabou de escrever, que
        // e mais especifica do que qualquer coisa que se diga aqui.
        pararTudo(nullptr);
        logEvento("travamento da junta %u interrompeu o movimento automatico",
                  (unsigned)tv.junta);
      }
    }
  }
  publicar();
  vTaskDelay(pdMS_TO_TICKS(1));
}
