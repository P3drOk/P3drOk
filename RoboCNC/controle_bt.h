#pragma once
#include "config.h"

// =====================================================================
//  Controle por Bluetooth (aplicativo Dabble, modo GamePad).
//
//  Segue a mesma regra do resto do projeto: este modulo roda no core 0 e
//  NAO toca em motores, rele nem estado. Ele so le o gamepad e enfileira
//  Comando -- exatamente o que os handlers HTTP fazem. Quem executa
//  continua sendo o core 1, com as mesmas validacoes.
//
//  Mapa dos botoes:
//
//    analogico / direcional  jog das duas juntas, proporcional
//    X (cross)               PARADA, fora da fila de comandos
//    triangulo               liga/desliga modo precisao
//    quadrado                grava ponto na posicao atual
//    circulo                 vai para o zero da maquina
//    start                   executa o ENSAIO (sem arco)
//    select                  habilita/desabilita os servos
//
//  O gamepad nao abre arco e nao executa com solda: isso exige a
//  confirmacao da tela. Botao de controle nao e lugar de comandar arco.
// =====================================================================

void btIniciar();
// Chamar de dentro da tarefa de rede (core 0), a cada ciclo.
void btAtualizar();

bool        btConectado();
const char* btNome();
