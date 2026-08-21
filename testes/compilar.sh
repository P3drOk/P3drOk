#!/usr/bin/env bash
# Compila e roda o banco de testes do firmware no PC.
set -e
cd "$(dirname "$0")/.."
mkdir -p testes/saida

FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -I testes/mocks -I RoboCNC -DROBOCNC_TESTE"

# servidor_web.cpp roda no core 0 e depende de rede, entao fica fora do
# banco -- mas passa por conferencia de compilacao para nao apodrecer.
g++ $FLAGS -DESTOP_FISICO_INSTALADO=true -fsyntax-only RoboCNC/servidor_web.cpp

# O banco compila com o botao de emergencia "instalado" para exercitar
# esse ramo. O config.h de producao mantem ESTOP_FISICO_INSTALADO=false
# ate o botao existir de verdade.
g++ $FLAGS -DESTOP_FISICO_INSTALADO=true \
    -o testes/saida/banco \
    testes/banco.cpp \
    testes/ino_wrapper.cpp \
    testes/mocks/mocks.cpp \
    RoboCNC/estado.cpp \
    RoboCNC/cinematica.cpp \
    RoboCNC/motores.cpp \
    RoboCNC/solda.cpp \
    RoboCNC/trajetoria.cpp \
    RoboCNC/programa.cpp \
    RoboCNC/calibracao.cpp \
    RoboCNC/armazenamento.cpp
exec testes/saida/banco
