#pragma once
#include "config.h"

// =====================================================================
//  Controle do rele de solda.
//
//  Regra: existe UMA porta de saida para o rele. Tudo que possa dar
//  errado no resto do sistema chama soldaDesligar(). Nunca escreva
//  digitalWrite(PIN_RELE_SOLDA, ...) fora deste modulo.
// =====================================================================

void soldaIniciar();

// Liga/desliga. Ligar so acontece se o intertravamento permitir.
void soldaDefinir(bool ligar);
void soldaDesligar();

bool soldaLigada();

// Bloqueia a solda enquanto a condicao de seguranca nao for satisfeita
// (drivers desabilitados, emergencia, sem conexao...).
void soldaPermitir(bool permitido);

// Chamado todo ciclo do loop: corta o arco se passar do tempo maximo
// ou se o pulso de teste terminar.
void soldaAtualizar();

// TESTE DE BANCADA: aciona a saida por alguns milissegundos ignorando o
// intertravamento, para conferir fiacao e rele sem habilitar os servos.
// Tem tempo limite proprio e nunca fica ligado sozinho.
void soldaTestar(uint32_t duracaoMs);
