#pragma once
#include "estado.h"

// =====================================================================
//  Rede. Roda no core 0, junto do servidor web.
//
//  A maquina tem Wi-Fi PROPRIO e so isso. Ela nao entra na rede de
//  ninguem, nao procura roteador e nao fala com a internet.
//
//  Ja houve aqui um modo estacao (entrar na rede da oficina). Ele foi
//  removido por um motivo tecnico, nao por gosto: o ESP32 tem UM radio.
//  Em AP+STA o ponto de acesso e obrigado a acompanhar o canal do
//  roteador e o radio passa a dividir tempo entre as duas redes. Isso
//  aparece como atraso e tremor no joystick -- e o heartbeat do jog e
//  justamente o trafego que nao pode atrasar. Rede de terceiro nao vale
//  latencia no controle de uma maquina que se move.
//
//  Nada aqui toca motor, rele ou estado de movimento.
// =====================================================================

void redeIniciar();     // chamar no setup, antes de servidorIniciar()
void redeAtender();     // chamar no laco da tarefa de rede (core 0)

const char* redeIpAcesso();      // IP fixo do ponto de acesso
const char* redeNomeLocal();     // nome mDNS, sem o ".local"
