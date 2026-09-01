#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include "encoder.h"
#include <math.h>

static FastAccelStepperEngine engine = FastAccelStepperEngine();

// O ultimo pedido de habilita, e se ele ainda espera resposta. Escritos
// e lidos so pelo core 1, que e quem chama servosHabilitar e quem
// supervisiona.
//
// A supervisao acompanha o PEDIDO, nao o codigo de resultado. Comparar
// resultados parece equivalente e nao e: habilitar que deu certo e
// desabilitar que deu certo sao os dois SON_OK, entao um desabilita logo
// depois de um habilita nao teria transicao nenhuma para ver -- e a tela
// continuaria dizendo "habilitado" com o braco ja solto.
static bool    sonPedidoEraLigar = false;
static bool    sonAguardando     = false;
static uint8_t sonPedidoJunta    = 0;

// Se ESTE firmware chegou a energizar cada junta desde o boot.
//
// E o que separa os dois "desabilitar nao confirmou". Junta que tinha
// torque e nao respondeu ao corte e o caso grave: o eixo pode estar
// energizado e nao existe segundo caminho para cortar -- isso e FALHA.
// Junta que nunca foi energizada e um driver que nao esta no barramento:
// nao ha o que cortar, e derrubar a maquina por isso travava o robo por
// causa de um motor que nao esta la.
static bool    sonJaEnergizou[2] = {false, false};


static int8_t   jogDir[2]     = {0, 0};
static uint32_t jogUltimoMs[2] = {0, 0};
static float    jogFracao[2]  = {1.0f, 1.0f};
// Ultimo valor realmente programado no gerador de pulso, para nao
// reprogramar a rampa a cada ciclo de 1 ms.
static uint32_t jogHzAplicado[2]      = {0, 0};
static int8_t   jogSentidoAplicado[2] = {0, 0};

// ---------------------------------------------------------------------
static uint32_t limitarFreq(uint32_t v) {
  if (v < 1) return 1;
  if (v > FREQ_PULSO_MAX_HZ) return FREQ_PULSO_MAX_HZ;
  return v;
}

// ---------------------------------------------------------------------
static uint32_t velProgramada[2] = {0, 0};

bool motoresIniciar() {
  J1.pinoPulso = PIN_J1_PULSO; J1.pinoDir = PIN_J1_DIR;
  J2.pinoPulso = PIN_J2_PULSO; J2.pinoDir = PIN_J2_DIR;

  sonPedidoEraLigar = false;
  sonAguardando     = false;
  sonPedidoJunta    = 0;
  sonJaEnergizou[0] = sonJaEnergizou[1] = false;
  J1.habilitado = J2.habilitado = false;

  // O habilita nao tem pino: vai por Modbus, na tarefa do encoder. No
  // boot ninguem escreveu nada ainda, entao o que o firmware SABE e que
  // nao habilitou -- nao que o driver esteja desabilitado. Sao coisas
  // diferentes, e a segunda so se descobre escrevendo.
  servosLigados = false;

  engine.init();
  J1.motor = engine.stepperConnectToPin(J1.pinoPulso);
  J2.motor = engine.stepperConnectToPin(J2.pinoPulso);

  if (!J1.motor || !J2.motor) {
    Serial.println("!!! Falha ao conectar os geradores de pulso !!!");
    return false;
  }

  // A lembranca do que ja esta programado nao vale depois de reconectar
  // os geradores: no boot eles estao no padrao deles, nao no ultimo valor
  // que este firmware escreveu.
  velProgramada[0] = velProgramada[1] = 0;

  aplicarSentido();
  aplicarVelocidadeManual();
  aplicarAceleracao();
  return true;
}

// ---------------------------------------------------------------------
// Habilita o torque. 'junta' e 1, 2, ou 0 para as duas.
//
// POR JUNTA, e nao um interruptor so. Cada driver e um escravo Modbus
// proprio: exigir que os dois confirmem impedia de trabalhar numa
// bancada com um driver ligado -- habilitar recusava tudo dizendo que o
// segundo nao respondeu, o que era verdade e nao ajudava ninguem.
// ---------------------------------------------------------------------
void servosHabilitar(bool ligar, uint8_t junta) {
  if (!ligar) {
    // Desabilitar so uma junta ainda para o movimento inteiro: um cordao
    // com um eixo sem torque nao e meio cordao, e um desenho torto.
    pararSuave();
    soldaDesligar();
  }

  // Sem registrador nao ha habilita. Dizer "habilitado" aqui seria a
  // tela mentindo sobre um braco que ninguem energizou -- ou, pior, um
  // desabilita que nunca saiu.
  if (configSon.reg == 0) {
    J1.habilitado = J2.habilitado = false;
    servosLigados = false;
    definirMensagem("Habilita nao configurado: ache o registrador em "
                    "Ajustes antes de mover a maquina");
    return;
  }

  sonPedidoEraLigar = ligar;
  sonPedidoJunta    = junta;
  sonAguardando     = true;
  encoderPedirSon(ligar, junta);

  // O pedido e assincrono, e quem confere e servosSupervisionar() a cada
  // ciclo. Ate a confirmacao chegar, o firmware assume o lado SEGURO: ao
  // habilitar so acredita depois do OK; ao desabilitar ja para de deixar
  // a maquina andar, mesmo antes de o driver responder.
  if (junta != 2) J1.habilitado = false;
  if (junta != 1) J2.habilitado = false;
  servosLigados = J1.habilitado && J2.habilitado;

  const char* alvo = (junta == 1) ? " da junta 1"
                   : (junta == 2) ? " da junta 2" : "";
  definirMensagem(ligar ? "Habilitando os servos%s pelo barramento..."
                        : "Desabilitando os servos%s pelo barramento...", alvo);
}

// ---------------------------------------------------------------------
// A confirmacao do habilita, olhada a cada ciclo pelo core 1.
//
// Existe porque o caminho do habilita deixou de ser falha segura quando
// o fio saiu. Um pino errado se percebe na hora; um quadro Modbus que se
// perdeu no barramento nao se percebe nunca, a menos que alguem olhe.
//
// Devolve true quando uma escrita de DESABILITAR falhou -- o caso em que
// a maquina tem de cair em FALHA, porque nao se sabe se o eixo esta
// energizado e nao ha outro caminho para tirar o torque.
// ---------------------------------------------------------------------
bool servosSupervisionar(bool& habilitouAgora) {
  habilitouAgora = false;
  if (!sonAguardando) return false;

  const uint8_t e = encoderSonEstado();
  if (e == SON_PENDENTE || e == SON_OCIOSO) return false;
  sonAguardando = false;

  const bool    pedidoEraLigar = sonPedidoEraLigar;
  const uint8_t junta          = sonPedidoJunta;

  // O que cada junta ficou valendo.
  //
  // Habilitar so vale com confirmacao: sem ela o braco continua sem
  // torque, que e o estado seguro.
  //
  // Desabilitar zera SEMPRE, inclusive quando a escrita falhou. Este
  // campo e o PORTAO DE MOVIMENTO, nao um sensor de torque: depois de um
  // desabilita que nao confirmou ninguem pode mover a maquina, e deixar
  // o portao aberto porque "talvez ainda tenha torque" seria ler o campo
  // ao contrario do que ele serve. Que o eixo pode estar energizado quem
  // diz e a FALHA e a mensagem, que e onde essa informacao ajuda.
  bool cortePerdidoComTorque = false;
  for (uint8_t k = 1; k <= 2; k++) {
    if (junta != 0 && junta != k) continue;
    Junta& j = (k == 1) ? J1 : J2;
    const bool ok = encoderSonJuntaOk(k);
    if (!pedidoEraLigar && !ok && sonJaEnergizou[k - 1]) cortePerdidoComTorque = true;
    j.habilitado = pedidoEraLigar && ok;
    if (pedidoEraLigar && ok) sonJaEnergizou[k - 1] = true;
    if (!pedidoEraLigar && ok) sonJaEnergizou[k - 1] = false;
  }
  servosLigados = J1.habilitado && J2.habilitado;

  if (e == SON_OK) {
    if (pedidoEraLigar) {
      habilitouAgora = true;
      definirMensagem(servosLigados ? "Servos habilitados"
                                    : "Junta %u habilitada", (unsigned)junta);
    } else {
      definirMensagem(junta ? "Junta %u sem torque" : "Servos desabilitados (sem torque)",
                      (unsigned)junta);
    }
    return false;
  }

  if (e == SON_FALHOU) {
    char motivo[96];
    encoderSonMotivo(motivo, sizeof(motivo));
    if (pedidoEraLigar) {
      // Habilitar que falhou e so um comando que nao pegou: a junta
      // continua sem torque, que e o estado seguro. Diz e segue.
      // Dizer so "nao consegui" manda o operador adivinhar. O caso
      // comum e o driver nao estar respondendo NAQUELE endereco Modbus
      // -- segundo driver ainda na caixa, ou com o endereco de fabrica
      // igual ao do primeiro. E isso que a frase tem de apontar.
      definirMensagem("Nao consegui habilitar: %s. Confira se esse driver "
                      "responde nesse endereco (Ajustes, Encoder)", motivo);
      return false;
    }
    // Desabilitar que falhou tem duas leituras, e so uma e grave.
    //
    // A junta nunca teve torque: e um driver que nao esta no barramento.
    // Nao ha o que cortar. Derrubar a maquina aqui a travava por causa
    // de um motor ausente -- e em FALHA todo comando e recusado,
    // inclusive levar o braco ao zero com o eixo que existe.
    if (!cortePerdidoComTorque) {
      definirMensagem("Sem resposta ao desabilitar: %s. Esse driver nunca foi "
                      "energizado -- confira o endereco dele em Ajustes, "
                      "Encoder, Endereco do driver", motivo);
      return false;
    }
    // A junta TINHA torque e nao respondeu ao corte. Agora sim: o eixo
    // pode estar energizado, o firmware nao sabe, e nao existe segundo
    // caminho para cortar.
    definirMensagem("DESABILITAR NAO CONFIRMOU: %s. "
                    "Corte a potencia dos drivers pelo contator.", motivo);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------
uint32_t grausPorSegParaHz(const Junta& j, float grausPorS) {
  if (grausPorS <= 0.0f || j.passosPorGrau <= 0.0f) return 1;
  return limitarFreq((uint32_t)(grausPorS * j.passosPorGrau));
}

// Ultima velocidade REALMENTE programada em cada gerador.
//
// Existe porque seguirSetpoint() so reprograma quando o valor muda:
// chamar setSpeedInHz a cada milissegundo obriga o gerador a refazer a
// rampa o tempo todo. Mas o cache tem de ser o mesmo para TODO mundo que
// programa velocidade -- com um cache privado dentro de seguirSetpoint,
// um moverCoordenado() no meio deixava a lembranca velha, e o trecho
// seguinte podia rodar na velocidade do deslocamento sem reprogramar
// nada. Toda escrita de velocidade passa por aqui.

static void programarVelocidade(Junta& j, int i, uint32_t hz) {
  if (!j.motor) return;
  const uint32_t v = limitarFreq(hz);
  if (v == velProgramada[i]) return;
  velProgramada[i] = v;
  j.motor->setSpeedInHz(v);
}

// Cada junta recebe a MESMA velocidade angular, convertida com o seu
// proprio passosPorGrau. Antes as duas recebiam o mesmo Hz, e a de menor
// reducao andava varias vezes mais rapido.
// Cada junta anda na velocidade escolhida VEZES o fator dela. Ver
// Junta.fatorVel em estado.h: a que carrega mais nem sempre aguenta a
// velocidade que serve para a outra.
static float velDaJunta(const Junta& j, float base) {
  const float f = (j.fatorVel > 0.01f) ? j.fatorVel : 1.0f;
  const float v = base * f;
  return (v > 0.01f) ? v : 0.01f;
}

void aplicarVelocidadeManual() {
  const float g = velNormal;
  programarVelocidade(J1, 0, grausPorSegParaHz(J1, velDaJunta(J1, g)));
  programarVelocidade(J2, 1, grausPorSegParaHz(J2, velDaJunta(J2, g)));
}

void aplicarAceleracao() {
  if (J1.motor) J1.motor->setAcceleration(grausPorSegParaHz(J1, J1.aceleracao));
  if (J2.motor) J2.motor->setAcceleration(grausPorSegParaHz(J2, J2.aceleracao));
  aplicarSuavidade();
}

// Sobe a aceleracao gradualmente nos primeiros passos em vez de aplicar
// o valor cheio de uma vez. E o degrau de torque da partida que se sente
// como tranco; alongar esse degrau tira o solavanco sem deixar o
// movimento mais lento.
void aplicarSuavidade() {
#if RAMPA_SUAVE_DISPONIVEL
  if (J1.motor) J1.motor->setLinearAcceleration(suavidadePartida);
  if (J2.motor) J2.motor->setLinearAcceleration(suavidadePartida);
#endif
}

// O segundo parametro do FastAccelStepper diz se o nivel ALTO no DIR faz
// a contagem SUBIR. Invertendo aqui, o contador e o braco passam a andar
// para o mesmo lado sem precisar trocar fio no driver.
void aplicarSentido() {
  if (J1.motor) J1.motor->setDirectionPin(J1.pinoDir, !J1.inverterDir);
  if (J2.motor) J2.motor->setDirectionPin(J2.pinoDir, !J2.inverterDir);
}

bool motoresEmMovimento() {
  return (J1.motor && J1.motor->isRunning()) ||
         (J2.motor && J2.motor->isRunning());
}

float velocidadeJ1Hz() {
  return J1.motor ? fabsf((float)J1.motor->getCurrentSpeedInMilliHz()) / 1000.0f : 0.0f;
}
float velocidadeJ2Hz() {
  return J2.motor ? fabsf((float)J2.motor->getCurrentSpeedInMilliHz()) / 1000.0f : 0.0f;
}

long posicaoJ1() { return J1.motor ? J1.motor->getCurrentPosition() : 0; }
long posicaoJ2() { return J2.motor ? J2.motor->getCurrentPosition() : 0; }

void zerarPosicoes() {
  if (J1.motor) J1.motor->setCurrentPosition(0);
  if (J2.motor) J2.motor->setCurrentPosition(0);
}

// Reajusta a CONTAGEM sem mover o eixo. Nenhum pulso sai no fio.
//
// Serve para uma coisa so: depois do assentamento pelo encoder, o eixo
// esta fisicamente no alvo mas a contagem ficou adiantada pelo tanto que
// o retoque andou. Sem este reajuste o desvio nao some -- ele so muda de
// lugar, e o proximo movimento absoluto nasce errado pelo mesmo tanto.
void ajustarContagem(Junta& j, long passos) {
  if (j.motor) j.motor->setCurrentPosition((int32_t)passos);
}

// ---------------------------------------------------------------------
// JOG
// ---------------------------------------------------------------------
void jogDefinir(uint8_t junta, int8_t direcao, float fracao) {
  if (junta != 1 && junta != 2) return;
  const uint8_t i = junta - 1;
  if (fracao < 0.0f) fracao = 0.0f;
  if (fracao > 1.0f) fracao = 1.0f;
  jogDir[i]      = direcao;
  jogFracao[i]   = fracao;
  jogUltimoMs[i] = millis();
}

void jogZerar() {
  jogDir[0] = jogDir[1] = 0;
  jogFracao[0] = jogFracao[1] = 1.0f;
  jogHzAplicado[0] = jogHzAplicado[1] = 0;
  jogSentidoAplicado[0] = jogSentidoAplicado[1] = 0;
  if (J1.motor) J1.motor->stopMove();
  if (J2.motor) J2.motor->stopMove();
}

// Distancia necessaria para frear: v^2/(2a), com a velocidade REAL do
// motor neste instante.
//
// Usar a velocidade maxima aqui (como na v3) reservava a freada de
// velocidade cheia mesmo com o eixo parado, e o jog travava dezenas de
// graus antes do limite. Parado, a reserva e praticamente zero e da
// para encostar no limite passo a passo.
static long distanciaFreada(const Junta& j) {
  if (!j.motor) return 0;
  const int32_t mHz = j.motor->getCurrentSpeedInMilliHz();
  const float v = fabsf((float)mHz) / 1000.0f;              // passos/s
  // A rampa e em graus/s2; a freada se calcula em passos.
  const float aGraus = (j.aceleracao > 0.0f) ? j.aceleracao : ACEL_PADRAO;
  const float a = aGraus * ((j.passosPorGrau > 0.0f) ? j.passosPorGrau : 1.0f);
  if (a <= 0.0f) return 4;
  return (long)(v * v / (2.0f * a)) + 4;
}

void jogAtualizar() {
  const uint32_t agora = millis();

  // Portao de seguranca (ver estado.h): emergencia, conexao,
  // falha. O TORQUE e conferido por eixo logo abaixo -- jog e movimento
  // de um eixo so, e travar a junta 1 porque a 2 esta sem torque impedia
  // de trabalhar numa bancada com um driver ligado.
  if (!movimentoSeguro) {
    if (jogDir[0] != 0 || jogDir[1] != 0) {
      jogZerar();
      definirMensagem("Jog bloqueado: intertravamento de seguranca");
    }
    return;
  }

  for (uint8_t i = 0; i < 2; i++) {
    Junta& j = (i == 0) ? J1 : J2;
    if (!j.motor) continue;
    // Sem torque nesta junta o gerador de pulso continuaria contando
    // passos com o eixo parado, e todo limite de curso passaria a
    // apontar para o lugar errado.
    if (!j.habilitado) {
      if (jogDir[i] != 0) {
        jogDefinir(i + 1, 0, 0.0f);
        definirMensagem("Jog da junta %u bloqueado: habilite o servo dela",
                        (unsigned)(i + 1));
      }
      continue;
    }

    // Heartbeat: se a interface parou de confirmar o jog, o eixo para.
    // Protege contra queda de Wi-Fi com o botao pressionado.
    if (jogDir[i] != 0 && (agora - jogUltimoMs[i] > TIMEOUT_JOG_MS)) {
      jogDir[i] = 0;
    }

    if (jogDir[i] == 0) {
      if (jogSentidoAplicado[i] != 0) {
        jogSentidoAplicado[i] = 0;
        jogHzAplicado[i]      = 0;
        j.motor->stopMove();
      }
      continue;
    }

    // Velocidade proporcional a intensidade do joystick, em GRAUS/s: as
    // duas juntas andam igual, independente da engrenagem de cada uma.
    //
    // So reprograma quando o valor muda de fato. Chamar setSpeedInHz e
    // runForward a cada milissegundo obriga o gerador a refazer a rampa
    // o tempo todo, o que suja o trem de pulsos.
    {
      const float base = velNormal;
      float f = jogFracao[i];
      if (f < JOY_FRACAO_MIN) f = JOY_FRACAO_MIN;
      const uint32_t hz = grausPorSegParaHz(j, velDaJunta(j, base) * f);
      if (hz != jogHzAplicado[i]) {
        jogHzAplicado[i] = hz;
        programarVelocidade(j, i, hz);
        jogSentidoAplicado[i] = 0;    // forca reemitir o sentido
      }
    }

    // Antecipa a postura no fim da freada e para antes de violar.
    const long freada = distanciaFreada(j) * jogDir[i];
    const long f1 = (i == 0) ? posicaoJ1() + freada : posicaoJ1();
    const long f2 = (i == 1) ? posicaoJ2() + freada : posicaoJ2();

    const char* motivo = nullptr;
    if (!posturaValidaPassos(f1, f2, &motivo)) {
      // O destino e invalido. Antes de bloquear, veja se a posicao ATUAL
      // ja e invalida: nesse caso bloquear tudo prenderia o braco fora
      // da area util sem nenhuma saida. Enquanto o movimento reduzir a
      // violacao, ele e liberado - e um jog de recuperacao.
      const float gAtual  = gravidadeViolacaoPassos(posicaoJ1(), posicaoJ2());
      const float gFuturo = gravidadeViolacaoPassos(f1, f2);

      // "Nao piorar" e o criterio, nao "melhorar": para tirar uma junta
      // de fora da area util muitas vezes e preciso mexer na outra
      // primeiro. Qualquer movimento que aumente a violacao continua
      // bloqueado.
      if (!(gAtual > 0.001f && gFuturo <= gAtual + 0.001f)) {
        j.motor->stopMove();
        jogDir[i] = 0;
        definirMensagem("Junta %u bloqueada: %s", (unsigned)(i + 1),
                        motivo ? motivo : "limite");
        continue;
      }
      definirMensagem("Junta %u fora da area util: voltando", (unsigned)(i + 1));
    }

    if (jogSentidoAplicado[i] != jogDir[i]) {
      jogSentidoAplicado[i] = jogDir[i];
      if (jogDir[i] > 0) j.motor->runForward();
      else               j.motor->runBackward();
    }
  }
}

// ---------------------------------------------------------------------
// MOVIMENTO COORDENADO
// ---------------------------------------------------------------------
void moverCoordenado(long alvo1, long alvo2, float grausPorS) {
  // BASTA UM MOTOR. Exigir os dois fazia a maquina de um eixo so nao
  // andar NADA -- nem o eixo que existe --, e sem uma palavra na tela.
  // Coordenar dois movimentos e o caso comum, nao a condicao para haver
  // movimento: com uma junta so, "chegar junto" e chegar.
  if (!J1.motor && !J2.motor) return;

  const long d1 = J1.motor ? labs(alvo1 - posicaoJ1()) : 0;
  const long d2 = J2.motor ? labs(alvo2 - posicaoJ2()) : 0;
  if (d1 == 0 && d2 == 0) return;

  // Quem manda no tempo do movimento e a junta que tem mais GRAUS a
  // percorrer, nao mais passos: com engrenagens diferentes, mais passos
  // pode ser menos angulo.
  const float g1 = (J1.passosPorGrau > 0.0f) ? d1 / J1.passosPorGrau : 0.0f;
  const float g2 = (J2.passosPorGrau > 0.0f) ? d2 / J2.passosPorGrau : 0.0f;
  const float gmax = (g1 > g2) ? g1 : g2;
  if (gmax <= 0.0f) return;

  // O fator de velocidade entra pela junta MAIS RESTRITA, e nao por
  // junta. Aplicar um fator diferente em cada uma faria as duas
  // chegarem em instantes diferentes, e o caminho deixaria de ser reto
  // no espaco das juntas -- que e o que este movimento existe para
  // garantir. Assim o movimento inteiro anda no que a mais lenta
  // aguenta, e as duas continuam chegando junto.
  const float f1 = (J1.fatorVel > 0.01f) ? J1.fatorVel : 1.0f;
  const float f2 = (J2.fatorVel > 0.01f) ? J2.fatorVel : 1.0f;
  const float fMin = (f1 < f2) ? f1 : f2;

  const float pedida = (grausPorS > 0.01f) ? grausPorS : 1.0f;
  const float vel = (pedida * fMin > 0.01f) ? pedida * fMin : 0.01f;
  const float segundos = gmax / vel;          // as duas chegam junto

  const uint32_t v1 = limitarFreq((uint32_t)(d1 / segundos) + 1);
  const uint32_t v2 = limitarFreq((uint32_t)(d2 / segundos) + 1);

  // Aceleracao escalada na mesma proporcao: as duas rampas comecam e
  // terminam juntas, entao o caminho fica reto no espaco das juntas.
  uint32_t a1 = grausPorSegParaHz(J1, J1.aceleracao * (g1 / gmax));
  uint32_t a2 = grausPorSegParaHz(J2, J2.aceleracao * (g2 / gmax));
  if (a1 < 100) a1 = 100;
  if (a2 < 100) a2 = 100;

  if (J1.motor) {
    J1.motor->setAcceleration(a1);
    programarVelocidade(J1, 0, v1);
    J1.motor->moveTo(alvo1);
  }
  if (J2.motor) {
    J2.motor->setAcceleration(a2);
    programarVelocidade(J2, 1, v2);
    J2.motor->moveTo(alvo2);
  }
}

void seguirSetpoint(long alvo1, long alvo2, uint32_t vel1, uint32_t vel2) {
  if (!J1.motor && !J2.motor) return;
  if (J1.motor) { programarVelocidade(J1, 0, vel1); J1.motor->moveTo(alvo1); }
  if (J2.motor) { programarVelocidade(J2, 1, vel2); J2.motor->moveTo(alvo2); }
}

// ---------------------------------------------------------------------
void pararSuave() {
  jogDir[0] = jogDir[1] = 0;
  if (J1.motor) J1.motor->stopMove();
  if (J2.motor) J2.motor->stopMove();
}

void pararEmergencia() {
  // Ordem importa: primeiro o arco, depois o movimento.
  soldaDesligar();
  pararSuave();
  aplicarAceleracao();
  aplicarVelocidadeManual();
  // FALHA nao se limpa com uma parada: quem rearma e CMD_SERVOS.
  if (modoAtual != MODO_FALHA) modoAtual = MODO_MANUAL;
  definirMensagem("PARADA: movimento interrompido e solda desligada");
}
