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
void calibAtualizar();   // chamar a cada ciclo do loop

bool calibAtiva();
uint8_t calibEixoAtivo();   // 0 = nenhum, 1 ou 2
