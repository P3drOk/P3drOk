#pragma once
#include "estado.h"

// =====================================================================
//  Leitura do encoder pelos drivers, por Modbus RTU sobre RS485.
//
//  Roda numa tarefa propria no core 0. Nao toca em motor, rele nem
//  estado de movimento -- so publica o que leu.
//
//  POR QUE ISTO NAO FECHA MALHA
//
//  Cada leitura Modbus custa de 5 a 20 ms e o intervalo tem jitter. Da
//  para mostrar posicao, comparar com o comandado e acusar perda de
//  passo em segundos. NAO da para cortar o arco no instante em que o
//  motor escorrega -- para isso o caminho e a saida PA/PB do driver num
//  contador PCNT. Ver ROADMAP.md secao 1.4.
//
//  SO LEITURA. Nenhuma funcao aqui escreve registrador. Um defeito que
//  escrevesse num parametro do servo estragaria a maquina de um jeito
//  que nao se desfaz pela tela.
//
//  O ENDERECO DO REGISTRADOR E CONFIGURAVEL
//
//  O mapa Modbus do T3D nao esta publicado e muda por versao de
//  firmware. Numero fixo aqui seria adivinhacao. O operador acha o
//  endereco com ferramentas/teste_rs485 (ou mexendo no campo com o
//  grafico na tela) e o sistema guarda.
// =====================================================================

// Por que nao esta lendo. Tela que so diz "nada" nao ensina ninguem.
enum MotivoEncoder : uint8_t {
  MOTIVO_OK = 0,
  MOTIVO_NUNCA,      // ainda nao perguntou
  MOTIVO_SILENCIO,   // ninguem respondeu naquele endereco
  MOTIVO_CRC,        // veio byte, mas corrompido ou de outro escravo
  MOTIVO_EXCECAO,    // o driver respondeu "esse registrador nao existe"
  MOTIVO_FORMATO,    // respondeu, mas nao no formato pedido
  // Lendo um registrador de cada vez, a palavra alta mudou entre as duas
  // perguntas. Nao e defeito: e a contagem virando no meio da leitura, e
  // a proxima volta do ciclo pega o par inteiro.
  MOTIVO_VIRADA
};

struct LeituraEncoder {
  bool     valido;        // houve leitura recente e coerente
  int32_t  bruto;         // contagem crua do registrador
  int32_t  referencia;    // contagem no momento do "zerar"
  float    graus;         // angulo da JUNTA medido pelo encoder
  float    erro;          // comandado - medido, em graus da junta
  uint32_t idadeMs;       // ha quanto tempo foi a ultima leitura boa
  uint32_t leituras;      // contador de sucessos
  uint32_t falhas;        // contador de silencios e CRC ruim
  uint8_t  motivo;        // MotivoEncoder: por que a ultima tentativa falhou
};

void encoderIniciar();                 // cria a tarefa no core 0
LeituraEncoder encoderLer(uint8_t junta);   // 1 ou 2

// Marca a contagem atual como o zero daquela junta. Chamado quando o
// operador referencia a maquina: dali em diante o erro e medido a partir
// deste ponto.
void encoderZerar(uint8_t junta);      // 0 = as duas

// Aplica a configuracao que esta em configEncoder (chamado pelo core 1
// ao processar CMD_APLICAR_ENCODER).
void encoderReconfigurar();

// Ultimo quadro Modbus que passou no fio, em hexadecimal, pronto para a
// tela. "Sem resposta" sem os bytes crus e uma palavra sem prova.
void encoderUltimoQuadro(char* destino, size_t tam);

// ---------------------------------------------------------------------
// Autoteste DENTRO do sistema rodando.
//
// O programa de bancada (ferramentas/teste_rs485) prova a fiacao com o
// ESP32 sozinho na placa. Isso nao responde a pergunta que importa
// quando o sistema nao le: sera que aqui dentro, com Wi-Fi, servidor
// web, cartao e as interrupcoes dos motores no mesmo nucleo, a linha
// ainda funciona? So medindo aqui dentro.
//
// Faz, em sequencia:
//   1. ECO -- deixa o receptor ligado enquanto transmite e ve se os
//      proprios bytes voltam. Se voltarem, ESP32 <-> MAX485 esta bom
//      DENTRO do sistema, e o que sobra e o barramento ou o tempo.
//   2. SONDAGEM -- pergunta pelo registrador 0 nas funcoes 3 e 4, que e
//      como o programa de bancada acha o driver. Ate a EXCECAO serve:
//      ela prova que o driver esta ai e respondeu.
//   3. A pergunta de verdade, com o registrador configurado.
//
// Sai um relatorio de texto, com os bytes crus de cada passo.
// ---------------------------------------------------------------------
void encoderPedirTeste();                        // core 1 / web: so pede
void encoderRelatorio(char* destino, size_t tam);
bool encoderTesteRodando();

// Cacada do registrador da posicao. Duas etapas: marcar o estado da
// faixa toda, o operador mover o braco, e comparar. O que andou junto
// com o eixo e a posicao -- e o unico jeito honesto de achar isso num
// driver cujo mapa Modbus nao esta publicado. Sai no mesmo relatorio.
void encoderPedirCacada(bool comparar);

#ifdef ROBOCNC_TESTE
// O banco bombeia a tarefa a mao, como faz com a do cartao.
void encoderCicloTeste();
void encoderReiniciarTeste();
#endif
