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
bool aferirAplicar(uint8_t junta, float grausReais);
long aferirPassosDesde(uint8_t junta);   // quanto andou desde a marca
void calibAtualizar();   // chamar a cada ciclo do loop

bool calibAtiva();
uint8_t calibEixoAtivo();   // 0 = nenhum, 1 ou 2
