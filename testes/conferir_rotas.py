#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Confere as rotas HTTP dos dois lados.

O defeito mais caro que este projeto ja teve nao foi de motor: foi botao
que nao fazia nada. A pagina chamava uma rota com nome diferente do que o
firmware registrava, e o resultado era um 404 silencioso -- o operador
apertava e nada acontecia, sem nenhuma mensagem.

Aqui a lista das duas pontas e comparada:
  - o que pagina_web.h chama em fetch()/post()/ping();
  - o que servidor_web.cpp registra em server.on().

Rota chamada e nao registrada = botao mudo, e erro.
Rota registrada e nunca chamada = so um aviso: pode ser ponta solta ou
uso legitimo por outro cliente.
"""
import re
import sys
import pathlib

RAIZ = pathlib.Path(__file__).resolve().parent.parent
PAGINA = RAIZ / "RoboCNC" / "pagina_web.h"
SERVIDOR = RAIZ / "RoboCNC" / "servidor_web.cpp"

# Rotas que o navegador pede sozinho, sem aparecer em nenhum fetch().
IMPLICITAS = {"/", "/manifest.webmanifest", "/icone.svg"}


def rotas_registradas(texto):
    return {m.group(1) for m in re.finditer(r'server\.on\(\s*"([^"]+)"', texto)}


def rotas_chamadas(texto):
    achadas = set()
    # "/api/x?a="+v   |   "/api/x"   |   '/api/x'
    for m in re.finditer(r'["\'](/(?:api|manifest|icone)[^"\'?+]*)', texto):
        achadas.add(m.group(1).rstrip("/") or "/")
    return achadas


def main():
    pagina = PAGINA.read_text(encoding="utf-8", errors="replace")
    servidor = SERVIDOR.read_text(encoding="utf-8", errors="replace")

    registradas = rotas_registradas(servidor)
    chamadas = rotas_chamadas(pagina)

    if not registradas:
        print("conferir_rotas: nenhuma rota encontrada em servidor_web.cpp")
        return 1

    mudas = sorted(chamadas - registradas)
    ociosas = sorted(registradas - chamadas - IMPLICITAS)

    for r in mudas:
        print("ERRO  a pagina chama %s, que o firmware nao registra" % r)
    for r in ociosas:
        print("aviso rota %s registrada e nunca chamada pela pagina" % r)

    if mudas:
        return 1
    print("conferir_rotas: %d rotas registradas, %d chamadas pela pagina, "
          "nenhuma muda" % (len(registradas), len(chamadas)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
