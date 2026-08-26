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

// Substitui o programa inteiro de uma vez (carga de arquivo). Valida
// TODOS os pontos antes de tocar no programa vivo: arquivo com um ponto
// fora da area util nao apaga o programa que estava na maquina.
bool    progCarregarDe(const Ponto* origem, uint8_t n, const char** motivo);
bool    progRemoverPonto(uint8_t indice);

#ifdef ROBO2DOF_TESTE
// So para o banco: em que fase da maquina de estados o programa esta.
uint8_t progFaseTeste();
#endif
void    progDefinirSolda(uint8_t indice, bool ligar);
void    progLimpar();

uint8_t      progQuantidade();
const Ponto* progLista();

// ---------------------------------------------------------------------
// Conferencia de um trecho, para a interface avisar ENQUANTO o operador
// ensina, em vez de o problema so aparecer ao apertar Executar.
//
// Devolve false e preenche 'aviso' quando o trecho i -> i+1 nao e
// percorrivel: reta cartesiana se houver solda, interpolacao nas juntas
// se for deslocamento.
// ---------------------------------------------------------------------
bool progConferirTrecho(uint8_t i, char* aviso, size_t tam);

// ensaio = true executa o percurso inteiro com o arco desligado.
// ---------------------------------------------------------------------
// PAUSA E RETOMADA
//
// Parar no meio de um cordao e recomecar do inicio queima a peca duas
// vezes no mesmo lugar. Pausar guarda ONDE o cordao estava -- em que
// trecho e a que fracao dele -- e retomar continua dali.
//
// O ARCO FECHA NA PAUSA, sempre. Arco aberto com o braco parado fura a
// chapa em segundos: nao existe pausa "segurando o arco". Ao retomar,
// ele reabre com o mesmo tempo de abertura do inicio de qualquer cordao.
// ---------------------------------------------------------------------
bool progPausar(const char** motivo);
bool progRetomar(const char** motivo);
bool progPausado();
uint8_t progFracaoTrecho();      // 0..100 dentro do trecho atual

// ---------------------------------------------------------------------
// DESFAZER
//
// Um nivel. Cobre o estrago que nao tem volta pela tela: "Apagar
// programa" com trinta pontos ensinados a mao, ou um ponto removido por
// engano no meio de um cordao. Guarda o programa inteiro antes de cada
// alteracao -- 120 pontos sao pouco mais de 1 kB de RAM, e refazer o
// ensino custa meia hora do operador.
// ---------------------------------------------------------------------
bool        progDesfazer(const char** motivo);
bool        progTemDesfazer();
const char* progDescricaoDesfazer();

bool    progIniciar(bool ensaio, const char** motivo);
void    progAtualizar();
void    progParar();
bool    progRodando();
bool    progEmEnsaio();
uint8_t progIndiceAtual();
uint8_t progProgresso();     // 0..100
