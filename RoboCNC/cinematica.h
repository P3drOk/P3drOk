#pragma once
#include "estado.h"

// =====================================================================
//  Conversoes, cinematica e - o mais importante - validacao de postura.
//
//  Toda ordem de movimento do sistema passa por posturaValida().
//  Nenhum caminho de codigo move um motor sem essa checagem.
// =====================================================================

float passosParaGraus(const Junta& j, long passos);
long  grausParaPassos(const Junta& j, float graus);

// Cinematica direta: posicao do cotovelo e da ponta, em mm.
void cinematicaDireta(float t1, float t2,
                      float& xCotovelo, float& yCotovelo,
                      float& xPonta,    float& yPonta);

// Cinematica inversa. 'cotoveloCima' escolhe entre as duas solucoes.
// Retorna false se o ponto for inalcancavel.
bool cinematicaInversa(float x, float y, bool cotoveloCima,
                       float& t1, float& t2);

// Resolve XY preferindo a solucao que passa em posturaValida() e que
// exige menos deslocamento a partir da postura atual.
bool resolverXY(float x, float y, float t1Atual, float t2Atual,
                float& t1, float& t2, const char** motivo);

// Verificacao unica de postura: curso de cada junta, dobra minima do
// cotovelo (auto-colisao entre os elos) e envelope cartesiano.
bool posturaValida(float t1, float t2, const char** motivo);

// Mesma checagem, recebendo passos.
bool posturaValidaPassos(long p1, long p2, const char** motivo);

// Verifica a interpolacao NAS JUNTAS entre duas posturas - o caminho que
// moverCoordenado() realmente percorre. Os limites de curso sao caixas no
// espaco das juntas e nao precisariam disso, mas o envelope cartesiano
// nao e convexo nesse espaco: da para ir de um ponto valido a outro
// mergulhando a ponta na mesa no meio do trajeto.
bool caminhoJuntasValido(float t1a, float t2a, float t1b, float t2b,
                         const char** motivo);
bool caminhoJuntasValidoPassos(long p1a, long p2a, long p1b, long p2b,
                               const char** motivo);

// Quanto a postura viola os limites, em "graus equivalentes".
// 0 = dentro. Serve para permitir movimento de RECUPERACAO quando o
// braco ja esta fora da regiao valida: o que importa nao e se o destino
// e valido, e se ele esta MENOS errado que a posicao atual.
float gravidadeViolacao(float t1, float t2);
float gravidadeViolacaoPassos(long p1, long p2);
