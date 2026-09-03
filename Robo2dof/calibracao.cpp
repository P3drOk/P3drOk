#include "calibracao.h"
#include "estado.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include "encoder.h"
#include "correcao.h"   // aferirEngrenagem(): a conta e a mesma dos dois lados
#include <math.h>   // fabsf/lroundf das contas de curso

bool calibAtiva() { return estadoCalib != CAL_INATIVO; }

// =====================================================================
//  CALIBRAR E MARCAR DOIS LIMITES. SO ISSO.
//
//   1. Toca em Calibrar -> a maquina SOLTA os motores. Na hora.
//   2. O operador empurra o braco com a mao ate o extremo NEGATIVO --
//      os dois eixos de uma vez -- e toca em Salvar.
//   3. Empurra ate o extremo POSITIVO e toca em Salvar.
//   4. A maquina grava os limites, religa o torque, espera, e volta ao
//      zero (se o zero estiver dentro do que foi medido).
//
//  O QUE ESTA CALIBRACAO NAO FAZ MAIS, e por que:
//
//  * NAO leva o braco ao zero antes de comecar. Aquela viagem LIGAVA o
//    torque no instante em que o operador tocava em "Calibrar" -- ele
//    pedia para soltar o braco e a maquina o segurava. Era o oposto do
//    que a tela promete.
//
//  * NAO mexe na ESCALA DO ENCODER (contagensPorGrau). Ela media a
//    escala entre os dois extremos e a adotava. Como grausMin/grausMax
//    saem dos passos DIVIDIDOS pela escala, os limites recem-marcados
//    passavam a descrever outros angulos: marcar 90 e 270 e depois ver
//    91 ser recusado como fora de curso e exatamente isso.
//
//  * NAO mede os PULSOS POR VOLTA. Mesma historia, mesma regua.
//
//  * NAO mexe no grausHome. Ele e o deslocamento entre a contagem e o
//    angulo que a maquina informa; zera-lo desloca TODOS os angulos de
//    uma vez.
//
//  * NAO estica o intervalo para incluir o zero. Se o curso vai de 90 a
//    270, o curso e 90 a 270 -- e o zero fica fora, o que a ida ao zero
//    ja sabe dizer com todas as letras. Esticar inventava 90 graus de
//    percurso que o ferro nao tem.
//
//  O que ela grava, e so isto: passosMin e passosMax de cada junta.
//  Angulo, escala e origem ficam como estavam. Desenho, programa e
//  cordao continuam trabalhando com a mesma regua de antes.
//
//  Por que soltar em vez de mandar de jog: batente se alcanca melhor com
//  a mao. Com o motor solto o operador SENTE o fim do curso; com torque
//  ele empurra o eixo contra o ferro e so descobre pelo barulho.
//
//  Por que os dois eixos juntos: com o braco solto, os dois estao soltos.
// =====================================================================
static long    marcaA[2] = {0, 0}, marcaB[2] = {0, 0};
static bool    temA[2]   = {false, false}, temB[2] = {false, false};
// A leitura crua do encoder em cada marca SAIU. Ela existia para medir
// contagensPorGrau entre os dois extremos -- a medida que reescrevia os
// angulos por baixo dos limites. Sem ela, nada aqui le o encoder para
// mudar escala: a calibracao grava passo, e so.

// true quando as marcas viraram curso: quem le e o passo final, que so
// leva o braco de volta ao zero se houve o que medir.
static bool medido = false;

// A viagem final ja foi mandada. Sem isto, cada ciclo em que o motor
// ainda nao arrancou mandaria moveTo() de novo e a rampa recomecaria.
static bool voltandoAoZero = false;

// Ultima leitura vista pelo seguimento a mao (puxarPelaMao, la embaixo).
//
// FICA AQUI EM CIMA porque calibIniciar() precisa limpar. calibAtualizar()
// so e chamado no modo CALIBRANDO: fora dele estes dois guardavam a
// leitura da calibracao ANTERIOR, e no primeiro ciclo com o motor solto a
// diferenca entre aquele numero velho e o de agora entrava na contagem de
// uma vez -- dezenas de milhares de graus num salto.
static int32_t ultimoBruto[2] = {0, 0};
static bool    temUltimo[2]   = {false, false};

uint8_t calibEixoAtivo() {
  // Os dois eixos andam juntos agora: nao ha "eixo da vez". 3 = os dois.
  return (estadoCalib == CAL_INATIVO) ? 0 : 3;
}

// A primeira parada e a unica em que nada foi medido ainda: e ali, e so
// ali, que trocar o sentido de um eixo nao contradiz uma marca ja feita.
bool calibNaPrimeiraEtapa() { return estadoCalib == CAL_LADO_A; }

// ---------------------------------------------------------------------
// O HABILITA E ASSINCRONO, E ESPERAR POR ELE PRENDE O NUCLEO.
//
// Desde que o SON virou Modbus, ligar e desligar torque e um pedido que
// a tarefa do encoder atende no outro nucleo. Chamar encoderSonEsperar()
// aqui travaria o nucleo 1 justamente enquanto o outro precisa dele -- e
// o proprio encoder.h avisa isso. Entao a calibracao PEDE e volta; o
// calibAtualizar() olha o estado a cada ciclo e segue quando ele assenta.
// ---------------------------------------------------------------------
static bool     esperandoSon   = false;
static bool     esperandoLigar = false;
static uint32_t prazoSon       = 0;

static void pedirTorque(bool ligar) {
  servosHabilitar(ligar, 0);
  esperandoSon   = true;
  esperandoLigar = ligar;
  prazoSon       = millis() + SON_PRAZO_MS;
}

// 0 = ainda esperando, 1 = confirmou, -1 = desistiu.
static int8_t torqueAssentou() {
  if (!esperandoSon) return 1;
  const uint8_t e = encoderSonEstado();
  if (e == SON_OK)     { esperandoSon = false; return  1; }
  if (e == SON_FALHOU) { esperandoSon = false; return -1; }
  if ((int32_t)(millis() - prazoSon) >= 0) { esperandoSon = false; return -1; }
  return 0;
}

// Manda os dois eixos para o zero da contagem.
//
// So anda com torque de verdade: gerar pulso para um eixo solto encheria
// a contagem de passos que o braco nao deu, que e o oposto do que esta
// calibracao existe para fazer. E passa pelo mesmo portao de seguranca
// que todo o resto.
static bool irAoZero() {
  if (!movimentoSeguro) return false;
  if (!J1.habilitado && !J2.habilitado) return false;
  // PELA PORTA, e nao por setSpeedInHz() direto.
  //
  // Escrever no gerador por fora deixa o cache de motores.cpp mentindo:
  // ele guarda o ultimo Hz REALMENTE programado e pula a escrita quando o
  // valor nao mudou. Depois de uma volta ao zero feita por fora, o
  // primeiro movimento que pedisse por acaso o valor cacheado era
  // DESCARTADO, e o eixo andava numa velocidade que ninguem pediu. E o
  // mesmo defeito que motores.h descreve no comentario de
  // programarVelocidade(), e que ja custou uma bancada.
  if (J1.habilitado && J1.motor) {
    programarVelocidadePub(J1, 0, grausPorSegParaHz(J1, velAuto));
    J1.motor->moveTo(0);
  }
  if (J2.habilitado && J2.motor) {
    programarVelocidadePub(J2, 1, grausPorSegParaHz(J2, velAuto));
    J2.motor->moveTo(0);
  }
  return true;
}

// ---------------------------------------------------------------------
// A MEDIDA DE PULSOS POR VOLTA SAIU DAQUI.
//
// Ela era colhida durante as voltas ao zero -- e as voltas ao zero
// sairam. Mas o motivo de fundo e outro, e vale escrever: a medida
// ADOTAVA o valor e refazia a resolucao, e refazer a resolucao no fim de
// uma calibracao muda o angulo que os limites recem-marcados descrevem.
// Marcar 90 e 270 e depois ver 91 recusado como fora de curso era isso.
//
// A conta continua existindo em aferirEngrenagem() (correcao.h), onde
// hoje ela MEDE e guarda como sugestao, sem escrever a regua. Quem
// escreve a regua da maquina e uma pessoa, no campo de Ajustes.
// ---------------------------------------------------------------------

// ---------------------------------------------------------------------
void calibIniciar() {
  soldaDesligar();
  pararSuave();
  jogZerar();

  esperandoSon = false;
  medido       = false;
  for (uint8_t i = 0; i < 2; i++) {
    temA[i] = temB[i] = false;
    temUltimo[i] = false;
  }
  voltandoAoZero = false;

  // Enquanto nao houver calibracao nova, a protecao de curso fica
  // desativada de proposito: e o operador que esta definindo os limites,
  // e proteger com os limites velhos o impediria de chegar nos novos.
  J1.calibrada = false;
  J2.calibrada = false;

  modoAtual   = MODO_CALIBRANDO;
  estadoCalib = CAL_SOLTANDO;
  // SOLTAR, e nao ligar. O operador tocou em Calibrar para poder empurrar
  // o braco com a mao: ligar o torque aqui era segurar o braco na cara
  // dele. A viagem ao zero que existia antes deste ponto e o que fazia
  // isso -- e ela nao volta.
  pedirTorque(false);
  aplicarVelocidadeManual();
  definirMensagem("Soltando os motores. Leve o braco ao extremo NEGATIVO "
                  "e toque em Salvar");
}

void calibCancelar() {
  pararSuave();
  jogZerar();
  esperandoSon   = false;
  voltandoAoZero = false;
  estadoCalib  = CAL_INATIVO;
  modoAtual    = MODO_MANUAL;

  carregarConfiguracoes();   // restaura a calibracao anterior

  // O zero nao se move nesta calibracao, entao nao ha origem a desfazer.
  aplicarVelocidadeManual();
  aplicarAceleracao();
  definirMensagem("Calibracao cancelada");
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
    temA[i] = temB[i] = false;
  }
  estadoCalib = CAL_INATIVO;
  if (modoAtual == MODO_CALIBRANDO) modoAtual = MODO_MANUAL;

  recalcularResolucao();
  salvarConfiguracoes();
  aplicarVelocidadeManual();
  aplicarAceleracao();

  Serial.println("[CAL] Curso apagado do NVS.");
  // Sem limites a maquina NAO para de funcionar: ela so deixa de ter
  // protecao de curso -- que, alias, nasce desligada.
  definirMensagem("Curso apagado. A maquina segue operando, sem limite");
}

// ---------------------------------------------------------------------
// Grava as marcas de um lado: onde CADA junta esta agora, em passos, e o
// que o encoder estava lendo no mesmo instante.
// ---------------------------------------------------------------------
static void gravarLado(bool ladoA) {
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    const long pos = (k == 1) ? posicaoJ1() : posicaoJ2();
    if (ladoA) { marcaA[i] = pos; temA[i] = true; }
    else       { marcaB[i] = pos; temB[i] = true; }
  }
}

// ---------------------------------------------------------------------
// Fecha uma junta a partir dos dois extremos dela.
// ---------------------------------------------------------------------
// Por que a junta nao fechou. Cada uma vira uma frase propria na tela:
// "nao deu" sem dizer o que fazer manda o operador tentar de novo do
// mesmo jeito.
enum MotivoCurso : uint8_t {
  CURSO_OK = 0,
  CURSO_SEM_MARCA,    // faltou salvar um dos dois extremos
  CURSO_SEM_REGUA,    // resolucao invalida: nem da para converter em graus
  CURSO_IGUAL,        // o positivo caiu no mesmo lugar do negativo
  CURSO_CURTO         // andou, mas menos que o minimo
};

static MotivoCurso fecharJunta(Junta& j, uint8_t k) {
  const uint8_t i = k - 1;
  if (!temA[i] || !temB[i])   return CURSO_SEM_MARCA;
  if (j.passosPorGrau <= 0.0f) return CURSO_SEM_REGUA;

  long lo = marcaA[i], hi = marcaB[i];
  if (lo > hi) { const long t = lo; lo = hi; hi = t; }

  // Os dois extremos no mesmo lugar: o operador salvou duas vezes sem
  // mover o braco. Merece frase propria -- "curso curto" mandaria ele
  // empurrar mais, quando o que faltou foi empurrar.
  if (hi == lo) return CURSO_IGUAL;

  const float curso = (float)(hi - lo) / j.passosPorGrau;
  if (curso < CURSO_MINIMO_GRAUS) return CURSO_CURTO;

  // OS LIMITES SAO OS DOIS EXTREMOS. Nada mais se escreve aqui.
  //
  // A versao anterior esticava o intervalo ate o zero (`if (lo>0) lo=0`),
  // zerava grausHome e adotava a escala do encoder medida entre as duas
  // marcas. As tres coisas mexiam no ANGULO -- e o operador que acabou de
  // marcar 90 e 270 via 91 ser recusado como fora de curso.
  //
  // Se o zero cair fora do curso, quem sabe dizer isso e a ida ao zero,
  // que ja recusa com todas as letras. Inventar percurso que o ferro nao
  // tem e pior do que uma recusa clara.
  j.passosMin = lo;
  j.passosMax = hi;
  j.calibrada = true;
  return CURSO_OK;
}

// A frase de cada recusa, com o que fazer. Uma junta que nao fechou NAO
// perde o que ja tinha: fecharJunta() sai antes de escrever qualquer
// coisa, entao a calibracao boa que estava la continua valendo.
static const char* frasePorque(MotivoCurso m) {
  switch (m) {
    case CURSO_SEM_MARCA:
      return "faltou salvar um dos dois extremos";
    case CURSO_SEM_REGUA:
      return "a resolucao desta junta e invalida: confira pulsos por volta "
             "e reducao em Ajustes";
    case CURSO_IGUAL:
      return "o extremo positivo caiu no mesmo lugar do negativo: mova o "
             "braco antes de salvar o segundo";
    case CURSO_CURTO:
      return "o curso ficou curto demais: leve o braco ate os batentes de "
             "verdade";
    default:
      return "";
  }
}

static void concluir() {
  medido = false;
  const MotivoCurso m1 = fecharJunta(J1, 1);
  const MotivoCurso m2 = fecharJunta(J2, 2);
  const bool ok1 = (m1 == CURSO_OK), ok2 = (m2 == CURSO_OK);

  if (!ok1 && !ok2) {
    estadoCalib = CAL_INATIVO;
    modoAtual   = MODO_MANUAL;
    aplicarVelocidadeManual();
    aplicarAceleracao();
    // As duas falharam: se foi pelo mesmo motivo, uma frase basta.
    if (m1 == m2)
      definirMensagem("Nada gravado nas duas juntas -- %s", frasePorque(m1));
    else
      definirMensagem("Nada gravado. Junta 1: %s. Junta 2: %s",
                      frasePorque(m1), frasePorque(m2));
    return;
  }
  medido = true;

  // Converte os limites em graus pela regua QUE JA ESTAVA VALENDO.
  // recalcularResolucao() nao mexe em passosPorVolta nem em reducao: ela
  // so refaz passosPorGrau e projeta passosMin/Max em grausMin/Max.
  recalcularResolucao();

  salvarConfiguracoes();
  aplicarVelocidadeManual();
  aplicarAceleracao();

  // O que a calibracao mede NAO liga a protecao sozinha: o braco continua
  // livre, e o limite e coisa que o operador liga quando quiser.
  if (ok1 && ok2)
    definirMensagem("Limites gravados: J1 %.1f a %.1f, J2 %.1f a %.1f graus. "
                    "Os angulos nao mudaram",
                    J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax);
  else
    definirMensagem("Junta %u gravada: %.1f a %.1f graus. A junta %u ficou "
                    "como estava -- %s",
                    ok1 ? 1u : 2u,
                    ok1 ? J1.grausMin : J2.grausMin,
                    ok1 ? J1.grausMax : J2.grausMax,
                    ok1 ? 2u : 1u,
                    frasePorque(ok1 ? m2 : m1));
}

// ---------------------------------------------------------------------
// Um toque so, que quer dizer coisas diferentes conforme onde se esta.
// Os dois numeros continuam na assinatura porque a fila de comandos os
// carrega, mas nenhum e usado: a calibracao nao pergunta nada.
void calibConfirmar(float, float) {
  switch (estadoCalib) {
    case CAL_LADO_A:
      jogZerar(); pararSuave();
      gravarLado(true);
      // SEGUE SOLTO, e sem viagem nenhuma. O braco ja esta num extremo;
      // levar de volta ao zero para depois pedir o outro extremo era uma
      // viagem inteira sem nada para medir. O operador empurra dali
      // mesmo para o outro lado.
      estadoCalib = CAL_LADO_B;
      definirMensagem("Extremo negativo gravado. Agora leve o braco ao "
                      "extremo POSITIVO e toque em Salvar");
      break;

    case CAL_LADO_B:
      jogZerar(); pararSuave();
      gravarLado(false);
      concluir();
      if (medido) {
        // Religa o torque e volta ao zero. Terminar a calibracao com a
        // maquina largada no batente e sem torque seria devolve-la pior
        // do que se pegou.
        estadoCalib = CAL_RELIGANDO;
        pedirTorque(true);
      }
      break;

    default:
      // Enquanto o torque esta assentando o botao nao faz nada: a marca
      // sairia de uma contagem que ainda esta se acertando.
      break;
  }
}

// ---------------------------------------------------------------------
// CALIBRAR COM OS MOTORES SOLTOS
//
// O jeito mais seguro de chegar num batente e com o motor sem torque,
// empurrando o braco com a mao. So que ai o gerador de pulso nao anda, e
// a contagem -- que e o que a marca grava -- ficaria parada nos extremos.
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

// ---------------------------------------------------------------------
// As tres viagens ao zero sao da MAQUINA. Cada uma tem tres tempos --
// esperar o torque confirmar, andar, e (nas duas primeiras) soltar de
// novo -- todos sem prender o nucleo, olhando o estado a cada ciclo.
// ---------------------------------------------------------------------
void calibAtualizar() {
  if (modoAtual != MODO_CALIBRANDO) { temUltimo[0] = temUltimo[1] = false; return; }

  // ---- soltando os motores, logo depois do toque em Calibrar ----
  if (estadoCalib == CAL_SOLTANDO) {
    temUltimo[0] = temUltimo[1] = false;
    const int8_t r = torqueAssentou();
    if (r == 0) return;                       // ainda escrevendo no barramento
    // Mesmo sem confirmacao do barramento a calibracao segue: o operador
    // ainda pode levar o braco ao batente com a mao, e recusar aqui o
    // deixaria sem nenhum caminho para calibrar.
    estadoCalib = CAL_LADO_A;
    definirMensagem(r == 1
        ? "Motores soltos. Leve o braco ao extremo NEGATIVO e toque em Salvar"
        : "Sem confirmacao de torque no barramento. Se o braco estiver "
          "solto, leve-o ao extremo NEGATIVO e toque em Salvar");
    return;
  }

  // ---- limites gravados: religa, espera, e vai ao zero ----
  if (estadoCalib == CAL_RELIGANDO) {
    temUltimo[0] = temUltimo[1] = false;

    if (esperandoSon) {
      const int8_t r = torqueAssentou();
      if (r == 0) return;
      if (r != 1) {
        estadoCalib = CAL_INATIVO;
        modoAtual   = MODO_MANUAL;
        definirMensagem("Limites gravados. Sem torque confirmado: leve o "
                        "braco de volta quando puder");
        return;
      }
      // O DRIVER CONFIRMOU, O ROTOR AINDA NAO SEGUROU.
      // Entre uma coisa e outra ha um intervalo, e mandar pulso dentro
      // dele e mandar pulso para um eixo solto: a contagem anda e o
      // braco nao. Ver CAL_ESPERA_RELIGAR_MS em config.h.
      prazoSon = millis() + CAL_ESPERA_RELIGAR_MS;
      esperandoSon = false;
      esperandoLigar = false;
      return;
    }

    // Ainda dentro da espera nomeada.
    if ((int32_t)(millis() - prazoSon) < 0) return;

    if (!motoresEmMovimento() && !voltandoAoZero) {
      if (!irAoZero()) {
        estadoCalib = CAL_INATIVO;
        modoAtual   = MODO_MANUAL;
        aplicarVelocidadeManual();
        aplicarAceleracao();
        definirMensagem("Limites gravados. Nao consegui voltar ao zero: "
                        "leve o braco quando puder");
        return;
      }
      voltandoAoZero = true;
      return;
    }

    if (motoresEmMovimento()) return;

    // Chegou.
    voltandoAoZero = false;
    estadoCalib = CAL_INATIVO;
    modoAtual   = MODO_MANUAL;
    aplicarVelocidadeManual();
    aplicarAceleracao();
    definirMensagem("Calibrado. J1 %.1f a %.1f, J2 %.1f a %.1f graus -- "
                    "ligue o limite em Ajustes para ele valer",
                    J1.grausMin, J1.grausMax, J2.grausMin, J2.grausMax);
    return;
  }

  // ---- parado num extremo, motores soltos: a mao e que anda ----
  if (motoresEmMovimento()) return;
  puxarPelaMao(1);
  puxarPelaMao(2);
}
