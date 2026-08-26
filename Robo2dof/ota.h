#pragma once
#include "estado.h"

// =====================================================================
//  ATUALIZACAO DE FIRMWARE PELA REDE (OTA local)
//
//  A maquina fica na fabrica, o cabo USB fica na bancada. Sem isto,
//  corrigir uma linha de firmware significa levar notebook ate o robo,
//  achar a porta, e torcer para a IDE estar instalada naquela maquina.
//  Com isto: abre o painel, escolhe o .bin, sobe.
//
//  LIMITE REAL, E ELE IMPORTA. O `partitions.csv` deste projeto da 3 MB
//  de app e NAO tem particao de OTA -- gravado assim, o robo nao tem
//  para onde escrever o firmware novo, e este modulo recusa dizendo
//  isso. Para ter OTA, grave uma vez pelo USB com o
//  `partitions_ota.csv` (duas particoes de 1,9 MB); dali em diante as
//  atualizacoes vao pela rede.
//
//  Nao ha meio-termo possivel: OTA exige duas particoes de app, porque o
//  firmware novo e escrito na que nao esta rodando. Uma particao so, e
//  escrever por cima do proprio codigo em execucao trava o processador
//  no meio da gravacao -- com o rele de solda em estado indefinido.
//
//  REGRAS:
//   1. So no modo manual, com o braco parado e a solda desligada.
//   2. Os servos sao desabilitados antes de comecar: o ESP32 reinicia no
//      fim, e um driver habilitado com o gerador de pulso morto e um
//      eixo que ninguem esta comandando.
//   3. Arquivo invalido nao chega a ser gravado: o Update do ESP32
//      confere o cabecalho da imagem antes de apagar a particao.
// =====================================================================

enum EstadoOta : uint8_t {
  OTA_PARADA = 0,
  OTA_RECEBENDO,
  OTA_OK,        // gravada; o robo reinicia em seguida
  OTA_ERRO
};

struct ResumoOta {
  bool     disponivel;   // ha particao de OTA neste firmware
  uint8_t  estado;       // EstadoOta
  uint32_t recebido;     // bytes ja gravados
  uint32_t espaco;       // tamanho da particao de destino
  char     motivo[64];
};

bool       otaDisponivel();
ResumoOta  otaResumo();

// Chamadas pelo servidor web (core 0), durante o upload.
bool otaComecar(const char** motivo);
bool otaPedaco(const uint8_t* dados, size_t n);
bool otaTerminar(const char** motivo);
void otaCancelar(const char* motivo);

// O reinicio nao acontece dentro do handler HTTP: a resposta precisa
// chegar ao navegador antes. O core 1 reinicia no ciclo seguinte.
bool otaPrecisaReiniciar();
void otaReiniciarAgora();
