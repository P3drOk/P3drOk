#pragma once
#include "config.h"

// =====================================================================
//  Programa de solda por PONTOS ENSINADOS.
//
//  Modelo mental: voce leva o braco ate um ponto e grava. Depois marca,
//  para cada trecho entre dois pontos, se o arco fica aberto ou nao.
//  Um cordao reto numa chapa = 2 pontos e 1 trecho com solda ligada.
//
//  E o oposto da gravacao a mao livre: da reta de verdade, da para
//  editar depois e da para ensaiar sem arco antes de queimar material.
// =====================================================================

struct Ponto {
  int32_t p1;                // passos da junta 1
  int32_t p2;                // passos da junta 2
  uint8_t soldaAteProximo;   // arco aberto no trecho ate o proximo ponto
};

bool    progAdicionarPonto(long p1, long p2, const char** motivo);
bool    progRemoverPonto(uint8_t indice);
void    progDefinirSolda(uint8_t indice, bool ligar);
void    progLimpar();

uint8_t      progQuantidade();
const Ponto* progLista();

// ensaio = true executa o percurso inteiro com o arco desligado.
bool    progIniciar(bool ensaio, const char** motivo);
void    progAtualizar();
void    progParar();
bool    progRodando();
bool    progEmEnsaio();
uint8_t progIndiceAtual();
uint8_t progProgresso();     // 0..100
