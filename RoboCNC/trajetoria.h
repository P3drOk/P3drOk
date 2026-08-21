#pragma once
#include "config.h"

// =====================================================================
//  Gravacao e reproducao de trajetoria.
//
//  O que a versao anterior fazia: guardava UM ponto final e ia ate ele.
//  O que este modulo faz: grava o CAMINHO inteiro amostrado no tempo,
//  junto com o estado do rele de solda em cada instante, e reproduz
//  seguindo o setpoint interpolado - que e como uma CNC de verdade
//  funciona e o que torna o movimento continuo em vez de "anda e para".
// =====================================================================

void trajLimpar();

bool trajIniciarGravacao();
void trajAmostrar(long p1, long p2, bool solda);  // chamar a cada ciclo
void trajPararGravacao();
bool trajGravando();

bool trajIniciarReproducao(const char** motivo);
void trajAtualizarReproducao();   // chamar a cada ciclo
void trajPararReproducao();
bool trajReproduzindo();

uint16_t trajPontos();
uint32_t trajDuracaoMs();
uint8_t  trajProgresso();          // 0..100

const Waypoint* trajBuffer();
