#pragma once
#include "estado.h"

// =====================================================================
//  Camada de movimento. SO PODE SER CHAMADA PELO CORE 1 (loop).
// =====================================================================

bool motoresIniciar();

// Habilita/desabilita o sinal SON dos dois drivers.
// Desabilitar tira o torque: use apenas com o braco apoiado.
// 'junta' e 1, 2, ou 0 para as duas. Por junta porque cada driver e um
// escravo Modbus proprio, e uma bancada com um driver ligado tem de
// conseguir trabalhar naquele eixo.
void servosHabilitar(bool ligar, uint8_t junta = 0);

// Confere, a cada ciclo do core 1, o que o barramento respondeu ao
// ultimo pedido de habilita. Devolve true quando um DESABILITAR nao
// confirmou -- caso em que a maquina tem de cair em FALHA: nao se sabe
// se o eixo esta energizado e nao ha segundo caminho para cortar.
// 'habilitouAgora' sai true no ciclo em que o habilita foi confirmado.
bool servosSupervisionar(bool& habilitouAgora);

// Le os pinos ALM dos drivers. Retorna true se houver alguma falha.

void aplicarVelocidadeManual();
void aplicarAceleracao();
// Reaplica o sentido de cada eixo. Chamar depois de mudar inverterDir.
void aplicarSentido();
// Aplica a suavidade de partida (limite de jerk) nos dois eixos.
void aplicarSuavidade();

// Converte graus/s naquilo que o gerador de pulso entende, com o
// passosPorGrau daquela junta e o teto do driver.
uint32_t grausPorSegParaHz(const Junta& j, float grausPorS);

// A velocidade escolhida ja multiplicada pelo fator daquela junta (ver
// Junta.fatorVel em estado.h). Quem mexe na velocidade de um eixo
// sozinho precisa dela para nao sair do ritmo do resto.
float velDaJuntaPub(const Junta& j, float base);

bool motoresEmMovimento();
float velocidadeJ1Hz();
float velocidadeJ2Hz();
long posicaoJ1();
long posicaoJ2();
void zerarPosicoes();
// Reajusta a contagem sem mover o eixo (nenhum pulso sai). Ver motores.cpp.
void ajustarContagem(Junta& j, long passos);

// ---------------------------------------------------------------------
// JOG
// ---------------------------------------------------------------------
// Registra a intencao de jog. O movimento so acontece em jogAtualizar(),
// que revalida a postura a cada ciclo e exige heartbeat da interface.
//
// 'fracao' e a intensidade, de 0 a 1, e existe para o joystick da
// interface: o botao de seta manda 1 (velocidade cheia configurada), o
// disco manda o quanto o dedo se afastou do centro. Cada eixo tem sua
// propria fracao, entao o movimento sai na direcao real do polegar.
void jogDefinir(uint8_t junta, int8_t direcao, float fracao = 1.0f);
void jogAtualizar();
void jogZerar();

// ---------------------------------------------------------------------
// MOVIMENTO COORDENADO
// ---------------------------------------------------------------------
// Move as duas juntas de forma que ELAS CHEGUEM JUNTAS: a velocidade e a
// aceleracao de cada eixo sao escaladas pela razao dos deslocamentos.
// E isso que faz o caminho ser previsivel em vez de um "L".
// 'grausPorS' e a velocidade ANGULAR do eixo que tem mais caminho a
// percorrer; o outro e escalado para os dois chegarem juntos.
void moverCoordenado(long alvo1, long alvo2, float grausPorS);

// Seguimento de setpoint, usado na reproducao de trajetoria: reemite o
// alvo a cada ciclo sem esperar a parada, o que da movimento continuo.
void seguirSetpoint(long alvo1, long alvo2, uint32_t vel1, uint32_t vel2);

// ---------------------------------------------------------------------
// PARADAS
// ---------------------------------------------------------------------
void pararSuave();     // desacelera pela rampa configurada
void pararEmergencia(); // desacelera, corta a solda e volta para MANUAL
