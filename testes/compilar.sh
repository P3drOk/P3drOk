#!/usr/bin/env bash
# Compila e roda o banco de testes do firmware no PC.
set -e
cd "$(dirname "$0")/.."
mkdir -p testes/saida
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
    -I testes/mocks -I RoboCNC \
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
    RoboCNC/calibracao.cpp
exec testes/saida/banco
