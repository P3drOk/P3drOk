#pragma once
#include "config.h"

// =====================================================================
//  Assistente de calibracao.
//
//  Papel de cada coisa (era o ponto mais confuso do codigo antigo):
//   - passosPorGrau vem da engrenagem eletronica do driver x reducao
//     mecanica. E um numero CONHECIDO, nao medido no olho.
//   - a calibracao mede os LIMITES DE CURSO fisicos de cada junta.
//   Ao final o assistente mostra os limites convertidos em graus para
//   voce conferir se batem com a realidade da maquina.
// =====================================================================

void calibIniciar();

// Confirma a etapa atual do assistente. Os dois numeros sao opcionais e
// mudam de significado conforme a etapa:
//
//   CAL_HOME       angulo REAL de cada junta nesta posicao de referencia.
//                  0 e 0 significa "braco esticado apontando para +X",
//                  que e a postura que a cinematica chama de zero.
//
//   CAL_CONCLUIDO  curso REAL de cada junta, medido com transferidor ou
//                  inclinometro. Zero = nao aferir, mantem a resolucao
//                  que esta nos ajustes.
//
// Nas demais etapas os numeros sao ignorados.
void calibConfirmar(float f1 = 0.0f, float f2 = 0.0f);
void calibCancelar();

// Esquece a calibracao gravada nas duas juntas: limites, referencia e a
// marca de "calibrada". O robo volta ao modo de instalacao, com o jog
// livre e os modos automaticos recusados ate calibrar de novo.
// A resolucao (pulsos por volta e reducao) NAO e apagada: ela descreve a
// mecanica, nao a medicao.
void calibApagar();

// O braco esta FISICAMENTE na posicao de referencia da calibracao:
// sincroniza a contagem com ela. E o que resolve o caso de alguem ter
// movido o braco a mao com os servos desligados -- o eixo andou e o
// contador nao.
//
// Nao serve para "zerar em qualquer lugar": os limites de curso sao
// contados a partir da origem, entao referenciar num ponto diferente
// desloca fisicamente toda a area util.
void calibReferenciar();

// ---------------------------------------------------------------------
// AFERICAO AVULSA DE UMA JUNTA
//
// Sem refazer a calibracao inteira: marca a contagem, o operador move o
// eixo o quanto quiser, mede com transferidor e informa quantos graus
// foram. Sai a resolucao real daquele eixo.
// ---------------------------------------------------------------------
void aferirMarcar(uint8_t junta);

// AFERIR A ENGRENAGEM ELETRONICA PELO ENCODER, sem transferidor.
//
// A resolucao de uma junta e:
//
//     passosPorGrau = passosPorVolta * reducao / 360
//
// Dois numeros, e cada um erra de um jeito. A REDUCAO e mecanica: esta
// no redutor, o operador sabe qual comprou, e o encoder do servo NAO
// consegue medi-la -- ele conta no eixo do MOTOR, antes do redutor.
//
// Ja a ENGRENAGEM ELETRONICA (passosPorVolta) e um parametro do driver,
// e e o que mais se erra: troca-se o driver, refaz-se um parametro, e o
// numero declarado aqui deixa de bater com o que o driver faz. O
// sintoma e o braco andar menos (ou mais) do que a tela diz, sem nada
// apontar para o culpado.
//
// Isso o encoder mede sozinho: manda-se um tanto conhecido de PASSOS e
// pergunta-se ao encoder quantas VOLTAS DO MOTOR aconteceram.
//
//     passosPorVolta = passos andados / voltas do motor
//
// Some um dos dois numeros da conta. A reducao continua sendo declarada
// por quem montou a maquina -- mas com um numero a menos para errar.
bool aferirPelosEncoder(uint8_t junta);

// Ensina a ESCALA do encoder: quantas contagens ele dá por GRAU da junta.
//
// O caminho antigo tira o ângulo de dois números digitados -- contagens
// por volta do motor e redução da engrenagem. Errar qualquer um sai em
// ângulo com escala errada, e nada na tela denuncia: o braço em 90 graus
// mostra 47, ou 300.
//
// Aqui o número sai da própria máquina: marque, leve o braço até um
// ângulo que você CONHECE (o batente, um esquadro, um transferidor), e
// diga quantos graus ele andou. A conta é uma divisão.
//
// O sinal vem junto: encoder que conta para trás enquanto a junta avança
// dá escala negativa, e o ângulo sai certo sem chave de inversão.
bool ensinarEscalaEncoder(uint8_t junta, float grausAndados);
// ---------------------------------------------------------------------
// AFERIR A REDUCAO MECANICA -- pelo encoder, contra UMA referencia
//
// LEIA ISTO ANTES DE ACHAR QUE O ENCODER RESOLVE SOZINHO.
//
// O encoder esta no eixo do MOTOR, antes do redutor. O angulo que ele
// mostra na tela ja e calculado ASSIM:
//
//     graus da junta = voltas do motor * 360 / reducao
//
// Ou seja: o angulo lido JA DEPENDE da reducao. Nao da para descobrir a
// reducao a partir dele -- seria tirar o numero de uma conta que usa o
// proprio numero. Isso e fisica, nao limitacao de software: com um unico
// sensor antes do redutor, a relacao do redutor e invisivel.
//
// O que o encoder da de graca, e com muita precisao, e a contagem de
// VOLTAS DO MOTOR. Falta uma referencia do lado da JUNTA -- uma, so uma.
// E com ela sai a reducao exata:
//
//     reducao = voltas do motor * 360 / angulo real da junta
//
// POR QUE ISSO E MUITO MELHOR QUE O QUE EXISTIA. A afericao antiga
// contava PULSOS COMANDADOS. Ela erra junto com a engrenagem eletronica
// (se passosPorVolta estiver errado, a reducao sai errada na mesma
// proporcao) e erra junto com perda de passo (o eixo escorrega e a conta
// nem fica sabendo). Contar voltas reais do motor nao tem nenhum dos
// dois problemas: o encoder mede o eixo, nao a intencao.
//
// DE ONDE TIRAR A REFERENCIA, da melhor para a pior:
//
//   1. ESQUADRO (90 graus). Um esquadro de carpinteiro da 90 graus com
//      precisao muito boa e todo mundo tem um. Encoste, marque, gire ate
//      a outra face, aplique 90. E o metodo recomendado.
//   2. CURSO ENTRE BATENTES. O maior angulo disponivel na maquina, e
//      quanto maior o angulo, menor o erro relativo da medida. Precisa
//      do curso real, medido uma vez.
//   3. VOLTA COMPLETA, se a junta der uma. Nao precisa de instrumento
//      nenhum: basta reconhecer que voltou ao mesmo lugar, e a reducao
//      e o numero de voltas do motor.
//
// Depois de aplicada, CONFIRA: mande o braco um tanto conhecido e veja
// se o encoder concorda. E o que fecha o laco e denuncia engrenagem
// eletronica errada.
// ---------------------------------------------------------------------
bool aferirReducaoPeloEncoder(uint8_t junta, float grausReais);

// Quantas voltas o MOTOR deu desde a marca, pelo encoder. Vai para a
// tela enquanto o operador move o eixo: ver o numero subir e o que
// mostra que a medida esta acontecendo.
float aferirVoltasDesde(uint8_t junta);
bool  aferirTemMarcaBoa(uint8_t junta);

bool aferirAplicar(uint8_t junta, float grausReais);
long aferirPassosDesde(uint8_t junta);   // quanto andou desde a marca
void calibAtualizar();   // chamar a cada ciclo do loop

bool calibAtiva();
uint8_t calibEixoAtivo();   // 0 = nenhum, 1 ou 2
