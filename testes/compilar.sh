#!/usr/bin/env bash
# Compila e roda o banco de testes do firmware no PC.
set -e
cd "$(dirname "$0")/.."
mkdir -p testes/saida

FLAGS="-std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter -I testes/mocks -I Robo2dof -DROBO2DOF_TESTE"

# Documento de fiacao que mente e pior que documento nenhum.
python3 testes/conferir_ligacoes.py

# Botao que chama rota inexistente e 404 silencioso: nada acontece e nada
# aparece. Confere as duas pontas antes de compilar.
python3 testes/conferir_rotas.py

# A pagina servida e a comprimida: se ela ficar velha, o robo entrega uma
# interface diferente da que esta no repositorio.
python3 testes/gerar_pagina_gz.py --conferir

# O banco compila com o botao de emergencia "instalado" para exercitar
# esse ramo. O config.h de producao mantem ESTOP_FISICO_INSTALADO=false
# ate o botao existir de verdade.
g++ $FLAGS -DESTOP_FISICO_INSTALADO=true \
    -o testes/saida/banco \
    testes/banco.cpp \
    testes/ino_wrapper.cpp \
    testes/mocks/mocks.cpp \
    Robo2dof/estado.cpp \
    Robo2dof/cinematica.cpp \
    Robo2dof/motores.cpp \
    Robo2dof/solda.cpp \
    Robo2dof/trajetoria.cpp \
    Robo2dof/programa.cpp \
    Robo2dof/calibracao.cpp \
    Robo2dof/armazenamento.cpp \
    Robo2dof/servidor_web.cpp \
    Robo2dof/rede.cpp \
    Robo2dof/encoder.cpp \
    Robo2dof/correcao.cpp
exec testes/saida/banco
