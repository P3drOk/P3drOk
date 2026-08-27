#!/usr/bin/env bash
# Roda o banco inteiro sob AddressSanitizer e UndefinedBehaviorSanitizer.
#
# O banco normal responde "o firmware fez o esperado?". Este responde uma
# pergunta diferente e complementar: "ele fez isso lendo so a memoria que
# e dele, e sem depender de comportamento indefinido?".
#
# Num ESP32 nada disso da erro. Ler um vetor uma posicao alem devolve o
# byte que estiver la e a maquina segue, com um numero errado que aparece
# meia hora depois em outro lugar. Aqui o mesmo acesso para o programa e
# aponta a linha.
#
# Nao entra no compilar.sh porque custa uns 3x o tempo. Rode antes de
# fechar uma rodada de mudancas -- especialmente as que mexem em buffer,
# indice ou conversao de tipo.
set -e
cd "$(dirname "$0")/.."
mkdir -p testes/saida

FLAGS="-std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer"
FLAGS="$FLAGS -fno-sanitize-recover=undefined"
FLAGS="$FLAGS -I testes/mocks -I Robo2dof -DROBO2DOF_TESTE"
FLAGS="$FLAGS -DESTOP_FISICO_INSTALADO=true -DAPRENDER_BOTAO_INSTALADO=true"
FLAGS="$FLAGS -Wall -Wextra -Wno-unused-parameter"

g++ $FLAGS -o testes/saida/banco_asan \
    testes/banco.cpp testes/ino_wrapper.cpp testes/mocks/mocks.cpp \
    Robo2dof/estado.cpp Robo2dof/cinematica.cpp Robo2dof/motores.cpp \
    Robo2dof/solda.cpp Robo2dof/trajetoria.cpp Robo2dof/programa.cpp \
    Robo2dof/calibracao.cpp Robo2dof/armazenamento.cpp \
    Robo2dof/servidor_web.cpp Robo2dof/rede.cpp Robo2dof/encoder.cpp \
    Robo2dof/correcao.cpp Robo2dof/aprender.cpp Robo2dof/ota.cpp

# detect_leaks=0: o banco nao libera os mocks de proposito no fim do
# processo, e vazamento no encerramento nao diz nada sobre o firmware.
ASAN_OPTIONS=detect_leaks=0 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
  exec testes/saida/banco_asan
