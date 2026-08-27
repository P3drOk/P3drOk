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
//  O LACO DE LEITURA E SO LEITURA. Nada no ciclo normal escreve
//  registrador nunca. Um defeito que escrevesse num parametro do servo
//  estragaria a maquina de um jeito que nao se desfaz pela tela.
//
//  Ha UMA excecao, e ela e um caminho avulso, fora do ciclo: escrever um
//  parametro por acao explicita do operador (o bip do driver). Ver
//  encoderPedirEscrita() no fim deste arquivo, e as travas que a cercam.
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
  MOTIVO_FORMATO     // respondeu, mas nao no formato pedido
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
void encoderPedirTeste();                        // core 1 / web: so pede
void encoderRelatorio(char* destino, size_t tam);
bool encoderTesteRodando();

// Cacada do registrador da posicao. Duas etapas: marcar o estado da
// faixa toda, o operador mover o braco, e comparar. O que andou junto
// com o eixo e a posicao -- e o unico jeito honesto de achar isso num
// driver cujo mapa Modbus nao esta publicado. Sai no mesmo relatorio.
void encoderPedirCacada(bool comparar);

// =====================================================================
//  PARAMETRO DO DRIVER: achar e mudar
//
//  O mapa Modbus do T3D nao esta publicado -- e o que vale para a
//  posicao vale para qualquer parametro do painel (P098 e companhia).
//
//  ACHAR sem escrever nada. Mesma ideia da cacada da posicao, virada do
//  avesso: em vez de mover o braco e ver que registrador acompanha, voce
//  MUDA O PARAMETRO NO PAINEL do driver e ve qual registrador mudou.
//  Duas fotos da faixa inteira e uma comparacao. Zero escrita, zero
//  risco -- e e assim que se descobre o endereco de um parametro sem
//  manual.
//
//    1. encoderPedirDiferenca(false)   tira a primeira foto
//    2. o operador muda o parametro no painel do driver
//    3. encoderPedirDiferenca(true)    tira a segunda e lista o que mudou
// =====================================================================
void encoderPedirDiferenca(bool comparar);

// =====================================================================
//  ESCREVER um parametro. A EXCECAO da regra de so-leitura.
//
//  O LACO DE LEITURA CONTINUA SO-LEITURA. Nada no ciclo normal escreve
//  nunca. Esta funcao e um caminho avulso, disparado por acao explicita
//  do operador, e existe porque ha parametros que so se muda no painel
//  do driver -- e o painel do driver fica atras da maquina.
//
//  POR QUE ISTO E MAIS PERIGOSO QUE LER. Ler no registrador errado da um
//  numero errado na tela. Escrever no registrador errado num servo drive
//  pode trocar a engrenagem eletronica, o modo de controle, o sentido do
//  eixo ou o limite de torque -- numa maquina com tocha de solda na
//  ponta. Alem disso, parametro costuma ir para EEPROM, que tem numero
//  limitado de ciclos de escrita.
//
//  Por isso:
//   - so com os SERVOS DESLIGADOS (varios drivers recusam escrever com o
//     eixo habilitado, e um parametro trocado com torque ligado muda o
//     comportamento na hora);
//   - so com o braco parado, no modo manual, com a solda desligada;
//   - o registrador e o valor sao digitados, nunca deduzidos;
//   - depois de escrever, o firmware RELE o registrador e confere. Uma
//     escrita que o driver ignorou em silencio e pior que uma recusada.
//
//  A funcao 6 escreve um registrador; a 16 escreve um bloco. Ha driver
//  que so aceita a 16 mesmo para um registrador so -- por isso as duas.
// =====================================================================
struct EscritaParam {
  bool     pedida;      // ha uma escrita em andamento ou concluida
  bool     concluida;
  bool     ok;
  uint16_t reg;
  uint16_t valor;       // o que se pediu
  uint16_t lido;        // o que voltou na releitura
  char     motivo[64];
};
void         encoderPedirEscrita(uint16_t reg, uint16_t valor, bool usarFuncao16);
EscritaParam encoderEscritaResumo();

#ifdef ROBO2DOF_TESTE
// O banco bombeia a tarefa a mao, como faz com a do cartao.
void encoderCicloTeste();
void encoderReiniciarTeste();
#endif
