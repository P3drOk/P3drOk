#!/usr/bin/env python3
"""Confere se LIGACOES.md concorda com RoboCNC/config.h.

Documento de fiacao que mente e pior que documento nenhum: o operador
liga o fio no pino errado. Isto roda junto com o banco de testes.
"""
import re, sys, pathlib

raiz = pathlib.Path(__file__).resolve().parent.parent
cfg = (raiz / "RoboCNC" / "config.h").read_text(encoding="utf-8")
doc = (raiz / "LIGACOES.md").read_text(encoding="utf-8")

pinos = dict(re.findall(r"^#define\s+(PIN_[A-Z0-9_]+)\s+(\d+)", cfg, re.M))

# nome no config.h -> como a linha aparece na tabela de LIGACOES.md
ESPERADO = {
    "PIN_J1_PULSO":  "Driver J1 · PUL+",
    "PIN_J1_DIR":    "Driver J1 · DIR+",
    "PIN_J2_PULSO":  "Driver J2 · PUL+",
    "PIN_J2_DIR":    "Driver J2 · DIR+",
    "PIN_SERVO_ON":  "SON dos dois drivers",
    "PIN_ALARME_J1": "Driver J1 · ALM",
    "PIN_ALARME_J2": "Driver J2 · ALM",
    "PIN_ESTOP":     "Botão de emergência",
    "PIN_SD_CS":     "microSD · CS",
    "PIN_SD_SCK":    "microSD · SCK",
    "PIN_SD_MOSI":   "microSD · MOSI",
    "PIN_SD_MISO":   "microSD · MISO",
}

falhas = []
for nome, texto in ESPERADO.items():
    if nome not in pinos:
        falhas.append("%s nao existe mais em config.h" % nome)
        continue
    gpio = pinos[nome]
    linha = [l for l in doc.splitlines() if texto in l]
    if not linha:
        falhas.append("%s (GPIO %s) nao aparece na tabela de LIGACOES.md" % (nome, gpio))
        continue
    if not re.search(r"\|\s*\**%s\**\s*(?:→[^|]*)?\|" % gpio, linha[0]):
        falhas.append("%s: config.h diz GPIO %s, LIGACOES.md diz \"%s\""
                      % (nome, gpio, linha[0].split("|")[1].strip()))

if falhas:
    print("\033[31mLIGACOES.md fora de sincronia com config.h\033[0m")
    for f in falhas:
        print("  - " + f)
    sys.exit(1)
print("  \033[32mPASSA\033[0m    LIGACOES.md confere com os pinos de config.h "
      "(%d pinos)" % len(ESPERADO))
