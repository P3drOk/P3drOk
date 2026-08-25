#pragma once
#include "estado.h"

// =====================================================================
//  CORRECAO DE POSICAO PELO ENCODER
//
//  O que isto e, e o que NAO e.
//
//  NAO e malha fechada de servo. Cada leitura Modbus custa de 5 a 20 ms
//  com jitter, o que da umas 20 amostras por segundo. Com isso NAO da
//  para corrigir o eixo enquanto ele anda -- a correcao chegaria tarde
//  demais e faria o braco oscilar.
//
//  E o ASSENTAMENTO no fim do movimento: o braco chega, para, o sistema
//  le onde ele REALMENTE parou, e da um retoque curto. E exatamente o
//  que resolve o incomodo do operador -- "saio de uma posicao e volto, e
//  ela nao e mais a mesma".
//
//  Sem isto o erro de um movimento entra no proximo, e no proximo, e o
//  desvio cresce sem nunca voltar. Com isto, cada parada zera a conta.
//
//  REGRAS DE SEGURANCA, todas obrigatorias antes de mexer no motor:
//
//   1. So com o eixo PARADO. Retoque em cima de eixo andando e briga
//      com a rampa de movimento.
//   2. So com leitura VALIDA e RECENTE. Corrigir por dado velho e mover
//      o braco baseado em onde ele estava, nao onde esta.
//   3. So dentro do curso calibrado. O retoque nunca empurra o eixo
//      para fora do limite -- ele e um ajuste fino, nao uma excecao as
//      protecoes.
//   4. NUNCA com a solda ligada. Um retoque no meio do cordao estraga o
//      cordao, e o operador nao pediu por ele ali.
//   5. Erro GRANDE nao se corrige, se DENUNCIA. Acima de
//      maxCorrecaoGraus o problema nao e folga: e acoplamento solto,
//      registrador errado ou reducao errada. Empurrar o braco varios
//      graus achando que esta consertando e a maneira mais rapida de
//      bater a ferramenta em alguma coisa.
//   6. Numero de tentativas limitado. Se tres retoques nao resolveram,
//      o problema nao e o que este modulo conserta.
// =====================================================================

// Em que pe esta o assentamento. Vai para a tela.
enum EstadoCorrecao : uint8_t {
  CORR_PARADA = 0,   // nao ha nada em curso
  CORR_ESPERANDO,    // esperando leitura fresca depois da parada
  CORR_RETOCANDO,    // movendo o retoque
  CORR_PRONTA,       // chegou dentro da tolerancia
  CORR_DESISTIU,     // tentou o bastante e nao fechou
  CORR_RECUSADA      // erro grande demais, ou sem leitura: nao mexeu
};

struct ResumoCorrecao {
  uint8_t estado;        // EstadoCorrecao
  uint8_t tentativas;    // quantos retoques ja foram dados
  float   erroInicial1;  // erro medido ao chegar, em graus da junta
  float   erroInicial2;
  float   erroFinal1;    // erro depois do assentamento
  float   erroFinal2;
  uint32_t totalOk;      // quantos assentamentos fecharam, desde o boot
  uint32_t totalDesistiu;
  char    motivo[48];    // por que recusou ou desistiu, em portugues
};

// Chamada quando um movimento COMECA. Sem isto o resultado do
// assentamento anterior fica de pe, o "ja terminei" do movimento passado
// vale para o novo, e a correcao roda uma vez so na vida da maquina.
void correcaoNovoMovimento();

void correcaoIniciar();                  // core 1: chegou, comeca a assentar
void correcaoAtualizar();                // core 1: chamada do loop
bool correcaoEmCurso();
void correcaoCancelar();                 // parada de emergencia, troca de modo
ResumoCorrecao correcaoResumo();

// Vigilancia: compara comandado com medido o tempo todo e avisa quando a
// diferenca passa do limite por tempo demais. Nao mexe no motor -- so
// conta e avisa. E o que transforma "o cordao saiu torto" em "a junta 1
// perdeu 2 graus as 14h32".
void correcaoVigiar();
uint32_t correcaoAlertas();

#ifdef ROBO2DOF_TESTE
void correcaoReiniciarTeste();
#endif
