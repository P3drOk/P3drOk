#!/usr/bin/env bash
# Sobe o servidor falso, roda a interface num Chromium de verdade e derruba.
# O servidor le pagina_web.h na partida, entao ele PRECISA subir junto do
# teste: um servidor deixado de pe serve a pagina antiga em memoria.
set -e
cd "$(dirname "$0")/../.."
mkdir -p testes/saida/ui

# A pagina servida e a comprimida; conferir antes evita testar uma versao
# que o robo nao entrega.
python3 testes/gerar_pagina_gz.py --conferir
PORTA=${PORTA:-8099}

python3 testes/interface/servidor_falso.py RoboCNC/pagina_web.h "$PORTA" >/tmp/robocnc-ui.log 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null || true' EXIT

for i in $(seq 1 40); do
  if curl -s -o /dev/null "http://127.0.0.1:$PORTA/"; then break; fi
  sleep 0.1
done

node testes/interface/testar_ui.js
