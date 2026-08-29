#pragma once
#include "config.h"

// =====================================================================
//  CALIBRACAO: QUATRO MARCAS, E NADA MAIS.
//
//  Levar a junta 1 ao limite POSITIVO e marcar; ao NEGATIVO e marcar; o
//  mesmo na junta 2. Acabou. O operador chega em cada limite como
//  preferir -- com torque, pelo jog; ou com os motores soltos,
//  empurrando o braco com a mao.
//
//  Do que foi marcado sai tudo:
//    - o CURSO de cada junta;
//    - o ZERO, que passa a ser o MEIO do curso -- a unica escolha que
//      nao pede numero nenhum;
//    - a ESCALA DO ENCODER em contagens por grau, quando ele leu as duas
//      marcas: entre elas ha um tanto de contagens e um tanto de graus, e
//      a divisao e a escala, com sinal.
//
//  O que continua declarado, e so isto: a REDUCAO do redutor. Ela e
//  mecanica, esta escrita no que se comprou, e com um sensor so antes do
//  redutor nenhuma medida a revela.
//
//  E CALIBRAR E OPCIONAL. Sem limites a maquina opera igual -- ela so
//  fica sem protecao de curso. Nada e recusado por falta de calibracao.
// =====================================================================

void calibIniciar();

// Marca a etapa atual e avanca. Os dois numeros continuam na assinatura
// porque a fila de comandos os carrega, mas nenhum e usado: a calibracao
// nao pergunta nada.
void calibConfirmar(float f1 = 0.0f, float f2 = 0.0f);
void calibCancelar();

// A primeira etapa e a unica em que nada foi medido: e ali, e so ali,
// que trocar o sentido de um eixo nao contradiz uma marca ja feita.
bool calibNaPrimeiraEtapa();

// Esquece os limites gravados nas duas juntas. A maquina continua
// operando -- ela so fica sem protecao de curso. A resolucao (pulsos por
// volta e redutor) NAO e apagada: ela descreve a mecanica, nao a medicao.
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
// AS AFERICOES AVULSAS SAIRAM.
//
// Eram tres telas -- medir a engrenagem eletronica pelo encoder, medir a
// reducao contra um esquadro, ensinar a escala do encoder -- cada uma
// com marca, movimento e um numero a informar. Todas existiam porque a
// calibracao media so os limites e deixava a ESCALA a cargo de dois
// numeros digitados.
//
// A calibracao agora mede a escala do encoder sozinha, das proprias
// marcas: entre o limite positivo e o negativo ha um tanto de contagens
// e um tanto de graus, e a divisao e a escala. Sobrou o redutor, que
// nenhuma medida revela e por isso continua declarado.
// ---------------------------------------------------------------------

void calibAtualizar();   // chamar a cada ciclo do loop

bool calibAtiva();
uint8_t calibEixoAtivo();   // 0 = nenhum, 1 ou 2
