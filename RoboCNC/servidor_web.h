#pragma once
#include <WiFi.h>
#include <WebServer.h>

// =====================================================================
//  Servidor web. Roda no core 0.
//
//  Nenhum handler deste arquivo pode chamar motores.h, solda.h ou
//  trajetoria.h. Eles apenas:
//    1. enfileiram um Comando,
//    2. leem o Snapshot publicado pelo loop,
//    3. alteram parametros de configuracao escalares.
// =====================================================================

void servidorIniciar();
void servidorAtender();
