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
//  ESCREVE UM REGISTRADOR SO: o habilita (SON), em configSon.reg.
//
//  Ate a versao anterior este modulo nao escrevia nada, e o habilita era
//  um fio no GPIO 23. O fio saiu -- nesta maquina o P098 do painel
//  governa o torque e o terminal externo nao tinha efeito. A escrita
//  entrou por uma porta estreita e continua estreita: um registrador,
//  o configurado, e nenhum outro. Um defeito que escrevesse em parametro
//  qualquer do servo estragaria a maquina de um jeito que nao se desfaz
//  pela tela.
//
//  Toda escrita e conferida RELENDO. Driver que responde "aceitei" e
//  guarda outra coisa existe, e dizer "desabilitado" com o eixo
//  energizado e a unica mentira que este arquivo nao pode contar.
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
  MOTIVO_SALTO       // respondeu numero possivel, mas longe demais do anterior
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
  uint32_t saltos;        // leituras recusadas por pular longe demais
  uint8_t  motivo;        // MotivoEncoder: por que a ultima tentativa falhou

  // ---- derivados, calculados AQUI e nao no navegador ----------------
  //
  // A tarefa le a 20 Hz; o painel so consulta a 4 Hz. Calcular
  // velocidade no navegador seria medir com uma regua cinco vezes mais
  // grossa que a disponivel, e perder toda variacao entre consultas.
  // Quem tem os instantes de verdade e quem le.
  int32_t  delta;         // variacao desde a leitura boa anterior
  float    velocidade;    // contagens do MOTOR por segundo
  float    rpm;           // voltas do MOTOR por minuto
  int8_t   sentido;       // +1 = cresce, -1 = decresce, 0 = parado
  uint32_t passosTotais;  // soma de |delta|: quanto o eixo andou, no total
  uint32_t inversoes;     // quantas vezes trocou de sentido
  int32_t  brutoMin;      // menor e maior contagem vistas desde o zerar
  int32_t  brutoMax;
  float    velMax;        // pico de velocidade, nos dois sentidos
  float    velMin;
};

void encoderIniciar();                 // cria a tarefa no core 0
LeituraEncoder encoderLer(uint8_t junta);   // 1 ou 2

// Marca a contagem atual como o zero daquela junta. Chamado quando o
// operador referencia a maquina: dali em diante o erro e medido a partir
// deste ponto.
void encoderZerar(uint8_t junta);

// Ensina que o braco esta AGORA em 'graus' naquela junta. Grava a
// referencia absoluta: a contagem crua que corresponde a esse angulo.
//
// E a unica calibracao que sobra com encoder absoluto. Feita uma vez, a
// maquina se localiza sozinha em todo boot -- sem fim de curso, sem
// procurar batente, e mesmo que alguem empurre o braco a mao com tudo
// desligado.
bool encoderDefinirZero(uint8_t junta, float graus);

// Referencia absoluta gravada, para o NVS e para a tela.
int32_t encoderReferencia(uint8_t junta);
void    encoderCarregarReferencia(uint8_t junta, int32_t bruto);      // 0 = as duas

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
// ---------------------------------------------------------------------
// HABILITA (SON) pelo barramento.
//
// O core 1 so PEDE; quem fala no fio e a tarefa do core 0. O resultado
// se consulta depois, por encoderSonEstado() -- e quem pediu para
// DESABILITAR tem obrigacao de olhar: escrita que nao confirmou com o
// eixo energizado e falha, nao detalhe.
// ---------------------------------------------------------------------
enum EstadoSon : uint8_t {
  SON_OCIOSO = 0,   // nada pedido desde o boot
  SON_PENDENTE,     // o core 0 ainda nao atendeu
  SON_OK,           // escrito nos dois drivers e conferido relendo
  SON_FALHOU        // nao confirmou, ou o prazo estourou
};

// 'junta' e 1, 2, ou 0 para as duas. Habilitar por junta existe porque
// cada driver e um escravo Modbus proprio: exigir que os dois confirmem
// impede de trabalhar numa bancada com um driver ligado.
void    encoderPedirSon(bool ligar, uint8_t junta);

// Pede e ESPERA a confirmacao, ate 'prazoMs'. Devolve true se confirmou.
//
// Existe para os caminhos que nao podem seguir com o eixo possivelmente
// energizado -- o OTA e o caso: ele reinicia o ESP32 no fim da gravacao,
// e um pedido apenas enfileirado pode nunca chegar ao fio. Enquanto o
// habilita era um pino isso nao existia: digitalWrite cortava na hora e
// o nivel sobrevivia ao reset.
//
// Nao use no caminho normal do operador. Esperar aqui prende o core 1.
// Espera o pedido que JA foi feito -- chame servosHabilitar() antes, para
// que a parada suave e o corte do arco acontecam junto.
bool    encoderSonEsperar(uint32_t prazoMs);
uint8_t encoderSonEstado();
// Qual junta o pedido atendia, e se ela confirmou. 0 = as duas.
uint8_t encoderSonJunta();
bool    encoderSonJuntaOk(uint8_t junta);
void    encoderSonMotivo(char* destino, size_t tam);

void encoderPedirTeste();                        // core 1 / web: so pede
void encoderRelatorio(char* destino, size_t tam);
bool encoderTesteRodando();

// Cacada do registrador da posicao. Duas etapas: marcar o estado da
// faixa toda, o operador mover o braco, e comparar. O que andou junto
// com o eixo e a posicao -- e o unico jeito honesto de achar isso num
// driver cujo mapa Modbus nao esta publicado. Sai no mesmo relatorio.
void encoderPedirCacada(bool comparar);

#ifdef ROBO2DOF_TESTE
// O banco bombeia a tarefa a mao, como faz com a do cartao.
void encoderCicloTeste();
void encoderReiniciarTeste();
#endif
