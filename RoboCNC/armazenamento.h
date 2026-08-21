#pragma once
#include "config.h"
#include "programa.h"

// =====================================================================
//  Cartao microSD.
//
//  REGRA DESTE MODULO, no mesmo espirito da regra de ouro do projeto:
//  o core 1 (loop) NUNCA toca no SPI do cartao. Escrever num cartao SD
//  leva de dezenas a centenas de milissegundos - se isso acontecesse
//  dentro do laco de 1 ms, a supervisao de seguranca pararia junto.
//
//  Quem faz I/O e uma tarefa propria, no core 0. A conversa entre ela e
//  o core 1 acontece por:
//
//    1. armSolicitar()  - pedido assincrono, uma tarefa por vez;
//    2. areas de troca  - staging de programa, area de config pendente,
//                         ou o proprio buffer de trajetoria emprestado;
//    3. Comando na fila - a tarefa avisa o core 1 quando ha o que aplicar.
//
//  Sem cartao TUDO continua funcionando: o NVS segue sendo a
//  configuracao de boot e o cartao e backup, biblioteca de programas e
//  registro de eventos.
//
//  Organizacao do cartao:
//    /prog/<nome>.prg    programas de solda (texto, editavel no PC)
//    /traj/<nome>.trj    trajetorias gravadas (binario, compacto)
//    /cfg/<nome>.cfg     backups de configuracao (texto)
//    /log/s####.csv      registro de eventos, um arquivo por partida
// =====================================================================

enum ArmEstado : uint8_t {
  ARM_DESLIGADO,     // compilado sem cartao (CARTAO_INSTALADO=false)
  ARM_SEM_CARTAO,    // nenhum cartao montado
  ARM_PRONTO,
  ARM_OCUPADO,
  ARM_ERRO
};

enum ArmTarefa : uint8_t {
  TAR_NENHUMA,
  TAR_MONTAR,
  TAR_LISTAR,          // nome = "prog" | "traj" | "cfg"
  TAR_APAGAR,          // nome = "<tipo>/<arquivo>"
  TAR_SALVAR_PROG,
  TAR_CARREGAR_PROG,
  TAR_SALVAR_TRAJ,
  TAR_CARREGAR_TRAJ,
  TAR_SALVAR_CONFIG,
  TAR_CARREGAR_CONFIG
};

enum ArmTipo : uint8_t { TIPO_PROG, TIPO_TRAJ, TIPO_CFG, TIPO_INVALIDO };

struct ArmEntrada {
  char     nome[MAX_NOME_ARQ + 1];
  uint32_t bytes;
};

// ---------------------------------------------------------------------
// Ciclo de vida
// ---------------------------------------------------------------------
void armIniciar();     // cria a tarefa no core 0 (chamar no setup)

// Pedido assincrono. Falso se ja houver tarefa em andamento, se o nome
// for invalido ou se nao houver cartao.
bool armSolicitar(ArmTarefa t, const char* nome);

// ---------------------------------------------------------------------
// Estado publicado (leitura barata, sem I/O)
// ---------------------------------------------------------------------
ArmEstado   armEstado();
bool        armOcupado();
const char* armMensagem();      // resultado da ultima tarefa
uint32_t    armSequencia();     // muda a cada tarefa concluida
uint64_t    armBytesTotais();
uint64_t    armBytesLivres();

uint8_t           armListaN();
const ArmEntrada* armLista();
ArmTipo           armListaTipo();

// Converte "prog"/"traj"/"cfg" e valida nome de arquivo.
ArmTipo armTipoDe(const char* texto);
// Aceita apenas [A-Za-z0-9 _-], 1..MAX_NOME_ARQ. Barra travessia de
// diretorio: um "../" num nome de arquivo vindo de HTTP le o cartao
// inteiro.
bool    armNomeValido(const char* nome);

// ---------------------------------------------------------------------
// Area de troca do programa.
//
// Carregar: a tarefa SD preenche o staging, valida a sintaxe e so entao
// posta CMD_ARQ_APLICAR_PROG. O programa vivo so e tocado pelo core 1,
// e so se cada ponto passar em posturaValida(). Arquivo corrompido nao
// derruba o programa que estava na maquina.
// ---------------------------------------------------------------------
uint8_t      armStagingN();
const Ponto* armStagingPontos();
void         armStagingDefinir(const Ponto* pontos, uint8_t n);  // core 1

// Geometria com que o arquivo foi gravado, para avisar quando os elos
// mudaram (o mesmo par de angulos aponta para outro lugar da chapa).
float armStagingElo1();
float armStagingElo2();

// ---------------------------------------------------------------------
// Registro de eventos.
//
// Seguro de chamar do core 1: so enfileira a linha, o I/O e da tarefa.
// Descarta em silencio se a fila estiver cheia - log nunca pode
// atrasar o laco de controle.
// ---------------------------------------------------------------------
void logEvento(const char* fmt, ...);

#ifdef ROBOCNC_TESTE
// Executa um ciclo da tarefa de cartao no lugar do laco proprio. Existe
// so para o banco de testes, que roda tudo numa thread so.
void armCicloTeste();
void armReiniciarTeste();
#endif
