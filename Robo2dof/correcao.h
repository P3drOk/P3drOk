#pragma once
#include "estado.h"

// =====================================================================
//  CORRECAO DE POSICAO PELO ENCODER
//
//  O que isto e, e o que NAO e.
//
//  NAO e malha fechada de servo. Cada leitura Modbus custa de 5 a 20 ms
//  com jitter, o que da umas 20 amostras por segundo. Com isso NAO da
//  para corrigir o eixo enquanto ele anda -- a correcao chegaria tarde
//  demais e faria o braco oscilar.
//
//  E o ASSENTAMENTO no fim do movimento: o braco chega, para, o sistema
//  le onde ele REALMENTE parou, e da um retoque curto. E exatamente o
//  que resolve o incomodo do operador -- "saio de uma posicao e volto, e
//  ela nao e mais a mesma".
//
//  Sem isto o erro de um movimento entra no proximo, e no proximo, e o
//  desvio cresce sem nunca voltar. Com isto, cada parada zera a conta.
//
//  REGRAS DE SEGURANCA, todas obrigatorias antes de mexer no motor:
//
//   1. So com o eixo PARADO. Retoque em cima de eixo andando e briga
//      com a rampa de movimento.
//   2. So com leitura VALIDA e RECENTE. Corrigir por dado velho e mover
//      o braco baseado em onde ele estava, nao onde esta.
//   3. So dentro do curso, QUANDO o curso esta valendo. Com o limite
//      de curso ligado o retoque nunca empurra o eixo para fora dele --
//      e ajuste fino, nao excecao as protecoes. Com o limite desligado
//      o braco anda livre pela mesa e o curso medido nao o prende (ver
//      R143 e R149: prender o retoque num curso fora de vigor -- pior,
//      num curso medido pela metade -- era um dos jeitos de o braco nao
//      chegar ao angulo pedido).
//   4. NUNCA com a solda ligada. Um retoque no meio do cordao estraga o
//      cordao, e o operador nao pediu por ele ali.
//   5. Erro GRANDE se fecha EM PASSOS, nunca de uma vez.
//      maxCorrecaoGraus e o tamanho maximo de UM retoque, nao um motivo
//      para desistir: cada passo anda no maximo isso, le o encoder de
//      novo e repete. A intencao da regra original continua de pe -- o
//      braco nunca lunga varios graus achando que esta consertando --,
//      mas RECUSAR deixava o braco parado no tanto do erro, que era
//      exatamente o defeito (R149).
//   6. Insiste enquanto APROXIMA. Contar tentativas absolutas desistia
//      no meio de uma convergencia saudavel. O que denuncia acoplamento
//      solto nao e o numero de retoques: e o retoque nao diminuir o
//      erro. Enquanto cada passo aproxima pelo menos 15%, continua;
//      parou de aproximar, desiste e diz. Ha um teto absoluto de 40 so
//      para nunca existir laco infinito no core 1.
//   7. A chegada e SUAVE. A velocidade do retoque acompanha o que
//      falta: meia velocidade longe, afinando ate o minimo da maquina.
//      O ultimo decimo de grau e um encosto, nao um tranco.
//   8. O retoque APRENDE A ESCALA da propria maquina. Ele anda em
//      pulsos (graus / passosPorGrau, do catalogo) mas o erro e medido
//      em graus do ENCODER; quando as duas reguas discordam, pedir dois
//      graus faz o eixo andar quatro, e o assentamento passa do ponto
//      sem nunca fechar. Nao ha por que adivinhar essa razao: o retoque
//      anterior a mede -- pediu tanto, o encoder andou tanto. O
//      seguinte ja sai dividido por ela. Ver R156.
// =====================================================================

// Em que pe esta o assentamento. Vai para a tela.
enum EstadoCorrecao : uint8_t {
  CORR_PARADA = 0,   // nao ha nada em curso
  CORR_ESPERANDO,    // esperando leitura fresca depois da parada
  CORR_RETOCANDO,    // movendo o retoque
  CORR_PRONTA,       // chegou dentro da tolerancia
  CORR_DESISTIU,     // tentou o bastante e nao fechou
  CORR_RECUSADA      // erro grande demais, ou sem leitura: nao mexeu
};

struct ResumoCorrecao {
  uint8_t estado;        // EstadoCorrecao
  uint8_t tentativas;    // quantos retoques ja foram dados
  float   erroInicial1;  // erro medido ao chegar, em graus da junta
  float   erroInicial2;
  float   erroFinal1;    // erro depois do assentamento
  float   erroFinal2;
  uint32_t totalOk;      // quantos assentamentos fecharam, desde o boot
  uint32_t totalDesistiu;
  char    motivo[48];    // por que recusou ou desistiu, em portugues
};

// Chamada quando um movimento COMECA. Sem isto o resultado do
// assentamento anterior fica de pe, o "ja terminei" do movimento passado
// vale para o novo, e a correcao roda uma vez so na vida da maquina.
void correcaoNovoMovimento();

// ---------------------------------------------------------------------
// AFERIR A ENGRENAGEM ELETRONICA DO DRIVER a partir de um deslocamento que
// ja aconteceu: sairam tantos pulsos, o encoder girou tantas contagens.
//
//     passosPorVolta = |pulsos| x contagensPorVolta / |contagens|
//
// Pulso e contagem estao os dois do lado do MOTOR, antes do redutor, entao
// a reducao cancela e nao precisa estar certa -- nem precisa haver
// calibracao guiada.
//
// ELA NAO ADOTA O VALOR AQUI. Guarda em Junta.ppvMedido e conta quantas
// medidas seguidas concordaram (Junta.ppvAcordo). Quem adota e
// adotarEngrenagemMedida(), com o braco parado.
//
// A versao que escrevia passosPorVolta no fim de cada movimento -- ou
// seja, DENTRO do ciclo de assentamento -- fazia o braco passar do ponto
// e nunca chegar: o alvo e congelado em GRAUS, e mudar a regua depois
// disso muda o lugar que aquele numero descreve.
//
// Devolve sempre false: gravar nao e assunto desta funcao.
// ---------------------------------------------------------------------
bool aferirEngrenagem(uint8_t junta, long dPasso, int32_t dCont);

// ---------------------------------------------------------------------
// Adota a engrenagem medida, se houver medida estavel e ela discordar do
// que esta configurado. Devolve true quando mudou alguma coisa.
//
// CHAME COM O BRACO PARADO E ANTES DE CALCULAR UM DESTINO. Adotar no
// meio de um movimento move o lugar que o alvo congelado descreve; adotar
// antes dele faz a regua e o destino nascerem juntos.
// ---------------------------------------------------------------------
bool adotarEngrenagemMedida();

void correcaoIniciar();                  // core 1: chegou, comeca a assentar
void correcaoAtualizar();                // core 1: chamada do loop
bool correcaoEmCurso();
void correcaoCancelar();                 // parada de emergencia, troca de modo
ResumoCorrecao correcaoResumo();

// Vigilancia: compara comandado com medido o tempo todo e avisa quando a
// diferenca passa do limite por tempo demais. Nao mexe no motor -- so
// conta e avisa. E o que transforma "o cordao saiu torto" em "a junta 1
// perdeu 2 graus as 14h32".
void correcaoVigiar();
uint32_t correcaoAlertas();

// =====================================================================
//  ANCORAR A CONTAGEM ANTES DE CALCULAR UM DESTINO
//
//  "Ir para zero" e "ir para um angulo" mandam um destino ABSOLUTO em
//  pulsos, calculado sobre a contagem que o firmware mantem. Se essa
//  contagem estiver adiantada do braco -- perda de passo, folga, escala
//  recem-medida --, mover a contagem ate o alvo deixa o BRACO parado no
//  tanto do erro -- o braco desenhado, que segue o encoder, nao chega ao
//  angulo pedido. (Houve um fantasma tracejado que desenhava a contagem
//  ao lado do braco e tornava isso literal na tela; ele saiu, porque
//  durante todo movimento a contagem vai a frente pela rampa e o
//  tracejado piscava a cada viagem.)
//
//  Por isso, antes de converter o angulo em pulsos, a contagem e
//  reescrita pelo que o encoder mede. Dali em diante ela descreve o
//  braco, e o destino calculado sobre ela leva o BRACO ao lugar.
//
//  Nao e o assentamento: aquele conserta DEPOIS de chegar, e so se
//  estiver ligado. Este arruma o ponto de PARTIDA, que e de onde o erro
//  vinha.
//
//  E DIZ POR QUE, quando nao da.
//
//  Devolver so um numero escondia a diferenca entre "nao havia o que
//  corrigir" e "nao consegui ler o encoder" -- e nos dois casos o
//  movimento saia igual, pela contagem, calado. Para quem pediu um
//  angulo baseado no encoder, essas duas coisas nao podem ter a mesma
//  cara na tela.
// =====================================================================
struct Ancoragem {
  float   ajuste;      // maior correcao aplicada, em graus da junta
  bool    comEncoder;  // ao menos uma junta foi ancorada no encoder
  uint8_t semLeitura;  // juntas COM encoder que nao deram leitura: bit0=J1, bit1=J2
  bool    andando;     // recusou porque o eixo nao estava parado
};
Ancoragem ancorarNoEncoder();

// =====================================================================
//  TRAVAMENTO: o eixo foi mandado andar e nao andou
//
//  Antes do encoder, encostar no batente era invisivel para o firmware:
//  ele continuava contando pulsos, o driver continuava recebendo, e o
//  motor ficava forcando contra o ferro. A calibracao existe justamente
//  para o operador ensinar onde o batente esta -- e enquanto ele ensina,
//  e ele quem tem de perceber a batida no olho e no ouvido.
//
//  Com o encoder isso e mensuravel: comandado andando + medido parado =
//  o eixo encostou em alguma coisa (ou perdeu o acoplamento, ou o driver
//  desarmou). O sistema para o eixo e diz.
//
//  E deliberadamente CONSERVADOR. Um falso positivo para o braco no meio
//  de um cordao, o que estraga a peca -- entao so acusa quando o
//  comandado esta claramente andando e o medido esta claramente parado,
//  por tempo de sobra.
// =====================================================================
struct Travamento {
  bool     ativo;        // ha travamento agora
  uint8_t  junta;        // 1 ou 2
  uint32_t total;        // quantos desde o boot
};
// =====================================================================
//  LOCALIZAR-SE AO LIGAR
//
//  Com encoder absoluto a maquina nao precisa de fim de curso nem de
//  procurar batente: a contagem crua do encoder ja diz onde o braco
//  esta, mesmo que alguem o tenha empurrado a mao com tudo desligado.
//
//  No boot, assim que houver leitura boa, a contagem de passos e
//  acertada para bater com o encoder. Dali em diante tudo o que ja
//  existia -- limites, cinematica, programa -- funciona igual, so que
//  partindo do lugar certo em vez de partir de "zero e onde eu liguei".
//
//  E, se o operador quiser, o braco vai para 0 grau em seguida. Isso SO
//  acontece depois que ele habilita os servos, que e uma acao explicita
//  na tela: enquanto ninguem habilitar, o braco nao tem como andar.
// =====================================================================
enum EstadoZero : uint8_t {
  ZERO_ESPERANDO = 0,  // ainda sem leitura boa do encoder
  ZERO_LOCALIZADO,     // contagem acertada pelo encoder
  ZERO_INDO,           // a caminho de 0 grau
  ZERO_PRONTO,         // no zero (ou nao foi pedido para ir)
  ZERO_SEM_ENCODER     // desistiu: nao ha leitura, segue como antes
};
struct ResumoZero {
  uint8_t estado;
  bool    localizou[2];
  float   graus[2];     // onde o encoder disse que estava, ao ligar
  char    motivo[48];
};
// =====================================================================
//  SEGUIR O EIXO MOVIDO A MAO
//
//  Com os servos DESLIGADOS o braco fica solto e o operador o leva com a
//  mao -- para ensinar um ponto, para tirar da frente, para conferir uma
//  peca. O eixo anda e nenhum pulso saiu no fio, entao a contagem do
//  firmware fica para tras.
//
//  O encoder ve. Este seguidor acerta a contagem enquanto o braco esta
//  solto, e e o que faz "movi com a mao" e "mandei ir" darem o mesmo
//  resultado.
//
//  SO COM OS SERVOS DESLIGADOS. Com servo ligado o motor segura a
//  posicao: se o eixo saiu do lugar mesmo assim, isso e PERDA DE PASSO,
//  nao movimento a mao. Seguir ali esconderia o defeito e o assentamento
//  nunca traria o braco de volta -- seria trocar uma correcao por um
//  disfarce.
// =====================================================================
// A leitura desta junta serve para alguma coisa?
//
// Junta o que ja era conferido em tres lugares diferentes: leitura
// valida, recente, e FISICAMENTE possivel -- dentro do curso que o
// proprio operador mediu, com folga. Uma leitura de 300 graus num braco
// de +/-90 nao e posicao, e defeito: nao pode virar contagem, e nao pode
// aparecer na tela como se fosse medida boa.
bool       leituraConfiavel(uint8_t junta);

void       seguirEixoSolto();    // core 1: chamada do loop

void       zeroAtualizar();      // core 1: chamada do loop
ResumoZero zeroResumo();

Travamento correcaoTravamento();
void correcaoLimparTravamento();

#ifdef ROBO2DOF_TESTE
void correcaoReiniciarTeste();
#endif
