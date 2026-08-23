#pragma once
#include "estado.h"

// =====================================================================
//  Rede. Roda no core 0, junto do servidor web.
//
//  O ponto de acesso proprio fica SEMPRE ligado, mesmo com a maquina
//  dentro da rede da oficina. Nao e desperdicio, e a saida de
//  emergencia: senha errada, roteador desligado, sinal ruim no fundo do
//  galpao -- em qualquer um desses casos o painel continua alcancavel em
//  192.168.4.1 pelo Wi-Fi da propria maquina. Um equipamento que se move
//  nao pode ficar inacessivel porque o roteador caiu.
//
//  Nada aqui toca motor, rele ou estado de movimento. Trocar de rede
//  passa pela fila de comandos como qualquer outro ajuste: o core 0
//  prepara, o core 1 valida, grava no NVS e pede a reconexao.
// =====================================================================

enum EstadoEstacao : uint8_t {
  EST_DESLIGADA,     // nenhuma rede configurada
  EST_CONECTANDO,
  EST_CONECTADA,
  EST_SEM_REDE,      // o SSID configurado nao aparece
  EST_SENHA,         // rede encontrada, autenticacao recusada
  EST_FALHOU         // desistiu; tenta de novo depois
};

void redeIniciar();     // chamar no setup, antes de servidorIniciar()
void redeAtender();     // chamar no laco da tarefa de rede (core 0)

EstadoEstacao redeEstado();
const char*   redeEstadoTexto();
const char*   redeIpEstacao();     // "" quando nao conectada
const char*   redeIpAcesso();      // IP fixo do ponto de acesso proprio
const char*   redeNomeLocal();     // nome mDNS, sem o ".local"
int32_t       redeSinal();         // RSSI da estacao, 0 quando desconectada
uint32_t      redeSequencia();     // muda a cada mudanca de estado

// ---------------------------------------------------------------------
// Varredura.
//
// ASSINCRONA, sempre. A varredura sincrona do ESP32 bloqueia por varios
// segundos; feita dentro da tarefa web, ela deixaria de responder o
// heartbeat do operador e o supervisor cortaria o movimento por
// "conexao perdida" -- um comando de tela derrubando o braco.
// ---------------------------------------------------------------------
struct RedeVizinha {
  char    ssid[33];
  int32_t rssi;
  uint8_t canal;
  bool    aberta;
};

bool               redeVarrerIniciar();
bool               redeVarrendo();
uint8_t            redeVizinhasN();
const RedeVizinha* redeVizinhas();

// Pedido de reconexao, escrito pelo core 1 ao aplicar a configuracao.
extern volatile bool redePedidoReconectar;
