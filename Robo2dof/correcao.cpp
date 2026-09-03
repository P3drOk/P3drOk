#include "correcao.h"
#include "encoder.h"
#include "motores.h"
#include "cinematica.h"
#include "solda.h"
#include "armazenamento.h"
#include <string.h>
#include <math.h>

// Tudo aqui roda no CORE 1, junto com o motor. E a regra de ouro do
// projeto: quem manda no motor e um nucleo so.

static ResumoCorrecao r;
static uint32_t esperaAte = 0;
// Onde o movimento pediu para parar. O retoque anda ALEM disso para levar
// o eixo ao lugar certo; no fim a contagem volta para ca, senao o desvio
// nao some -- so muda de lugar.
//
// GUARDADO EM GRAUS, e nao so em passos. O alvo tem de significar a mesma
// POSTURA do comeco ao fim do assentamento, e passo nao e uma unidade
// estavel: a maquina afere a propria engrenagem (ver aferirEngrenagem
// abaixo), e quando ela corrige a regua no meio do caminho, um mesmo
// numero de passos passa a descrever outro angulo. Recalcular o alvo a
// partir da contagem a cada ciclo movia o alvo junto com a regua -- o
// braco perseguiria um destino que anda.
//
// A contagem original fica junto so como rede: com regua invalida
// (passosPorGrau <= 0) nada se move mesmo, e devolver a contagem exata e
// mais seguro do que converter por uma escala que nao existe.
static long  alvo1Original = 0, alvo2Original = 0;
static float alvoGraus1 = 0.0f, alvoGraus2 = 0.0f;

// O ANGULO QUE O OPERADOR PEDIU, guardado quando o movimento comeca.
//
// Nao e a mesma coisa que converter a contagem de destino pela regua:
// com a regua errada os dois divergem, e quem manda e o que foi pedido.
// E dele que sai o freio do encoder.
static float  alvoPedido[2]  = {0.0f, 0.0f};
// A BUSCA: ir ao angulo e andar ate a medida bater.
//
// Nao ha destino em passos, nao ha rampa planejada, nao ha conta de
// distancia de frenagem. O eixo anda em velocidade CONSTANTE no sentido
// do alvo e para quando o encoder diz o numero -- foi o que a bancada
// pediu, depois de quatro rodadas de conta cada vez mais elaborada
// errando o ponto:
//
//   "So quero que ele acerte. O braco tem a leitura do encoder, entao e
//    so mover o braco em uma velocidade constante ate que a leitura do
//    encoder seja a mesma."
//
// Quem anda o eixo e o JOG, com o encoder no lugar do dedo: dali vem o
// portao de seguranca, o torque por eixo e a antecipacao da postura no
// fim da freada.
static bool     buscando[2]      = {false, false};
static int8_t   buscaSentido[2]  = {0, 0};
static float    buscaVel[2]      = {0.0f, 0.0f};   // graus/s comandados
static uint8_t  buscaPassadas[2] = {0, 0};
static uint32_t buscaPrazo[2]    = {0, 0};
// Depois de mandar parar, o eixo ainda anda o que a rampa de parada
// levar. Por isso a busca tem duas fases: ANDANDO ate a medida bater, e
// PARANDO ate o eixo assentar e uma leitura nova dizer onde ele ficou.
// So entao se decide se chegou ou se vai de novo, mais devagar.
static bool     buscaParando[2]     = {false, false};
static uint32_t buscaContaParada[2] = {0, 0};
static uint32_t buscaPrazoParada[2] = {0, 0};
// Esta junta foi levada pela BUSCA nesta viagem.
//
// O assentamento nao toca nela: a busca ja e malha fechada no encoder --
// ela so para quando a medida diz o numero, e absorve escorregao no
// caminho. Retocar depois seria uma segunda malha discutindo com a
// primeira, que e de onde vinha o vai-e-vem.
//
// O assentamento continua inteiro para o que a busca NAO levou: pontos
// de programa, maquina sem encoder, junta cuja leitura nao servia na
// hora de largar.
static bool     buscouEstaViagem[2] = {false, false};

// AS DUAS PONTAS SO VALEM COM O EIXO PARADO.
//
// Uma leitura de 217 ms atras conta onde a junta ESTAVA. Com o eixo
// parado isso nao importa -- parado, o angulo de 217 ms atras e o de
// agora. Com o eixo andando a 48 graus/s, importa dez graus.
//
// Emparelhar a contagem de passos de AGORA com um angulo de 217 ms atras
// erra o percurso nos dois sentidos ao mesmo tempo: a regua parece ter
// andado menos e o encoder parece ter andado mais. Foi assim que a
// maquina "aprendeu" que a regua era oito vezes maior quando ela era
// quatro, e o braco passou a andar a 6,6 graus/s com 12 pedidos.
//
// Por isso a ponta de partida e tomada em correcaoAlvoPedido(), com o
// eixo ainda parado, e a de chegada so depois que uma leitura NOVA
// chegou com o eixo ja parado.
static float largadaGraus[2]  = {0.0f, 0.0f};
static long  largadaPassos[2] = {0, 0};
static bool  largadaValida[2] = {false, false};
static uint32_t contaNaParada[2] = {0, 0};
static bool     esperandoParada[2] = {false, false};
static uint32_t prazoAprender[2]   = {0, 0};

// O FATOR PERSISTE ENTRE OS MOVIMENTOS.
//
// Ele mede o quanto a regua da maquina exagera, e isso e propriedade da
// MAQUINA, nao do movimento. Guardado, a largada seguinte ja sai no
// ritmo certo -- inclusive quando a primeira leitura do movimento falha,
// que num barramento com 4% de falhas acontece.
static float govFator[2] = {1.0f, 1.0f};
// Se ja houve uma viagem longa o bastante para medir. Antes dela o 1,0
// acima nao e conhecimento, e so a suposicao de que a regua digitada
// esta certa -- e a primeira medida de verdade vale mais do que uma
// suposicao, entao ela entra inteira em vez de pela metade.
static bool govAprendeu[2] = {false, false};
static bool   temAlvoPedido  = false;

// Depois que o eixo para, a ultima leitura do encoder ainda e de antes
// da parada. O ciclo le a 20 Hz e o valor leva um tempo para assentar --
// retocar em cima de leitura de meio segundo atras e mover o braco
// baseado em onde ele estava.
static const uint32_t ESPERA_ASSENTAR_MS = 250;

static void dizer(const char* m) {
  strncpy(r.motivo, m, sizeof(r.motivo) - 1);
  r.motivo[sizeof(r.motivo) - 1] = '\0';
}

// ---------------------------------------------------------------------
// Quanto falta para o eixo chegar no ALVO, em graus, SE der para confiar.
//
// Repare que NAO se usa L.erro (comandado menos medido). O comandado sai
// da contagem de passos, e a contagem ANDA JUNTO com o retoque: medir
// contra ela daria sempre a mesma diferenca, o retoque nunca fecharia, e
// o eixo iria embora um pouco a cada tentativa. O alvo, ao contrario,
// esta parado -- e e nele que o operador quer o braco.
// ---------------------------------------------------------------------
static bool faltaPara(uint8_t junta, float alvoGraus, float& falta) {
  // NAO exige curso medido. O encoder mede a junta com ou sem
  // calibracao, e exigi-la aqui era o mesmo defeito de R143: uma junta
  // sem curso -- ou com um curso pela metade -- ficava sem assentamento
  // nenhum, e o braco parava onde o erro deixasse. leituraConfiavel()
  // cobre registrador, validade, idade e possibilidade fisica.
  if (!leituraConfiavel(junta)) return false;

  const LeituraEncoder L = encoderLer(junta);
  falta = alvoGraus - L.graus;   // graus da junta que faltam andar
  return true;
}

// Teto absoluto de retoques. Nao e criterio de qualidade -- quem julga
// isso e o progresso, logo abaixo. E so a garantia de que nenhum defeito
// novo consegue prender o core 1 num laco.
static const uint8_t RETOQUES_MAXIMOS = 40;

// Acima disto o retoque anda a meia velocidade; abaixo, proporcional.
static const float GRAUS_VEL_CHEIA = 3.0f;

// Maior |erro| do ciclo anterior, e quantos ciclos seguidos nao
// aproximaram. E o que separa "esta convergindo" de "esta martelando".
static float   erroAnterior = 0.0f;
static uint8_t semProgresso = 0;
// Quanto o passo anterior tinha COMO fechar, em graus medidos. E a regua
// contra a qual o progresso e julgado -- ver a Regra 6 la embaixo.
static float   fechavelAnterior = 0.0f;

// =====================================================================
//  O RETOQUE APRENDE A ESCALA DA PROPRIA MAQUINA
//
//  O retoque anda em PULSOS: graus de erro viram passos por
//  passosPorGrau, que sai do catalogo (pulsos por volta x reducao). O
//  erro, porem, e medido em graus do ENCODER. Quando as duas reguas
//  discordam -- e discordar e o normal antes de calibrar --, pedir "ande
//  2 graus" faz o eixo andar 4, ou 1. Ai o retoque passa do ponto,
//  volta passando do ponto de novo, e o assentamento desiste dizendo que
//  nao aproxima. Era o "chega perto, passa, e nao tem ajuste que
//  conserte".
//
//  Nao ha por que adivinhar essa razao: o retoque anterior a mede. Foi
//  comandado tanto, o encoder andou tanto -- a divisao e o ganho real da
//  maquina, do jeito que ela esta agora. O proximo retoque ja sai
//  dividido por ele e cai no ponto.
//
//  O ganho e propriedade da MAQUINA, nao de um movimento: sobrevive de
//  um posicionamento para o outro, entao o segundo ja nasce certo.
// =====================================================================
static float ganhoRetoque[2]   = {1.0f, 1.0f};
static bool  ganhoAprendido[2] = {false, false};
static float retoquePedido[2]  = {0.0f, 0.0f};   // graus comandados
static float medidoAntes[2]    = {0.0f, 0.0f};   // encoder antes do retoque
static bool  temMedidoAntes[2] = {false, false};

// Enquanto o ganho nao foi medido, o retoque sai amortecido: passar do
// ponto custa outra viagem, e chegar em dois passos curtos e melhor que
// bater e voltar.
static const float AMORTECIMENTO_SEM_GANHO = 0.7f;
// Faixa em que um ganho medido e crivel. Fora dela a medida veio de
// ruido -- retoque curto demais, leitura tremida -- e nao vira regua.
static const float GANHO_MIN = 0.15f;
static const float GANHO_MAX = 6.0f;

static void esquecerGanho() {
  ganhoRetoque[0] = ganhoRetoque[1] = 1.0f;
  ganhoAprendido[0] = ganhoAprendido[1] = false;
  retoquePedido[0] = retoquePedido[1] = 0.0f;
  temMedidoAntes[0] = temMedidoAntes[1] = false;
}

// Limita a MAGNITUDE de um retoque, guardando o sinal.
static float comTeto(float v, float teto) {
  if (v >  teto) return  teto;
  if (v < -teto) return -teto;
  return v;
}

// =====================================================================
//  A MAQUINA AFERE A PROPRIA ENGRENAGEM
//
//  `passosPorGrau = passosPorVolta x reducao / 360`. Os dois sao
//  DIGITADOS, e o mais errado dos dois costuma ser o primeiro: pulsos por
//  volta e parametro do DRIVER, muda quando alguem troca o drive ou refaz
//  uma configuracao, e nada na tela denuncia. O sintoma e o braco andar
//  mais (ou menos) do que a tela diz -- e, no fim de todo movimento,
//  passar do angulo pedido pelo mesmo fator, sempre.
//
//  E POR QUE A REDUCAO NAO ENTRA NESTA CONTA: o encoder esta no eixo do
//  MOTOR, antes do redutor. Pulso e contagem estao os dois do mesmo lado
//  dele, entao a reducao cancela:
//
//      passosPorVolta = |pulsos| x contagensPorVolta / |contagens|
//
//  Isso importa mais do que parece. Quer dizer que a maquina consegue
//  acertar a regua do movimento SEM calibracao guiada, sem transferidor e
//  sem saber a reducao -- que e justamente a situacao de quem so ligou a
//  maquina e mandou ir a um angulo. A reducao errada desloca as duas
//  leituras JUNTO (o encoder tambem divide por ela), entao o braco
//  continua chegando onde o operador pediu na escala que a tela mostra.
//
//  A conta nasceu na calibracao guiada, na viagem ao zero. So que essa
//  viagem so acontece em uma calibracao -- e a maquina do relato nunca
//  fez nenhuma. Agora ela e feita tambem no fim de qualquer movimento
//  comum, que e a mesma medida com outro nome: pulso contado de um lado,
//  voltas do motor do outro.
// =====================================================================
bool aferirEngrenagem(uint8_t junta, long dPasso, int32_t dCont) {
  if (junta != 1 && junta != 2) return false;
  const uint8_t i = junta - 1;
  Junta& j = (junta == 1) ? J1 : J2;

  const float cv = configEncoder.contagensPorVolta[i];
  if (cv < 1.0f) return false;

  const long  passos = labs(dPasso);
  const float voltas = fabsf((float)dCont) / cv;
  if (voltas < AFERIR_VOLTAS_MIN || passos < AFERIR_PASSOS_MIN) return false;

  const long novo = lroundf((float)passos / voltas);
  // Fora desta faixa nao e engrenagem: e leitura estragada, e obedecer a
  // ela reescreveria a regua da maquina inteira com lixo.
  if (novo < 100 || novo > 2000000L) return false;
  if ((uint32_t)novo == j.passosPorVolta) return false;

  const uint32_t velho = j.passosPorVolta;
  const float    rel   = velho ? fabsf((float)novo - (float)velho) / (float)velho
                               : 1.0f;

  // ELA MEDE E GUARDA. NINGUEM ADOTA SOZINHO.
  //
  // Aqui ela escrevia `j.passosPorVolta = novo; recalcularResolucao();`
  // -- no fim de cada movimento, DENTRO do ciclo de assentamento. E dai
  // vinha o "passa do ponto e nunca chega".
  //
  // O alvo do assentamento e congelado EM GRAUS quando o movimento
  // comeca (alvoGraus1/2, la em cima). Trocar passosPorGrau depois disso
  // nao move o numero -- move o LUGAR que aquele numero descreve. O
  // retoque entao levava o braco para um angulo que ninguem pediu.
  //
  // O momento certo de adotar e o contrario deste: com o braco parado e
  // NENHUM alvo congelado, logo antes de o proximo movimento ser
  // calculado. Ai a regua e o destino nascem juntos e valem o movimento
  // inteiro.
  //
  j.ppvMedido = (uint32_t)novo;
  logEvento("junta %u: engrenagem MEDIDA em %ld pulsos por volta "
            "(configurada %lu) -- %ld pulsos em %.3f voltas do motor",
            (unsigned)junta, novo, (unsigned long)velho, passos, (double)voltas);
  (void)rel;
  return false;   // adotar nao e aqui
}

// =====================================================================
//  O FREIO DO ENCODER
//
//  "Se o encoder diz 3 graus, ele esta em 3 graus." Entao o movimento
//  nao tem por que continuar depois que o encoder diz que chegou --
//  qualquer que seja a regua com que os pulsos foram calculados.
//
//  Isto NAO e malha fechada de servo: nao se corrige o eixo enquanto ele
//  anda (leitura Modbus custa 5 a 20 ms com jitter, e retocar em cima
//  disso faria o braco oscilar). E um FIM DE CURSO por medida: enquanto
//  o movimento acontece, so se olha se ja passou do alvo, e se passou,
//  para. Um teste, uma decisao, sem ganho nenhum no meio.
//
//  E o que torna impossivel "dar voltas sem parar": por pior que esteja
//  passosPorGrau, o braco anda no maximo ate o angulo pedido mais o
//  atraso da leitura. O resto quem fecha e o assentamento.
// =====================================================================
void correcaoAlvoPedido(float t1, float t2, bool valido) {
  temAlvoPedido = valido;
  for (uint8_t i = 0; i < 2; i++) {
    buscando[i]      = false;
    buscaSentido[i]  = 0;
    buscaPassadas[i] = 0;
    buscaVel[i]      = 0.0f;
    buscaParando[i]  = false;
    buscouEstaViagem[i] = false;
  }
  buscaDefinir(1, 0, 0.0f);
  buscaDefinir(2, 0, 0.0f);
  if (!valido) return;
  // DESLIGAR A CORRECAO PELO ENCODER DESLIGA A BUSCA JUNTO.
  //
  // A chave diz "nao use o encoder para mexer no meu posicionamento", e
  // a busca e exatamente isso -- so que durante o movimento, em vez de
  // depois. Com ela desligada o braco anda pela contagem de passos e
  // para no destino em passos, como antes de existir encoder.
  if (!configCorrecao.ativa) return;

  alvoPedido[0] = t1;
  alvoPedido[1] = t2;
  largadaValida[0]   = largadaValida[1]   = false;
  esperandoParada[0] = esperandoParada[1] = false;

  // O PERCURSO DE CADA JUNTA, para as duas chegarem juntas.
  float percurso[2] = {0.0f, 0.0f}, maior = 0.0f;
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    const Junta& j = (k == 1) ? J1 : J2;
    if (!j.motor || !j.habilitado || !leituraConfiavel(k)) continue;
    percurso[i] = fabsf(alvoPedido[i] - encoderLer(k).graus);
    if (percurso[i] > maior) maior = percurso[i];
  }

  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    const Junta& j = (k == 1) ? J1 : J2;
    if (!j.motor || !j.habilitado) continue;
    if (!leituraConfiavel(k)) continue;   // sem medida nao ha o que buscar

    // A PONTA DE PARTIDA, com o eixo ainda parado: e o unico instante em
    // que a contagem de passos e a leitura do encoder falam do mesmo
    // momento, por mais velho que seja o ultimo quadro do barramento.
    largadaValida[i] = true;
    largadaGraus[i]  = encoderLer(k).graus;
    largadaPassos[i] = (k == 1) ? posicaoJ1() : posicaoJ2();

    const float falta = alvoPedido[i] - largadaGraus[i];
    if (fabsf(falta) <= BUSCA_TOLERANCIA_GRAUS) continue;   // ja esta la

    buscando[i] = true;
    buscouEstaViagem[i] = true;
    // A VELOCIDADE, CONSTANTE, e a que o operador pediu.
    //
    // O fator da regua entra so aqui, e so para que "12 graus/s" seja
    // mesmo 12: com passosPorVolta digitado maior que o real, o mesmo Hz
    // vira mais grau por segundo. Ele nao decide distancia nenhuma --
    // quem decide onde parar e a medida.
    // E ELA E PROPORCIONAL AO PERCURSO DA JUNTA.
    //
    // Uma proporcao, e nada mais: a junta que anda 4 graus enquanto a
    // outra anda 40 vai a um decimo da velocidade. As duas chegam
    // juntas, o caminho sai reto, e -- o que importa aqui -- a junta de
    // percurso curto nao corre no talo para depois nao conseguir parar.
    // Sem isto ela passava 3,2 graus onde a longa passava 0,5.
    buscaVel[i] = velAuto * govFator[i];
    if (maior > 0.001f) buscaVel[i] *= percurso[i] / maior;
    if (buscaVel[i] < BUSCA_VEL_MINIMA) buscaVel[i] = BUSCA_VEL_MINIMA;

    // O TETO DE TEMPO. Nao serve para acertar nada: serve para que uma
    // medida que congela com o eixo andando nao vire eixo solto.
    const float vReal = (velAuto > 0.1f) ? velAuto : 1.0f;
    const uint32_t previsto =
        (uint32_t)(maior / vReal * 1000.0f * BUSCA_FOLGA_TEMPO);
    buscaPrazo[i] = millis() + previsto + BUSCA_TEMPO_EXTRA_MS;
  }

  // A JUNTA QUE A BUSCA NAO PEGOU ANDA PELA RAMPA -- e a rampa foi
  // programada com a regua digitada. Com ela maior que a real o eixo sai
  // rapido demais; o fator ja medido corrige isso, e e a unica coisa que
  // ainda vale ali. Acontece quando a leitura nao servia na hora de
  // largar, que num barramento com falhas acontece.
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    Junta& j = (k == 1) ? J1 : J2;
    if (buscando[i] || !j.motor || govFator[i] >= 0.999f) continue;
    uint32_t hz = velProgramadaPub(i);
    if (hz < 1) continue;
    hz = (uint32_t)((float)hz * govFator[i]);
    if (hz < 1) hz = 1;
    programarVelocidadePub(j, i, hz);
  }
}

bool correcaoBuscando() {
  return buscando[0] || buscando[1];
}

// Para a busca de uma junta e deixa o eixo parar com a rampa.
//
// A CONTAGEM NAO SE ACERTA AQUI. Ela e acertada no fim do assentamento,
// depois que o movimento ja mediu a engrenagem e ja ensinou o fator da
// regua -- as duas contas saem de "quantos passos o eixo andou", e
// reescrever a contagem antes delas apagaria justamente o que elas
// medem. Ja aconteceu duas vezes nesta mesma funcao.
static void buscaEncerrar(uint8_t k, uint8_t i) {
  buscando[i] = false;
  buscaDefinir(k, 0, 0.0f);
}

// A BUSCA NAO CONSEGUIU TERMINAR: o assentamento assume.
//
// Perder a medida no meio -- leitura fora do curso, cabo caido, o teto de
// tempo -- deixaria o braco parado onde estivesse, e "onde estivesse"
// pode ser longe. Limpando a marca de que esta junta foi buscada, o
// assentamento volta a olhar para ela: ele e mais lento e retoca em
// passos, mas e o unico que ainda pode trazer o braco de volta.
//
// E uma escalada, nao um remendo: quem sabe fazer melhor tentou primeiro.
static void buscaDesistir(uint8_t k, uint8_t i) {
  buscaEncerrar(k, i);
  buscouEstaViagem[i] = false;
}

// =====================================================================
//  ANDA ATE A MEDIDA BATER. E SO ISSO.
//
//  Um teste e uma decisao por ciclo:
//
//    falta = alvo - o que o encoder diz
//    chegou dentro da tolerancia?  para.
//    o sinal de falta inverteu?    passou: volta mais devagar.
//    senao                         segue no mesmo sentido, mesma velocidade.
//
//  Nao ha distancia de frenagem, nao ha degrau planejado, nao ha destino
//  em passos. Nada aqui depende de passosPorVolta estar certo: a regua
//  entra so na velocidade, e a velocidade nao decide onde o eixo para.
//
//  A REDUCAO AO INVERTER nao e conta para acertar a posicao -- e a unica
//  coisa que impede a mesma frase de se repetir para sempre. Voltando na
//  mesma velocidade, o eixo passaria de novo pelo mesmo tanto.
//
//  Num barramento saudavel a primeira passada ja chega. Num barramento a
//  4,6 leituras por segundo o eixo anda 200 ms as cegas e passa: dai duas
//  ou tres passadas curtas. Consertar o cabo tira essas passadas -- o
//  firmware nao cria medida que nao chegou.
// =====================================================================
void correcaoBuscarAlvo() {
  if (!temAlvoPedido) return;
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    if (!buscando[i]) continue;
    Junta& j = (k == 1) ? J1 : J2;
    if (!j.motor || !j.habilitado) { buscaDesistir(k, i); continue; }

    // O teto de tempo vem primeiro: ele existe justamente para o caso em
    // que a medida parou de chegar e nada mais faria o eixo parar.
    if ((int32_t)(millis() - buscaPrazo[i]) >= 0) {
      buscaDesistir(k, i);
      logEvento("junta %u: a busca passou do tempo previsto e parou",
                (unsigned)k);
      definirMensagem("Junta %u parou: a busca passou do tempo previsto -- "
                      "confira a leitura do encoder", (unsigned)k);
      continue;
    }

    // ---- FASE 2: mandou parar, agora espera assentar e olha ----
    if (buscaParando[i]) {
      if (j.motor->isRunning()) continue;      // a rampa ainda esta correndo
      if (buscaContaParada[i] == 0) {
        buscaContaParada[i] = encoderLer(k).leituras;
        buscaPrazoParada[i] = millis() + ESPERA_ASSENTAR_MS;
        continue;
      }
      // Uma leitura NOVA, de depois da parada: a anterior conta onde o
      // eixo estava, nao onde ele ficou.
      if (encoderLer(k).leituras == buscaContaParada[i] &&
          (int32_t)(millis() - buscaPrazoParada[i]) < 0) continue;

      buscaParando[i]     = false;
      buscaContaParada[i] = 0;
      if (!leituraConfiavel(k)) {
        buscaDesistir(k, i);
        definirMensagem("Junta %u: a busca perdeu a leitura do encoder -- o "
                        "assentamento assume", (unsigned)k);
        continue;
      }

      const float falta = alvoPedido[i] - encoderLer(k).graus;
      if (fabsf(falta) <= BUSCA_TOLERANCIA_GRAUS) {
        buscaEncerrar(k, i);
        logEvento("junta %u: o encoder diz %.2f graus (pedido %.2f) -- "
                  "chegou", (unsigned)k, (double)encoderLer(k).graus,
                  (double)alvoPedido[i]);
        continue;
      }
      // Nao bateu. Vai de novo, MAIS DEVAGAR -- e a unica coisa que
      // impede a mesma frase de se repetir para sempre.
      buscaPassadas[i]++;
      if (buscaPassadas[i] > BUSCA_PASSADAS_MAX) {
        buscaEncerrar(k, i);
        logEvento("junta %u: parou a %.2f graus (pedido %.2f) -- e o que a "
                  "medida distingue neste barramento", (unsigned)k,
                  (double)encoderLer(k).graus, (double)alvoPedido[i]);
        continue;
      }
      buscaVel[i] *= BUSCA_REDUCAO;
      if (buscaVel[i] < BUSCA_VEL_MINIMA) buscaVel[i] = BUSCA_VEL_MINIMA;
      buscaSentido[i] = (falta > 0.0f) ? (int8_t)+1 : (int8_t)-1;
      buscaDefinir(k, buscaSentido[i], buscaVel[i]);
      logEvento("junta %u: faltam %.2f grau -- de novo, a %.2f graus/s",
                (unsigned)k, (double)falta, (double)buscaVel[i]);
      continue;
    }

    // ---- FASE 1: andando ate a medida bater ----
    //
    // Sem leitura nova e confiavel o eixo SEGUE no que ja estava fazendo.
    // Parar a cada falha de leitura, num barramento com 4% delas, seria
    // um movimento aos solavancos. Quem cuida do caso em que a leitura
    // nunca mais volta e o teto de tempo, la em cima.
    if (!leituraConfiavel(k)) continue;

    const float agora = encoderLer(k).graus;
    const float falta = alvoPedido[i] - agora;

    if (buscaSentido[i] == 0)
      buscaSentido[i] = (falta > 0.0f) ? (int8_t)+1 : (int8_t)-1;

    // Chegou ou passou, no sentido em que estava indo.
    if ((float)buscaSentido[i] * falta <= 0.0f ||
        fabsf(falta) <= BUSCA_TOLERANCIA_GRAUS) {
      buscaParando[i]     = true;
      buscaContaParada[i] = 0;
      buscaDefinir(k, 0, 0.0f);           // stopMove, com rampa
      continue;
    }
    buscaDefinir(k, buscaSentido[i], buscaVel[i]);
  }
}

// =====================================================================
//  O QUE A VIAGEM ENSINOU SOBRE A REGUA DIGITADA.
//
//  Chamada uma vez, quando o movimento termina. Compara os graus que a
//  REGUA mandou andar (passos percorridos / passosPorGrau) com os graus
//  que o ENCODER mediu. A razao entre os dois e o exagero da regua, e o
//  fator do governador e o seu inverso.
//
//  Uma conta por viagem, usando as duas pontas de um percurso de
//  segundos. A versao anterior fazia essa conta a cada leitura, dentro
//  de uma janela de 217 ms: dois numeros vizinhos, ruidosos, e um
//  resultado diferente a cada vez.
//
//  Isto NAO adota a regua medida. O fator so entra na VELOCIDADE, nunca
//  na distancia -- adotar a regua por baixo do pano ja foi tentado duas
//  vezes e as duas terminaram com o braco dando voltas (ver o comentario
//  em Robo2dof.ino, "A ADOCAO AUTOMATICA DA ENGRENAGEM SAIU DAQUI").
// =====================================================================
void correcaoAprenderDaViagem() {
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    if (!largadaValida[i]) continue;

    // A PONTA DE CHEGADA ESPERA UMA LEITURA NOVA.
    //
    // O eixo acabou de parar, mas o ultimo quadro do barramento e de
    // antes da parada -- num barramento de 217 ms, de ate 217 ms antes,
    // com o eixo ainda andando. Usar esse quadro conta um percurso mais
    // curto do que o de verdade, e a maquina "aprende" uma regua maior
    // do que a que tem.
    //
    // Contando LEITURAS, e nao relogio: o que interessa e que o quadro
    // tenha sido pedido depois de o eixo parar, e nao quantos
    // milissegundos passaram.
    //
    // E ESTE E O UNICO INSTANTE POSSIVEL.
    //
    // Colher no comeco do movimento seguinte nao serve: ancorarNoEncoder()
    // roda antes dele e REESCREVE a contagem de passos para casar com o
    // angulo medido. Depois disso o percurso em passos da viagem que
    // acabou nao existe mais -- os dois lados da conta viram o mesmo
    // numero e a razao sai 1, isto e, "a regua esta certa", justamente
    // quando ela nao esta.
    //
    // Por isso o robo espera aqui, em POSICIONANDO, ate a leitura
    // chegar. O prazo evita que um barramento mudo prenda a maquina.
    if (!esperandoParada[i]) {
      esperandoParada[i] = true;
      contaNaParada[i]   = encoderLer(k).leituras;
      const uint32_t ritmo = correcaoRitmoMs(k);
      const uint32_t espera = (ritmo > 0 && ritmo * 3 > 500) ? ritmo * 3 : 500;
      prazoAprender[i] = millis() + espera;
      continue;
    }
    if (encoderLer(k).leituras == contaNaParada[i]) {
      if ((int32_t)(millis() - prazoAprender[i]) < 0) continue;
      // Prazo vencido: esta viagem nao ensina, e a maquina segue.
      largadaValida[i]   = false;
      esperandoParada[i] = false;
      continue;
    }

    largadaValida[i]   = false;
    esperandoParada[i] = false;

    Junta& j = (k == 1) ? J1 : J2;
    if (j.passosPorGrau <= 0.0f) continue;
    if (!leituraConfiavel(k)) continue;

    const long  passosAgora = (k == 1) ? posicaoJ1() : posicaoJ2();
    const float grausRegua  =
        fabsf((float)(passosAgora - largadaPassos[i])) / j.passosPorGrau;
    const float grausMedidos = fabsf(encoderLer(k).graus - largadaGraus[i]);

    // Viagem curta nao ensina: a diferenca fica dentro do ruido das duas
    // pontas, e um fator tirado de ruido vale menos que nenhum.
    //
    // A medida e a que manda, e nao a regua: com a regua dobrada, andar
    // 3 graus de verdade sao so 1,5 graus de regua, e exigir 3 dos DOIS
    // fazia justamente as viagens mais reveladoras nao ensinarem nada.
    if (grausMedidos < APRENDER_VIAGEM_MINIMA_GRAUS) continue;

    // graus da regua / graus medidos = 1 / exagero.
    float f = grausRegua / grausMedidos;
    if (f > 4.0f)  f = 4.0f;    // leitura absurda nao ensina nada
    if (f < 0.02f) f = 0.02f;

    // O TETO DE 1 VALE NO RESULTADO, NUNCA NA MEDIDA.
    //
    // Travar a medida em 1 antes da media parece a mesma coisa e nao e:
    // o ruido de uma regua CERTA cai dos dois lados de 1, e travando um
    // dos lados so o outro entra na conta. O fator entao descia sozinho,
    // viagem apos viagem -- 1,000 para 0,987 para 0,97 --, e a maquina
    // acabava rastejando com a regua perfeitamente certa. Foi medido
    // assim no banco, num cenario que nao tinha nada a ver com regua.
    //
    // Medindo dos dois lados, uma regua certa fica em 1 porque o ruido
    // se cancela, e o teto no fim so garante que o fator SEGURE o eixo e
    // nunca o acelere.
    //
    // Meio caminho a cada viagem, e nao o valor cheio: uma unica viagem
    // atipica -- escorregao, batente, leitura ruim numa das pontas --
    // nao pode redefinir sozinha como a maquina anda.
    //
    // Menos na PRIMEIRA: ali o valor antigo nao e uma medida, e o
    // palpite de que a regua digitada esta certa. Ir pela metade entre
    // um palpite e uma medida so atrasa a maquina em duas ou tres
    // viagens -- com a regua quatro vezes errada, era o segundo
    // movimento ainda saindo a 16 graus/s com 12 pedidos.
    if (!govAprendeu[i]) { govAprendeu[i] = true; govFator[i] = f; }
    else                  govFator[i] += (f - govFator[i]) * 0.5f;
    if (govFator[i] > 1.0f)  govFator[i] = 1.0f;
    if (govFator[i] < 0.02f) govFator[i] = 0.02f;
  }
}

bool correcaoAprendendo() {
  for (uint8_t i = 0; i < 2; i++)
    if (largadaValida[i] && esperandoParada[i] &&
        (int32_t)(millis() - prazoAprender[i]) < 0) return true;
  return false;
}

float correcaoFatorRegua(uint8_t junta) {
  if (junta != 1 && junta != 2) return 1.0f;
  return govFator[junta - 1];
}

// ---------------------------------------------------------------------
// A ADOCAO AUTOMATICA FOI TENTADA E RETIRADA.
//
// Ela durou uma rodada. O resultado na bancada foi o pior de todos:
// "quando peco para ir a um angulo ele fica dando voltas sem parar".
//
// A medida e uma divisao entre pulsos contados e voltas lidas. Num
// barramento com 7% de falha e 4,5 leituras por segundo, uma leitura
// PARADA enquanto o eixo anda deixa o divisor pequeno demais e a
// engrenagem sai enorme. Regua enorme transforma um pedido de tres graus
// em centenas de voltas. O teto que existia -- 2 milhoes de pulsos por
// volta -- e duzentas vezes o valor real: nao segurava nada. E exigir
// que duas medidas concordem tambem nao salva, porque se a causa e
// leitura parada as duas concordam no mesmo numero errado.
//
// A medida continua sendo feita e continua aparecendo na tela, ao lado
// do campo. Quem escreve a regua da maquina e uma pessoa.
//
// O que faz o braco chegar sem depender da regua e o FREIO DO ENCODER:
// o alvo pedido fica guardado em graus e o movimento para quando o
// encoder diz que chegou. Ver correcaoAlvoPedido() logo abaixo.
// ---------------------------------------------------------------------

// Instantaneo do inicio do movimento. A afericao compara o comeco com o
// fim, entao ela precisa dos dois -- e de saber que nada estranho
// aconteceu no meio.
static long     engPasso0[2]  = {0, 0};
static int32_t  engBruto0[2]  = {0, 0};
static bool     engValendo[2] = {false, false};
static uint32_t engTravAntes  = 0;

static void esquecerAfericao() {
  engValendo[0] = engValendo[1] = false;
  engPasso0[0] = engPasso0[1] = 0;
  engBruto0[0] = engBruto0[1] = 0;
  engTravAntes = 0;
}

static void marcarInicioParaAfericao() {
  engTravAntes = correcaoTravamento().total;
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    const Junta& j = (k == 1) ? J1 : J2;
    engValendo[i] = false;
    // Sem torque quem move o eixo e a mao, e mao nao sai em pulso: a
    // divisao nao mediria engrenagem nenhuma.
    if (!j.habilitado) continue;
    if (configEncoder.reg[i] == 0) continue;
    if (!leituraConfiavel(k)) continue;
    engPasso0[i]  = (k == 1) ? posicaoJ1() : posicaoJ2();
    engBruto0[i]  = encoderLer(k).bruto;
    engValendo[i] = true;
  }
}

// Chamada UMA vez por movimento, depois que o eixo parou e a leitura
// assentou, e antes do primeiro retoque -- o retoque tambem sai em pulso e
// misturaria a medida.
static void aferirEngrenagemDoMovimento() {
  // Travou no meio: o eixo nao andou o que foi mandado, e a divisao daria
  // uma engrenagem inventada. Justamente o caso em que errar e mais caro.
  const bool travou = (correcaoTravamento().total != engTravAntes);
  bool gravar = false;

  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    if (!engValendo[i]) continue;
    engValendo[i] = false;
    if (travou) continue;
    const Junta& j = (k == 1) ? J1 : J2;
    // O torque caiu no meio do caminho: dali para a frente o encoder viu
    // o ferro, nao o pulso.
    if (!j.habilitado) continue;
    if (!leituraConfiavel(k)) continue;

    const long    dP = ((k == 1) ? posicaoJ1() : posicaoJ2()) - engPasso0[i];
    // Complemento de dois: a volta do contador de 32 bits sai sozinha.
    const int32_t dC =
        (int32_t)((uint32_t)encoderLer(k).bruto - (uint32_t)engBruto0[i]);
    if (aferirEngrenagem(k, dP, dC)) gravar = true;
  }
  // Sobreviver ao desligamento importa: sem isto o primeiro movimento
  // depois de cada boot repetiria o erro inteiro.
  if (gravar) salvarConfiguracoes();
}

// ---------------------------------------------------------------------
void correcaoNovoMovimento() {
  // TODO MOVIMENTO NOVO DESARMA O FREIO.
  //
  // Aqui, e nao em irParaPassos(): a ida automatica ao zero absoluto
  // move o braco por moverCoordenado() direto, sem passar por la. Sem
  // desarmar, ela herdava o alvo do movimento ANTERIOR e o freio a
  // parava no meio do caminho. Quem tem angulo pedido e irParaAngulos(),
  // que arma o freio logo depois de chamar esta funcao.
  correcaoAlvoPedido(0.0f, 0.0f, false);

  r.estado     = CORR_PARADA;
  r.tentativas = 0;
  erroAnterior = 0.0f;
  semProgresso = 0;
  fechavelAnterior = 0.0f;
  marcarInicioParaAfericao();
  dizer("");
}

// ---------------------------------------------------------------------
void correcaoIniciar() {
  if (!configCorrecao.ativa) return;
  if (soldaLigada()) return;     // retoque no meio do cordao estraga o cordao

  // BARRAMENTO LENTO NAO ASSENTA.
  //
  // Cada retoque anda e so muito depois a maquina ve onde parou; o
  // seguinte sai em cima de um numero velho. Da bancada, a 4,6 leituras
  // por segundo: "fica tentando acertar". Sem assentamento o braco para
  // onde a rampa o deixou -- que e onde ele parava antes de existir
  // correcao, e exatamente o que o operador pediu de volta.
  //
  // Maquina SEM encoder nenhum nao entra aqui: ali operar pela contagem e
  // escolha da instalacao, nao falha, e nao ha o que avisar. Quem leva
  // aviso e quem TEM encoder e ele nao esta dando conta.
  if (!encoderGuiaOMovimento(1) && !encoderGuiaOMovimento(2)) {
    const uint32_t m = correcaoRitmoMs(1) ? correcaoRitmoMs(1)
                                          : correcaoRitmoMs(2);
    if (m > 0) {
      r.estado = CORR_RECUSADA;
      dizer("barramento lento: sem assentamento");
      definirMensagem("Cheguei pela rampa. O encoder esta a uma leitura cada "
                      "%lu ms -- lento demais para acertar o ponto sem ficar "
                      "cacando", (unsigned long)m);
    }
    return;
  }

  r.estado     = CORR_ESPERANDO;
  r.tentativas = 0;
  erroAnterior = 0.0f;
  semProgresso = 0;
  fechavelAnterior = 0.0f;
  r.erroInicial1 = r.erroInicial2 = 0.0f;
  r.erroFinal1   = r.erroFinal2   = 0.0f;
  alvo1Original = posicaoJ1();
  alvo2Original = posicaoJ2();
  // Congelado agora, com a regua que valia quando o movimento foi pedido.
  alvoGraus1    = passosParaGraus(J1, alvo1Original);
  alvoGraus2    = passosParaGraus(J2, alvo2Original);
  // MAS SE HOUVE UM ANGULO PEDIDO, e ele que manda.
  //
  // Com o freio do encoder o movimento pode ter parado ANTES da contagem
  // de destino -- e ai converter a contagem daria um alvo que ninguem
  // pediu, e o assentamento levaria o braco de volta para o lugar errado.
  // O que o operador quer e o numero que ele digitou.
  if (temAlvoPedido) {
    alvoGraus1 = alvoPedido[0];
    alvoGraus2 = alvoPedido[1];
  }
  esperaAte = millis() + ESPERA_ASSENTAR_MS;
  dizer("assentando");
}

void correcaoCancelar() {
  if (r.estado == CORR_ESPERANDO || r.estado == CORR_RETOCANDO) {
    r.estado = CORR_PARADA;
    dizer("cancelado");
  }
}

bool correcaoEmCurso() {
  return r.estado == CORR_ESPERANDO || r.estado == CORR_RETOCANDO;
}

ResumoCorrecao correcaoResumo() { return r; }

// ---------------------------------------------------------------------
void correcaoAtualizar() {
  if (!correcaoEmCurso()) return;

  // Regra 4: solda ligada cancela na hora, em qualquer fase.
  if (soldaLigada()) {
    r.estado = CORR_RECUSADA;
    dizer("solda ligada: nao se retoca no cordao");
    return;
  }
  // Regra 1: so com o eixo parado.
  if (motoresEmMovimento()) return;
  if ((int32_t)(millis() - esperaAte) < 0) return;

  // O MOVIMENTO QUE ACABOU DE ACONTECER MEDIU A ENGRENAGEM.
  //
  // Aqui, e so aqui: o eixo esta parado, a leitura ja assentou, e nenhum
  // retoque saiu ainda para misturar pulso na conta. Se a regua mudar, o
  // resto deste ciclo ja trabalha com a nova -- o alvo nao se mexe porque
  // esta guardado em graus.
  if (r.tentativas == 0) aferirEngrenagemDoMovimento();

  const float alvoG1 = alvoGraus1;
  const float alvoG2 = alvoGraus2;

  float e1 = 0.0f, e2 = 0.0f;
  const bool t1 = faltaPara(1, alvoG1, e1);
  const bool t2 = faltaPara(2, alvoG2, e2);

  // Regra 2: sem leitura confiavel nao se mexe no motor.
  if (!t1 && !t2) {
    r.estado = CORR_RECUSADA;
    dizer("sem leitura do encoder: nada foi retocado");
    return;
  }

  // O RETOQUE ANTERIOR MEDIU A ESCALA: aproveita antes de calcular o
  // proximo. 'andou' e quanto o encoder se moveu de fato; 'pedido' e
  // quanto foi comandado. A divisao e o ganho real desta maquina.
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    if (retoquePedido[i] == 0.0f || !temMedidoAntes[i]) continue;
    if (!leituraConfiavel(k)) { retoquePedido[i] = 0.0f; continue; }
    const float andou = encoderLer(k).graus - medidoAntes[i];
    const float g = andou / retoquePedido[i];
    if (g > GANHO_MIN && g < GANHO_MAX) {
      ganhoRetoque[i] = g;
      ganhoAprendido[i] = true;
      logEvento("junta %u: retoque pediu %.3f grau e o eixo andou %.3f "
                "(ganho %.2f)", (unsigned)k, (double)retoquePedido[i],
                (double)andou, (double)g);
    }
    retoquePedido[i] = 0.0f;
  }

  if (r.tentativas == 0) { r.erroInicial1 = e1; r.erroInicial2 = e2; }
  r.erroFinal1 = e1;
  r.erroFinal2 = e2;

  const float m1 = fabsf(e1), m2 = fabsf(e2);
  const float tol = configCorrecao.toleranciaGraus;

  // JUNTA QUE A BUSCA LEVOU NAO ENTRA NO RETOQUE.
  //
  // A busca ja e malha fechada no encoder: ela para quando a medida diz o
  // numero. Duas malhas discutindo pelo mesmo eixo e de onde vinha o
  // vai-e-vem. O erro medido continua sendo calculado e mostrado; o que
  // muda e que ele nao vira comando.
  const bool ok1 = !t1 || m1 <= tol || buscouEstaViagem[0];
  const bool ok2 = !t2 || m2 <= tol || buscouEstaViagem[1];

  // Chegou.
  if (ok1 && ok2) {
    // O eixo esta fisicamente no lugar certo. A contagem, porem, ficou
    // adiantada pelo tanto que o retoque andou -- e e ela que o proximo
    // movimento absoluto usa como ponto de partida. Sem devolver a
    // contagem ao alvo, o desvio nao some: ele so passa para o proximo
    // movimento, que e exatamente o incomodo que este modulo existe para
    // resolver.
    //
    // Nenhum pulso sai no fio aqui: o eixo NAO se mexe.
    //
    // A contagem que representa o alvo se refaz da regua DE AGORA: se a
    // engrenagem foi aferida durante este assentamento, o numero de passos
    // que descreve aquele angulo mudou. Devolver o valor velho reporia na
    // contagem justamente o erro que a afericao acabou de tirar.
    //
    // Junta que a BUSCA levou tem a contagem acertada no que a MEDIDA
    // diz, e nao no alvo: a busca para onde o encoder mandou, e se ela
    // parou a um decimo dali e esse decimo que a contagem tem de contar.
    for (uint8_t k = 1; k <= 2; k++) {
      const uint8_t i = k - 1;
      Junta& j = (k == 1) ? J1 : J2;
      if (buscouEstaViagem[i] && j.passosPorGrau > 0.0f &&
          leituraConfiavel(k)) {
        ajustarContagem(j, grausParaPassos(j, encoderLer(k).graus));
      } else if (r.tentativas > 0) {
        const float alvoG = (k == 1) ? alvoGraus1 : alvoGraus2;
        const long  orig  = (k == 1) ? alvo1Original : alvo2Original;
        ajustarContagem(j, (j.passosPorGrau > 0.0f)
                           ? grausParaPassos(j, alvoG) : orig);
      }
    }
    r.estado = CORR_PRONTA;
    r.totalOk++;
    dizer("posicao conferida pelo encoder");
    return;
  }

  // REGRA 5, REFEITA: erro grande nao se pula, mas tambem nao se
  // abandona.
  //
  // Antes, erro acima de maxCorrecaoGraus era RECUSADO e o braco ficava
  // onde estava. Era isto que fazia "peco zero grau e quem chega e so o
  // tracejado": com sete graus de diferenca o assentamento se recusava a
  // mexer, o operador via a contagem no alvo e o braco longe dele.
  //
  // O teto passou a ser o tamanho do PASSO, nao um motivo para desistir.
  // Cada retoque anda no maximo maxCorrecaoGraus, le o encoder de novo e
  // repete: sete graus fecham em tres passos, sempre olhando o encoder,
  // e nenhum deles e um pulo. A intencao da regra continua de pe -- o
  // braco nunca lunga varios graus de uma vez.
  const float teto = configCorrecao.maxCorrecaoGraus;

  // REGRA 6, REFEITA DE NOVO: progresso se mede contra o PASSO, nunca
  // contra o erro inteiro.
  //
  // Desistir de nao PROGREDIR (em vez de contar tentativas) foi acerto: o
  // que denuncia acoplamento solto ou reducao errada e o retoque nao
  // diminuir o erro. Mas cobrar 15% DO ERRO TOTAL por passo era um alvo
  // que o proprio teto tornava inalcancavel: um passo de tres graus so
  // consegue 15% enquanto o erro for menor que vinte. Acima disso a
  // desistencia virava ARITMETICA -- o assentamento parava em tres
  // retoques com dezenas de graus na peca, dissesse o que dissesse o
  // encoder, e o operador via "comeca bem, da uns travamentos e nunca
  // chega".
  //
  // A pergunta certa nao e "sobrou pouco?", e "o passo fez o que podia
  // fazer?". Um passo que fechou o que tinha como fechar esta convergindo,
  // por mais que ainda falte caminho.
  const float faltaMaior = ((ok1 ? 0.0f : m1) > (ok2 ? 0.0f : m2))
                             ? (ok1 ? 0.0f : m1) : (ok2 ? 0.0f : m2);
  if (r.tentativas > 0) {
    const float fechou = erroAnterior - faltaMaior;
    // Sem referencia do passo anterior (primeiro ciclo apos um retoque que
    // nao comandou nada), cai no criterio antigo.
    const float podia = (fechavelAnterior > 0.0f) ? fechavelAnterior
                                                  : erroAnterior;
    if (fechou > podia * 0.15f) semProgresso = 0;
    else                        semProgresso++;
  }
  erroAnterior = faltaMaior;

  if (semProgresso >= configCorrecao.tentativas) {
    r.estado = CORR_DESISTIU;
    r.totalDesistiu++;
    dizer("o retoque nao aproxima: veja acoplamento e reducao");
    return;
  }
  // Teto absoluto, so para nunca existir laco infinito no core 1.
  if (r.tentativas >= RETOQUES_MAXIMOS) {
    r.estado = CORR_DESISTIU;
    r.totalDesistiu++;
    dizer("nao fechou na tolerancia");
    return;
  }

  // O retoque. 'falta' e alvo menos medido: positivo quer dizer que o
  // eixo esta ATRAS do alvo e precisa avancar.
  // Graus de erro viram passos direto pela resolucao da junta. Nao se
  // usa grausParaPassos() aqui: aquela funcao converte um ANGULO
  // ABSOLUTO (descontando o zero da maquina), e o que se tem aqui e uma
  // DIFERENCA -- passar diferenca por ela somaria o zero duas vezes.
  // Quanto PEDIR para andar tantos graus de erro: divide pelo ganho
  // medido. Sem ganho medido ainda, sai amortecido -- passar do ponto
  // custa outra viagem.
  const auto passoDe = [](float erro, uint8_t i) {
    return ganhoAprendido[i] ? (erro / ganhoRetoque[i])
                             : (erro * AMORTECIMENTO_SEM_GANHO);
  };

  long alvo1 = posicaoJ1();
  long alvo2 = posicaoJ2();
  float pedido1 = 0.0f, pedido2 = 0.0f;
  if (!ok1) {
    pedido1 = comTeto(passoDe(e1, 0), teto);
    alvo1 += lroundf(pedido1 * J1.passosPorGrau);
  }
  if (!ok2) {
    pedido2 = comTeto(passoDe(e2, 1), teto);
    alvo2 += lroundf(pedido2 * J2.passosPorGrau);
  }

  // QUANTO ESTE PASSO TEM COMO FECHAR, em graus MEDIDOS -- que e a unidade
  // em que o erro e contado. O passo sai em graus COMANDADOS; o eixo anda
  // esses graus multiplicados pelo ganho da maquina. E ele nunca fecha
  // mais do que o erro que existia.
  //
  // E contra este numero que o ciclo seguinte julga o progresso. Sem ele o
  // criterio cobrava do passo mais do que o teto deixava o passo entregar.
  const auto fechavelDe = [&](float erro, float pedido, uint8_t i) {
    if (pedido == 0.0f) return 0.0f;
    const float anda = fabsf(pedido) *
                       (ganhoAprendido[i] ? ganhoRetoque[i] : 1.0f);
    const float e    = fabsf(erro);
    return (anda < e) ? anda : e;
  };
  const float f1 = fechavelDe(e1, pedido1, 0);
  const float f2 = fechavelDe(e2, pedido2, 1);
  fechavelAnterior = (f1 > f2) ? f1 : f2;

  // Regra 3: nunca para fora do curso, QUANDO o curso esta valendo.
  // Com o limite desligado o braco anda livre pela mesa (ver R143), e
  // prender o retoque num curso que nao esta em vigor -- pior, num curso
  // medido pela metade -- era mais um jeito de o braco nao chegar.
  if (protCurso && J1.calibrada) {
    if (alvo1 < J1.passosMin) alvo1 = J1.passosMin;
    if (alvo1 > J1.passosMax) alvo1 = J1.passosMax;
  }
  if (protCurso && J2.calibrada) {
    if (alvo2 < J2.passosMin) alvo2 = J2.passosMin;
    if (alvo2 > J2.passosMax) alvo2 = J2.passosMax;
  }

  if (alvo1 == posicaoJ1() && alvo2 == posicaoJ2()) {
    // O retoque cairia fora do curso: nao ha o que fazer sem furar o
    // limite, e furar o limite nunca.
    r.estado = CORR_DESISTIU;
    r.totalDesistiu++;
    dizer("retoque cairia fora do curso calibrado");
    return;
  }

  // O RETOQUE E MOVIMENTO, e passa pelo mesmo portao que todo o resto.
  //
  // pararTudo() ja cancela o assentamento, entao na pratica ele nao
  // chega aqui com a emergencia acionada. Mas este e o unico caminho do
  // firmware que move o braco sem ter vindo de um comando do operador:
  // depender de um cancelamento la longe para ele nao andar e confiar em
  // ordem de chamada. A trava fica aqui tambem.
  if (!movimentoSeguro) {
    r.estado = CORR_PARADA;
    dizer("assentamento suspenso: intertravamento de seguranca");
    return;
  }

  // SUAVIDADE NA CHEGADA: a velocidade acompanha o que falta.
  //
  // Era fixa em um quarto da normal -- boa para decimos de grau, dura
  // para graus inteiros, e sempre a mesma no ultimo passo, que e onde
  // passar do ponto custa outra viagem. Agora o retoque longe anda a
  // meia velocidade e vai afinando ate o minimo da maquina, entao o
  // ultimo decimo e um encosto e nao um tranco.
  const float cheia = velNormal * 0.5f;
  const float andarMaior = (fabsf(pedido1) > fabsf(pedido2))
                         ? fabsf(pedido1) : fabsf(pedido2);
  float vel = (GRAUS_VEL_CHEIA > 0.001f)
            ? cheia * (andarMaior / GRAUS_VEL_CHEIA) : cheia;
  if (vel > cheia)     vel = cheia;
  if (vel < velMinima) vel = velMinima;
  // Guarda o que foi pedido e de onde partiu: e com esse par que o
  // ciclo seguinte mede o ganho.
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    const float ped = (k == 1) ? pedido1 : pedido2;
    retoquePedido[i]  = ped;
    temMedidoAntes[i] = false;
    if (ped != 0.0f && leituraConfiavel(k)) {
      medidoAntes[i]    = encoderLer(k).graus;
      temMedidoAntes[i] = true;
    }
  }

  moverCoordenado(alvo1, alvo2, vel);
  r.tentativas++;
  r.estado = CORR_RETOCANDO;
  esperaAte = millis() + ESPERA_ASSENTAR_MS;
  dizer("retocando");
}

// =====================================================================
//  Localizar-se ao ligar. Ver correcao.h.
// =====================================================================
static ResumoZero z;
static uint32_t zeroDesde = 0;
static bool     zeroComecou = false;
// Uma leitura chegou, mas era impossivel. Guardado para a mensagem
// distinguir "o encoder nao respondeu" de "o encoder respondeu besteira"
// -- sao dois defeitos diferentes, com conserto diferente.
static bool  zImplausivel[2]      = {false, false};
static float zGrausImplausivel[2] = {0.0f, 0.0f};

ResumoZero zeroResumo() { return z; }

static void dizerZero(const char* m) {
  strncpy(z.motivo, m, sizeof(z.motivo) - 1);
  z.motivo[sizeof(z.motivo) - 1] = '\0';
}

// Acerta a contagem de passos de uma junta para bater com o encoder.
// Nenhum pulso sai no fio: o eixo NAO se mexe, so a conta muda.
// ---------------------------------------------------------------------
// A leitura e FISICAMENTE possivel?
//
// O encoder e a unica testemunha de onde o braco esta, e escrever a
// leitura na contagem de passos e obedecer a essa testemunha sem
// conferir nada. Se ela mentir uma vez -- registrador errado, contagens
// por volta erradas, ruido que passou no CRC -- a mentira vira a posicao
// oficial da maquina, e a partir dali TODA protecao de curso se apoia num
// numero inventado.
//
// A conferencia nao e estatistica, e fisica: o braco nao pode estar fora
// do curso que o proprio operador mediu. A folga cobre o que sobra
// depois do limite (o batente fica um pouco alem, e da para empurrar o
// braco a mao ate ele). Alem disso nao e posicao, e defeito.
//
// SO QUE ISSO SO VALE SE O CURSO MEDIDO FOR UMA AFIRMACAO SOBRE A
// MAQUINA -- e desde que o limite virou OPCAO, ele nao e.
//
// O operador mede o curso e decide, em separado, se quer que a maquina o
// respeite (protCurso). Com o limite desligado o braco anda livre pela
// mesa, e a leitura do encoder fora daquela faixa e leitura BOA de um
// lugar onde o braco legitimamente esta. Conferir assim mesmo virava o
// pior defeito possivel: um curso medido pela metade -- uma calibracao
// abortada, dois graus de faixa -- calava o encoder na maquina inteira.
// E como calava, calava TUDO: nao havia mais reancoragem, nem
// seguimento de eixo solto, nem assentamento, e o desenho na tela (que
// so obedece ao encoder) congelava. O sintoma era "movo o motor e o
// braco nao acompanha", e a causa era esta linha ignorando o encoder de
// proposito.
//
// Entao: a conferencia contra o curso vale quando o limite esta LIGADO.
// O que separa leitura de lixo em qualquer caso e o teto absoluto
// abaixo, e ele vale sempre.
// ---------------------------------------------------------------------
static const float FOLGA_PLAUSIVEL_GRAUS = 10.0f;

// Teto que vale SEM calibracao nenhuma.
//
// A conferencia contra o curso so existe depois de calibrar, e maquina em
// comissionamento nunca esta calibrada -- entao ali qualquer numero
// passava. Com os dois encoders no barramento e um deles mal configurado
// (contagens por volta erradas, formato de 32 bits errado, registrador do
// vizinho), o angulo saia em dezenas de milhares de graus, era marcado
// CONFIAVEL, ia para a tela e o braco desenhado girava sem parar.
//
// Nao existe junta desta maquina em 170 mil graus, calibrada ou nao. Duas
// voltas completas e folga generosa para qualquer montagem real e ainda
// pega o lixo por ordens de grandeza -- que e o que esta guarda precisa
// fazer: separar leitura de numero, nao medir precisao.
static const float LIMITE_ABSURDO_GRAUS = 720.0f;

static bool leituraPlausivel(uint8_t k, float graus) {
  const Junta& j = (k == 1) ? J1 : J2;
  if (graus != graus) return false;                 // NaN nao e angulo
  // Vale sempre, inclusive sem calibracao. E a unica conferencia que
  // existe durante a montagem, que e justamente quando o encoder esta
  // mal configurado.
  if (graus < -LIMITE_ABSURDO_GRAUS || graus > LIMITE_ABSURDO_GRAUS) return false;
  // Curso medido so e fronteira quando o operador ligou o limite. Ver o
  // bloco acima: com ele desligado o braco anda livre, e leitura fora da
  // faixa e leitura boa de onde o braco de fato esta.
  if (!protCurso) return true;
  if (!j.calibrada) return true;
  return graus >= j.grausMin - FOLGA_PLAUSIVEL_GRAUS &&
         graus <= j.grausMax + FOLGA_PLAUSIVEL_GRAUS;
}

// =====================================================================
//  O ENCODER CHEGA A TEMPO DE GUIAR O MOVIMENTO?
//
//  Corrigir o eixo a partir de uma medida so funciona se a PROXIMA
//  medida chegar a tempo de mostrar o resultado da correcao. Num
//  barramento lento a maquina age, o eixo anda, e so muito depois ela ve
//  onde parou -- e age de novo em cima de um numero velho. Isso nao
//  converge: fica cacando o ponto.
//
//  Da bancada, com 4,6 leituras por segundo: "micro variacao e nunca
//  fica no ponto setado, ele fica tentando acertar. Antes ele ia
//  desacelerando com rampa e parava exatamente."
//
//  Entao o ritmo do barramento decide QUEM leva o eixo:
//
//    rapido  -> o encoder guia: afina ao chegar, freia no alvo, assenta.
//    lento   -> a rampa do gerador de pulso leva, desacelera e para. O
//               encoder segue valendo para dizer onde o braco esta,
//               ancorar a partida, calibrar e avisar de travamento --
//               so nao manda no motor.
//
//  Mede-se o intervalo entre leituras BOAS, suavizado, e nao a taxa
//  nominal configurada: o que importa e o que chega, com as falhas
//  incluidas.
// =====================================================================
static uint32_t ritmoMs[2]     = {0, 0};   // intervalo medio entre leituras
static uint32_t ritmoUlt[2]    = {0, 0};   // instante da ultima leitura nova
static uint32_t ritmoConta[2]  = {0, 0};   // contador visto por ultimo

void correcaoMedirRitmo() {
  const uint32_t agora = millis();
  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    if (configEncoder.reg[i] == 0) { ritmoMs[i] = 0; continue; }
    const LeituraEncoder L = encoderLer(k);
    if (L.leituras == ritmoConta[i]) continue;   // nada novo
    ritmoConta[i] = L.leituras;
    if (ritmoUlt[i] != 0) {
      const uint32_t dt = agora - ritmoUlt[i];
      // LACUNA NAO E RITMO. Cabo que caiu e voltou, reconfiguracao, a
      // maquina parada num veu de calibracao -- tudo isso produz um
      // intervalo enorme que nao diz nada sobre o barramento. Contar
      // esse numero na media deixaria o encoder "lento" por segundos
      // depois de qualquer pausa. Acima do teto, so recomeca a contar.
      if (dt <= CORR_LACUNA_MS) {
        // Media exponencial: uma leitura atrasada isolada nao decide, e
        // uma degradacao que dura aparece em poucos ciclos.
        ritmoMs[i] = ritmoMs[i] ? (ritmoMs[i] * 3 + dt) / 4 : dt;
      }
    }
    ritmoUlt[i] = agora;
  }
}

bool encoderGuiaOMovimento(uint8_t junta) {
  if (junta != 1 && junta != 2) return false;
  const uint32_t m = ritmoMs[junta - 1];
  if (m == 0) return false;            // ainda nao ha ritmo medido
  return m <= CORR_INTERVALO_MAX_MS;
}

uint32_t correcaoRitmoMs(uint8_t junta) {
  return (junta == 1 || junta == 2) ? ritmoMs[junta - 1] : 0;
}

bool leituraConfiavel(uint8_t junta) {
  if (junta != 1 && junta != 2) return false;
  if (configEncoder.reg[junta - 1] == 0) return false;
  const LeituraEncoder L = encoderLer(junta);
  if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) return false;
  return leituraPlausivel(junta, L.graus);
}

static bool localizar(uint8_t k) {
  Junta& j = (k == 1) ? J1 : J2;
  if (configEncoder.reg[k - 1] == 0) return false;   // junta nao ligada
  // Sem zero ensinado a referencia e um numero arbitrario, e acertar a
  // contagem por ela poria o braco em qualquer lugar.
  if (!configZero.ensinado[k - 1]) return false;

  const LeituraEncoder L = encoderLer(k);
  if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) return false;
  if (j.passosPorGrau <= 0.0f) return false;

  // Localizar-se por uma leitura impossivel e pior que nao se localizar:
  // a maquina passaria a operar com uma posicao inventada, e o "ir ao
  // zero" mandaria um curso inteiro de pulso contra o batente.
  if (!leituraPlausivel(k, L.graus)) {
    zImplausivel[k - 1] = true;
    zGrausImplausivel[k - 1] = L.graus;
    return false;
  }

  ajustarContagem(j, grausParaPassos(j, L.graus));
  z.graus[k - 1] = L.graus;
  return true;
}

void zeroAtualizar() {
  if (!configZero.sincronizar) { z.estado = ZERO_PRONTO; return; }
  // Nenhuma junta com zero ensinado: nao ha o que recuperar, e a maquina
  // se comporta exatamente como antes do encoder absoluto.
  if (!configZero.ensinado[0] && !configZero.ensinado[1]) {
    if (z.estado == ZERO_ESPERANDO) {
      z.estado = ZERO_PRONTO;
      dizerZero("zero nao ensinado: a maquina liga como antes");
    }
    return;
  }
  if (z.estado == ZERO_PRONTO || z.estado == ZERO_SEM_ENCODER) return;

  if (!zeroComecou) { zeroComecou = true; zeroDesde = millis(); }

  // ---- 1. localizar ----
  if (z.estado == ZERO_ESPERANDO) {
    // Nao se acerta contagem com o eixo andando: entre ler e escrever o
    // eixo teria andado mais, e a conta nasceria torta.
    if (motoresEmMovimento()) return;

    bool alguma = false;
    for (uint8_t k = 1; k <= 2; k++) {
      if (z.localizou[k - 1]) { alguma = true; continue; }
      if (localizar(k)) { z.localizou[k - 1] = true; alguma = true; }
    }

    // Junta nao ligada nao impede: uma bancada com um driver so tem de
    // conseguir ligar a maquina.
    bool faltaAlguma = false;
    for (uint8_t k = 1; k <= 2; k++)
      if (configEncoder.reg[k - 1] != 0 && configZero.ensinado[k - 1] &&
          !z.localizou[k - 1]) faltaAlguma = true;

    if (!faltaAlguma && alguma) {
      z.estado = ZERO_LOCALIZADO;
      dizerZero("posicao recuperada do encoder");
      definirMensagem("Posicao recuperada do encoder: %.2f / %.2f graus",
                      (double)z.graus[0], (double)z.graus[1]);
      return;
    }

    // Cinco segundos sem leitura: segue como antes do encoder. Uma
    // maquina que nao liga porque o encoder nao respondeu e pior que uma
    // maquina que liga sem saber onde esta.
    if ((uint32_t)(millis() - zeroDesde) > 5000) {
      z.estado = ZERO_SEM_ENCODER;
      // "Nao respondeu" e "respondeu besteira" pedem conserto diferente:
      // um e cabo, o outro e registrador ou contagens por volta.
      if (zImplausivel[0] || zImplausivel[1]) {
        const uint8_t k = zImplausivel[0] ? 1 : 2;
        dizerZero("leitura fora do curso: nao me localizei");
        definirMensagem("Junta %u: o encoder diz %.1f graus, fora do curso "
                        "%.0f a %.0f. Confira registrador e contagens por volta",
                        (unsigned)k, (double)zGrausImplausivel[k - 1],
                        (double)((k == 1) ? J1.grausMin : J2.grausMin),
                        (double)((k == 1) ? J1.grausMax : J2.grausMax));
      } else {
        dizerZero("sem encoder no boot: posicao nao recuperada");
        definirMensagem("Sem leitura do encoder ao ligar: refira a maquina a mao");
      }
    }
    return;
  }

  // ---- 2. ir para o zero ----
  if (z.estado == ZERO_LOCALIZADO) {
    if (!configZero.irParaZero) {
      z.estado = ZERO_PRONTO;
      dizerZero("localizado (ir ao zero desligado)");
      return;
    }
    // O INTERTRAVAMENTO: nada anda antes de o operador habilitar os
    // servos, que e uma acao explicita dele na tela.
    if (!servosLigados) return;
    if (soldaLigada()) return;
    if (modoAtual != MODO_MANUAL) return;
    if (motoresEmMovimento()) return;

    // Ja esta no zero? Nao mexe.
    const float g1 = passosParaGraus(J1, posicaoJ1());
    const float g2 = passosParaGraus(J2, posicaoJ2());
    if (fabsf(g1) <= configZero.toleranciaGraus &&
        fabsf(g2) <= configZero.toleranciaGraus) {
      z.estado = ZERO_PRONTO;
      dizerZero("ja estava no zero");
      return;
    }

    // O zero tem de caber no curso calibrado. Se nao couber, ir para la
    // seria furar a protecao -- e a protecao existe justamente porque
    // nao ha fim de curso.
    if ((J1.calibrada && (grausParaPassos(J1, 0.0f) < J1.passosMin ||
                          grausParaPassos(J1, 0.0f) > J1.passosMax)) ||
        (J2.calibrada && (grausParaPassos(J2, 0.0f) < J2.passosMin ||
                          grausParaPassos(J2, 0.0f) > J2.passosMax))) {
      z.estado = ZERO_PRONTO;
      dizerZero("o zero esta fora do curso: nao fui");
      definirMensagem("Nao fui ao zero: ele esta fora do curso calibrado");
      return;
    }

    z.estado = ZERO_INDO;
    dizerZero("indo para o zero");
    definirMensagem("Indo para 0 grau (o encoder disse onde o braco estava)");
    correcaoNovoMovimento();
    moverCoordenado(grausParaPassos(J1, 0.0f), grausParaPassos(J2, 0.0f),
                    velAuto);
    modoAtual = MODO_POSICIONANDO;
    return;
  }

  // ---- 3. chegou ----
  if (z.estado == ZERO_INDO) {
    if (motoresEmMovimento() || modoAtual == MODO_POSICIONANDO) return;
    z.estado = ZERO_PRONTO;
    dizerZero("no zero");
  }
}

// =====================================================================
//  Seguir o eixo movido a mao. Ver correcao.h.
// =====================================================================
// Avisa, no maximo uma vez a cada 10 s por junta: com o eixo solto a
// leitura ruim se repete 20 vezes por segundo, e uma tira de mensagem
// piscando esconde tudo o que importa.
static void avisarImplausivel(uint8_t k, float graus) {
  static uint32_t ultimo[2] = {0, 0};
  const uint32_t agora = millis();
  if (ultimo[k - 1] && (uint32_t)(agora - ultimo[k - 1]) < 10000) return;
  ultimo[k - 1] = agora;
  const Junta& j = (k == 1) ? J1 : J2;
  definirMensagem("Junta %u: encoder diz %.1f graus, fora do curso %.0f a %.0f. "
                  "Posicao nao acompanhada", (unsigned)k, (double)graus,
                  (double)j.grausMin, (double)j.grausMax);
}

// A contagem de passos parou de descrever o braco: reescreve pelo
// encoder. Ver DIVERGENCIA_MAXIMA_GRAUS em config.h para o porque.
//
// Roda com o servo LIGADO, que e onde o assentamento manda -- mas so
// acima do teto em que o assentamento ja desistiu. Abaixo dele nada
// muda: divergencia pequena continua sendo perda de passo, e continua
// sendo do assentamento.
// A maquina esta REALMENTE parada, e nao apenas entre dois pulsos.
//
// isRunning() do gerador de pulso responde "esta saindo pulso agora?",
// e entre mandar um destino e o primeiro pulso sair ela responde NAO.
// Nessa fresta a contagem era reescrita, o destino que acabara de ser
// calculado sobre a contagem antiga virava outro lugar, e o braco
// arrancava e parava -- que e exatamente o que se via na tela: "comeca a
// se mover, da uma atualizacao e para".
//
// Duas condicoes fecham a fresta: o modo tem de ser MANUAL (posicionar,
// executar e reproduzir sao movimento em curso, com ou sem pulso neste
// instante) e o gerador tem de estar quieto ha um tempo.
static const uint32_t QUIETO_MS = 300;

static bool bracoQuieto() {
  static uint32_t ultimoMovimentoMs = 0;
  if (motoresEmMovimento()) { ultimoMovimentoMs = millis(); return false; }
  if (modoAtual != MODO_MANUAL) { ultimoMovimentoMs = millis(); return false; }
  if (ultimoMovimentoMs == 0) return true;
  return (uint32_t)(millis() - ultimoMovimentoMs) > QUIETO_MS;
}

static void reancorarSeAContagemSePerdeu() {
  if (!bracoQuieto()) return;
  if (correcaoEmCurso()) return;

  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    Junta& j = (k == 1) ? J1 : J2;
    if (configEncoder.reg[i] == 0) continue;
    if (j.passosPorGrau <= 0.0f) continue;
    if (!leituraConfiavel(k)) continue;

    const LeituraEncoder L = encoderLer(k);
    const float conta = passosParaGraus(j, (k == 1) ? posicaoJ1() : posicaoJ2());
    const float dif   = L.graus - conta;
    if (fabsf(dif) < DIVERGENCIA_MAXIMA_GRAUS) continue;

    ajustarContagem(j, grausParaPassos(j, L.graus));
    // Dizer sempre: reescrever a posicao da maquina em silencio seria a
    // tela mudando de numero sem ninguem entender por que.
    definirMensagem("Junta %u: a contagem tinha se perdido (%.0f graus de "
                    "diferenca) e foi reancorada no encoder, que mede "
                    "%.2f graus", (unsigned)k, (double)dif, (double)L.graus);
    logEvento("contagem reancorada na junta %u: %.1f -> %.1f graus",
              (unsigned)k, (double)conta, (double)L.graus);
  }
}

void seguirEixoSolto() {
  // A contagem perdida se conserta com servo ligado ou desligado: e o
  // caso em que ela deixou de significar qualquer coisa.
  reancorarSeAContagemSePerdeu();

  if (!bracoQuieto()) return;
  if (correcaoEmCurso()) return;

  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;
    Junta& j = (k == 1) ? J1 : J2;

    // A regra e POR JUNTA, e nao pela maquina inteira.
    //
    // Era "servosLigados" -- que so e verdade com AS DUAS juntas
    // energizadas. Numa bancada com um driver so isso nunca acontece, e
    // o seguimento de eixo solto ficava permanentemente ligado sobre uma
    // junta COM torque: a contagem era reescrita pelo encoder a cada
    // ciclo, inclusive logo depois de um destino ter sido calculado a
    // partir dela. Com torque, divergencia e perda de passo, e quem
    // cuida disso e o assentamento -- nao esta funcao.
    if (j.habilitado) continue;

    // Sem zero ensinado a leitura nao tem do que ser medida.
    if (!configZero.ensinado[i]) continue;
    if (configEncoder.reg[i] == 0) continue;
    if (j.passosPorGrau <= 0.0f) continue;

    const LeituraEncoder L = encoderLer(k);
    if (!L.valido || L.idadeMs > ENC_IDADE_MAX_MS) continue;
    // Mao nenhuma leva o braco para fora do curso que ele tem. Leitura
    // dali para fora nao e o eixo: e defeito, e obedecer a ela poria a
    // maquina inteira operando com uma posicao inventada.
    if (!leituraPlausivel(k, L.graus)) {
      avisarImplausivel(k, L.graus);
      continue;
    }

    const float conta = passosParaGraus(j, (k == 1) ? posicaoJ1() : posicaoJ2());
    const float dif = L.graus - conta;
    // Zona morta: encoder de 17 bits treme, e reescrever a contagem a
    // cada tremor encheria o barramento de nada. Dois decimos de grau e
    // menos que qualquer movimento de mao.
    if (fabsf(dif) < 0.2f) continue;

    ajustarContagem(j, grausParaPassos(j, L.graus));
  }
}

// =====================================================================
//  Travamento
// =====================================================================
static Travamento trav = {false, 0, 0};

Travamento correcaoTravamento() { return trav; }
void correcaoLimparTravamento() { trav.ativo = false; trav.junta = 0; }

// Quanto o eixo DEVERIA estar andando, em contagens do encoder por
// segundo, para a velocidade de pulso que esta saindo agora.
static float esperadoContagensPorSeg(uint8_t k) {
  const Junta& j = (k == 1) ? J1 : J2;
  const float hz = (k == 1) ? velocidadeJ1Hz() : velocidadeJ2Hz();

  // A ESCALA MEDIDA vem primeiro.
  //
  // Este vigia PARA o braco. Ele nao pode decidir isso a partir de dois
  // numeros de catalogo que ninguem conferiu: bastava o driver estar
  // configurado com outro numero de pulsos por volta para o esperado sair
  // varias vezes maior que o real, e ai um braco andando normalmente era
  // declarado travado meio segundo depois de arrancar -- "comeca a se
  // mover, aparece um aviso e para".
  //
  // Quando ha escala ensinada, ela e a regua certa: contagens por grau
  // medidas na propria maquina, do mesmo lado de onde vem a leitura que
  // vai ser comparada.
  const float cpg = configEncoder.contagensPorGrau[k - 1];
  if (j.passosPorGrau > 0.0f && (cpg > 0.0001f || cpg < -0.0001f))
    return fabsf(hz) / j.passosPorGrau * fabsf(cpg);

  // Sem escala ensinada, o caminho antigo: pulsos por volta do motor e
  // contagens por volta do encoder.
  const float cv = configEncoder.contagensPorVolta[k - 1];
  if (j.passosPorVolta == 0 || cv <= 0.0f) return 0.0f;
  // passos/s -> voltas do motor/s -> contagens/s
  return fabsf(hz) / (float)j.passosPorVolta * cv;
}

static void vigiarTravamento() {
  static uint32_t desde[2] = {0, 0};
  // Quantas leituras boas o encoder ja tinha entregue quando a suspeita
  // comecou. A diferenca ate agora diz se a janela foi preenchida por
  // medida ou por silencio.
  static uint32_t lidasAoArmar[2] = {0, 0};
  const uint32_t agora = millis();

  for (uint8_t k = 1; k <= 2; k++) {
    const uint8_t i = k - 1;

    // Sem leitura, sem julgamento: um cabo solto no encoder nao pode
    // parar o braco no meio de um cordao.
    // Leitura que nao merece confianca nao pode PARAR o braco no meio de
    // um cordao. "Confiavel" aqui e o mesmo criterio de todo o resto:
    // valida, recente e fisicamente possivel.
    if (!leituraConfiavel(k)) { desde[i] = 0; continue; }
    const LeituraEncoder L = encoderLer(k);

    // DOIS CRITERIOS, E O SEGUNDO NAO PRECISA DE ESCALA NENHUMA.
    //
    // Com escala medida da para exigir proporcao: "o eixo entrega menos
    // de um quinto do que deveria". E preciso, e pega ate escorregao
    // parcial.
    //
    // Sem escala medida essa conta sai de dois numeros de catalogo, e um
    // deles errado transforma braco andando em braco travado -- foi o que
    // fazia a maquina parar do nada. Mas ha um sinal que independe de
    // escala: o gerador de pulso claramente correndo e o encoder
    // claramente PARADO. Eixo que gira produz contagem, seja qual for a
    // escala; entao aqui nao existe falso positivo por numero errado.
    const float cpg = configEncoder.contagensPorGrau[i];
    const bool reguaMedida = (cpg > 0.0001f || cpg < -0.0001f);
    const float hz = (k == 1) ? velocidadeJ1Hz() : velocidadeJ2Hz();

    // O CRITERIO SEM ESCALA MANDA, SEMPRE.
    //
    // A conta proporcional parece a melhor das duas, e e -- quando as
    // duas reguas conversam. So que ela divide por passosPorGrau, que
    // vem do CATALOGO (pulsos por volta x reducao), e compara com
    // contagensPorGrau, que foi MEDIDO. Numa maquina em que os dois
    // discordam -- e discordar e o normal antes de calibrar --, o
    // esperado sai varias vezes maior que o real e um eixo andando
    // perfeitamente e declarado travado meio segundo depois de arrancar.
    // Era o "vai bem e no meio do caminho trava".
    //
    // Eixo que gira produz contagem, seja qual for a escala. Entao o
    // teste sem escala -- pulso claramente correndo, encoder claramente
    // PARADO -- e o unico que nao pode mentir por numero errado, e ele
    // passou a ser condicao NECESSARIA. Travamento de verdade passa nos
    // dois: eixo preso nao produz contagem nenhuma.
    if (hz < TRAV_HZ_MINIMO) { desde[i] = 0; continue; }
    if (fabsf(L.velocidade) > TRAV_CONTAGENS_QUIETO) { desde[i] = 0; continue; }

    // Com escala medida a conta proporcional ainda serve para REFINAR:
    // ela pega escorregao parcial que o teste acima deixaria passar. Mas
    // so estreita o criterio -- nunca inventa um travamento sozinha.
    if (reguaMedida) {
      const float esperado = esperadoContagensPorSeg(k);
      // Perto de zero a conta nao distingue eixo parado de eixo travado,
      // e nao precisa: eixo parado nao esta forcando contra nada.
      if (esperado < 200.0f) { desde[i] = 0; continue; }
      if (fabsf(L.velocidade) > esperado * 0.2f) { desde[i] = 0; continue; }
    }

    if (!desde[i]) { desde[i] = agora; lidasAoArmar[i] = L.leituras; continue; }
    // Meio segundo dando pulso sem o eixo responder. A leitura vem a 20
    // Hz: menos que isso seria julgar com duas ou tres amostras.
    //
    // E A JANELA TEM DE ESTAR CHEIA DE MEDIDA, nao so de tempo.
    //
    // Quando uma leitura falha, `velocidade` e zerada de proposito -- a
    // tela nao pode dizer que o eixo gira depois que o fio caiu. Mas esse
    // zero e o encoder CALADO, nao o eixo parado, e o teste acima nao
    // distingue os dois. Como a leitura so deixa de ser confiavel depois
    // de um segundo inteiro, sobrava meio segundo em que uma rajada de
    // falhas enchia a janela sozinha: o braco parava com "Junta travada"
    // no meio do cordao por causa do BARRAMENTO, nao do eixo. Num
    // barramento ruim -- que e exatamente quando isso acontece -- bastavam
    // tres respostas perdidas seguidas.
    //
    // Contando amostras que CHEGARAM, silencio nunca mais acusa: ou o
    // encoder respondeu e disse "parado", ou nao ha o que julgar.
    if ((uint32_t)(agora - desde[i]) > 500 &&
        (uint32_t)(L.leituras - lidasAoArmar[i]) >= TRAV_AMOSTRAS_MINIMAS) {
      desde[i] = 0;
      {
        // Parar o eixo e a acao, nao o aviso: continuar forcando contra o
        // batente aquece o servo e torce a mecanica.
        //
        // Quem observa `trav.total` la no laco principal interrompe o
        // movimento automatico -- programa, trajetoria, posicionamento.
        // Contar sem parar seria o movimento morrendo sem ninguem ter
        // mandado.
        if (trav.ativo) continue;
        trav.ativo = true;
        trav.junta = k;
        trav.total++;
        jogZerar();
        pararSuave();
        definirMensagem("Junta %u travada: o comando anda e o eixo nao. "
                        "Encostou no batente?", (unsigned)k);
      }
    }
  }
}

// ---------------------------------------------------------------------
// Vigilancia. Nao mexe no motor: so conta e avisa.
// ---------------------------------------------------------------------
static uint32_t alertas = 0;

uint32_t correcaoAlertas() { return alertas; }

// ---------------------------------------------------------------------
// Zona morta do ancoramento. O encoder de 17 bits treme; reescrever a
// contagem por centesimos nao muda nada e so gasta ciclo. Cinco
// centesimos de grau esta abaixo de qualquer coisa que o operador veja e
// bem acima do tremor.
static const float ANCORA_MORTA_GRAUS = 0.05f;

Ancoragem ancorarNoEncoder() {
  Ancoragem a = {0.0f, false, 0, false};

  // Nunca sobre eixo andando: reescrever a contagem no meio da rampa
  // muda o destino que o gerador de pulso ja esta perseguindo. Mas isso
  // TEM de aparecer: o movimento seguinte sai pela contagem, e quem
  // pediu um angulo pelo encoder precisa saber que nao foi assim.
  if (motoresEmMovimento() || correcaoEmCurso()) { a.andando = true; return a; }

  for (uint8_t k = 1; k <= 2; k++) {
    Junta& j = (k == 1) ? J1 : J2;
    // Junta sem encoder configurado nao e falha: e uma maquina que se
    // escolheu operar pela contagem, e ali nao ha nada a avisar.
    if (configEncoder.reg[k - 1] == 0) continue;
    if (j.passosPorGrau <= 0.0f) continue;
    // leituraConfiavel() ja cobre validade, idade e possibilidade fisica.
    if (!leituraConfiavel(k)) { a.semLeitura |= (uint8_t)(1u << (k - 1)); continue; }

    a.comEncoder = true;
    const LeituraEncoder L = encoderLer(k);
    const float conta = passosParaGraus(j, (k == 1) ? posicaoJ1() : posicaoJ2());
    const float dif   = L.graus - conta;
    if (fabsf(dif) < ANCORA_MORTA_GRAUS) continue;

    ajustarContagem(j, grausParaPassos(j, L.graus));
    if (fabsf(dif) > a.ajuste) a.ajuste = fabsf(dif);
    logEvento("contagem ancorada no encoder na junta %u: %.2f -> %.2f graus",
              (unsigned)k, (double)conta, (double)L.graus);
  }
  return a;
}

void correcaoVigiar() {
  if (!configCorrecao.vigiar) return;
  vigiarTravamento();

  // Enquanto o eixo anda, comandado e medido divergem de propria conta:
  // o encoder ve onde o eixo ESTA e o firmware conta onde ele MANDOU
  // estar, e entre os dois ha a rampa. Vigiar em movimento acusaria erro
  // que nao existe. So parado.
  if (motoresEmMovimento()) return;
  if (correcaoEmCurso()) return;

  static uint32_t desde[2] = {0, 0};
  static bool     avisado[2] = {false, false};
  const uint32_t agora = millis();

  for (uint8_t k = 1; k <= 2; k++) {
    // Aqui o alvo E o comandado: nao ha retoque em curso, entao a
    // contagem esta parada e as duas medidas coincidem.
    const float cmd = (k == 1) ? passosParaGraus(J1, posicaoJ1())
                               : passosParaGraus(J2, posicaoJ2());
    float e = 0.0f;
    if (!faltaPara(k, cmd, e) || fabsf(e) <= configCorrecao.alertaGraus) {
      desde[k - 1] = 0;
      avisado[k - 1] = false;
      continue;
    }
    if (!desde[k - 1]) { desde[k - 1] = agora; continue; }
    // Um segundo fora do limite, parado: nao e transiente de leitura.
    if (!avisado[k - 1] && (uint32_t)(agora - desde[k - 1]) > 1000) {
      avisado[k - 1] = true;
      alertas++;
      definirMensagem("Junta %u fora de posicao: %+.2f graus pelo encoder",
                      (unsigned)k, (double)e);
    }
  }
}

#ifdef ROBO2DOF_TESTE
void correcaoReiniciarTeste() {
  zImplausivel[0] = zImplausivel[1] = false;
  zGrausImplausivel[0] = zGrausImplausivel[1] = 0.0f;
  memset(&r, 0, sizeof(r));
  esperaAte = 0;
  alvo1Original = alvo2Original = 0;
  alvoGraus1 = alvoGraus2 = 0.0f;
  erroAnterior = 0.0f;
  semProgresso = 0;
  fechavelAnterior = 0.0f;
  esquecerGanho();
  esquecerAfericao();
  alertas = 0;
  // O fator do governador e a memoria de quanto a REGUA daquela maquina
  // exagera. Na maquina ele so some quando ela reinicia; no banco, um
  // cenario nao pode herdar o que o anterior aprendeu, senao a ordem em
  // que os cenarios rodam vira parte do resultado.
  govFator[0] = govFator[1] = 1.0f;
  govAprendeu[0] = govAprendeu[1] = false;
  buscando[0]      = buscando[1]      = false;
  buscaSentido[0]  = buscaSentido[1]  = 0;
  buscaVel[0]      = buscaVel[1]      = 0.0f;
  buscaPassadas[0] = buscaPassadas[1] = 0;
  buscaParando[0]  = buscaParando[1]  = false;
  buscouEstaViagem[0] = buscouEstaViagem[1] = false;
  largadaValida[0] = largadaValida[1] = false;
  temAlvoPedido = false;
  trav.ativo = false; trav.junta = 0; trav.total = 0;
  memset(&z, 0, sizeof(z));
  zeroDesde = 0; zeroComecou = false;
}
#endif
