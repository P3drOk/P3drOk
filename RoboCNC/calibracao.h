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
void calibConfirmar();
void calibCancelar();
void calibAtualizar();   // chamar a cada ciclo do loop

bool calibAtiva();
uint8_t calibEixoAtivo();   // 0 = nenhum, 1 ou 2
