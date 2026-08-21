#pragma once
#include "estado.h"

// =====================================================================
//  Camada de movimento. SO PODE SER CHAMADA PELO CORE 1 (loop).
// =====================================================================

bool motoresIniciar();

// Habilita/desabilita o sinal SON dos dois drivers.
// Desabilitar tira o torque: use apenas com o braco apoiado.
void servosHabilitar(bool ligar);

// Le os pinos ALM dos drivers. Retorna true se houver alguma falha.
bool motoresLerAlarmes();

void aplicarVelocidadeManual();
void aplicarAceleracao();

bool motoresEmMovimento();
float velocidadeJ1Hz();
float velocidadeJ2Hz();
long posicaoJ1();
long posicaoJ2();
void zerarPosicoes();

// ---------------------------------------------------------------------
// JOG
// ---------------------------------------------------------------------
// Registra a intencao de jog. O movimento so acontece em jogAtualizar(),
// que revalida a postura a cada ciclo e exige heartbeat da interface.
void jogDefinir(uint8_t junta, int8_t direcao);
void jogAtualizar();
void jogZerar();

// ---------------------------------------------------------------------
// MOVIMENTO COORDENADO
// ---------------------------------------------------------------------
// Move as duas juntas de forma que ELAS CHEGUEM JUNTAS: a velocidade e a
// aceleracao de cada eixo sao escaladas pela razao dos deslocamentos.
// E isso que faz o caminho ser previsivel em vez de um "L".
void moverCoordenado(long alvo1, long alvo2, uint32_t velJunta);

// Seguimento de setpoint, usado na reproducao de trajetoria: reemite o
// alvo a cada ciclo sem esperar a parada, o que da movimento continuo.
void seguirSetpoint(long alvo1, long alvo2, uint32_t vel1, uint32_t vel2);

// ---------------------------------------------------------------------
// PARADAS
// ---------------------------------------------------------------------
void pararSuave();     // desacelera pela rampa configurada
void pararEmergencia(); // desacelera, corta a solda e volta para MANUAL
