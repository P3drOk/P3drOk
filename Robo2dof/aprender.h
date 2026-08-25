#pragma once
#include "estado.h"

// =====================================================================
//  MODO APRENDIZADO -- ensinar o caminho com a mao
//
//  O jeito antigo de montar um programa era levar o braco com as setas
//  da tela e apertar "gravar ponto" no navegador: o operador olhava para
//  a peca, andava ate o computador, apertava, voltava. Cada ponto era
//  uma viagem.
//
//  Aqui e um botao so, na maquina, com dois gestos:
//
//    SEGURAR 1,5 s   entra (ou sai) do modo aprendizado.
//    TOQUE CURTO     grava o ponto onde a ponta esta AGORA.
//
//  Dentro do modo, se der para soltar o braco, o torque cai e o operador
//  leva a ponteira com a mao: encosta no comeco do cordao, toca; leva ao
//  fim, toca. O programa nasce da peca, nao da tela.
//
//  O QUE FAZ ISSO SER POSSIVEL. Motor solto anda sem que nenhum pulso
//  saia no fio -- a contagem do firmware ficaria parada e todo ponto
//  gravado sairia no lugar errado. Quem resolve e o encoder absoluto:
//  seguirEixoSolto() (correcao.h) acerta a contagem enquanto o braco
//  esta solto, e o ponto gravado e onde a ponta REALMENTE esta.
//
//  Por isso o braco so e solto quando AS DUAS juntas sao acompanhadas
//  pelo encoder, com zero ensinado. O sinal SON e um fio so para os dois
//  drivers: soltar por causa da junta 1 solta a 2 junto, e uma junta que
//  cai sem ninguem medindo grava ponto torto sem avisar. Sem essa
//  garantia o modo continua valendo -- so que com torque, e o operador
//  posiciona pelas setas. Ele grava igual; o que muda e quem carrega o
//  braco.
//
//  O QUE ESTE MODO NAO FAZ: religar o torque na saida. Habilitar servo e
//  acao explicita do operador em todo o resto do sistema, e nao vai
//  deixar de ser justamente aqui, com a mao dele dentro da area do
//  braco.
// =====================================================================

struct ResumoAprender {
  bool    instalado;    // ha botao fisico compilado neste firmware
  bool    ativo;        // o modo esta ligado
  bool    bracoSolto;   // torque cortado, encoder acompanhando
  bool    apertado;     // o botao esta apertado agora (ja filtrado)
  uint8_t gravados;     // pontos gravados nesta sessao de aprendizado
  uint8_t recusados;    // e quantos foram recusados
  char    motivo[64];   // ultima recusa, em portugues
};

void aprenderIniciar();     // core 1, no setup
void aprenderAtualizar();   // core 1, no loop

// Entrar e sair tambem pela tela: o botao fisico e uma comodidade, nao
// um pre-requisito. Quem nao instalou o botao usa a pagina.
bool aprenderEntrar(const char** motivo);
void aprenderSair(const char* motivo);   // motivo = nullptr usa o padrao
bool aprenderAtivo();

// O toque curto. Devolve false quando o ponto foi recusado -- a razao
// fica em ResumoAprender::motivo e na mensagem da tela.
bool aprenderGravarPonto();

ResumoAprender aprenderResumo();

#ifdef ROBO2DOF_TESTE
void aprenderReiniciarTeste();
#endif
