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
PAGINA = RAIZ / "Robo2dof" / "pagina_web.h"
SERVIDOR = RAIZ / "Robo2dof" / "servidor_web.cpp"
BANCO = RAIZ / "testes" / "banco.cpp"

# Rotas que o navegador pede sozinho, sem aparecer em nenhum fetch().
IMPLICITAS = {"/", "/manifest.webmanifest", "/icone.svg"}

# Rotas que o firmware serve DE PROPOSITO sem nenhum botao no painel.
# Sem esta lista elas viram um aviso solto na compilacao, e aviso que
# ninguem sabe explicar acaba ignorado -- inclusive quando for de verdade.
# O banco de firmware continua exercitando cada uma delas.
SEM_PAGINA = {
    "/api/mover_xy":
        "o botao IR saiu do painel a pedido de quem opera (tocar na mesa "
        "nao move mais o braco); a rota fica para uso externo",
}


def rotas_registradas(texto):
    return {m.group(1) for m in re.finditer(r'server\.on\(\s*"([^"]+)"', texto)}


def _lista_c(texto, nome):
    """Extrai os literais de uma lista de strings C chamada `nome`."""
    i = texto.find("static const char* %s[]" % nome)
    if i < 0:
        return None
    j = texto.index("};", i)
    return {m.group(1) for m in re.finditer(r'"(/api/[^"]+)"', texto[i:j])}


def confere_varredura(servidor, banco):
    """Toda rota de POST tem de estar na varredura de valores hostis.

    O banco dispara lixo em cada rota registrada -- numeros absurdos,
    texto onde se espera numero, caminho de arquivo onde se espera
    indice. Uma rota fora dessa lista e uma rota que ninguem nunca
    testou com o que um cliente errado manda, e a lista ficava para tras
    calada: era so acrescentar uma rota e esquecer de inscreve-la.
    """
    registradas = {m.group(1) for m in
                   re.finditer(r'server\.on\(\s*"(/api/[^"]+)",\s*HTTP_POST', servidor)}
    varridas = _lista_c(banco, "ROTAS_POST")
    fora = _lista_c(banco, "ROTAS_POST_FORA")
    if varridas is None or fora is None:
        print("ERRO  nao achei ROTAS_POST/ROTAS_POST_FORA em banco.cpp")
        return 1
    faltando = sorted(registradas - varridas - fora)
    sobrando = sorted((varridas | fora) - registradas)
    for r in faltando:
        print("ERRO  %s aceita POST e nao esta na varredura de valores "
              "hostis (ROTAS_POST em banco.cpp)" % r)
    for r in sobrando:
        print("ERRO  a varredura cita %s, que o firmware nao registra "
              "mais" % r)
    return 1 if (faltando or sobrando) else 0


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
    ociosas = sorted(registradas - chamadas - IMPLICITAS - set(SEM_PAGINA))

    for r in mudas:
        print("ERRO  a pagina chama %s, que o firmware nao registra" % r)
    for r in ociosas:
        print("aviso rota %s registrada e nunca chamada pela pagina" % r)
    # Uma lista que envelhece calada nao serve: se a pagina voltar a
    # chamar a rota, o motivo aqui deixou de valer e tem de sair.
    for r in sorted(set(SEM_PAGINA) & chamadas):
        print("ERRO  %s esta em SEM_PAGINA mas a pagina chama: tire da lista"
              % r)
        mudas.append(r)
    for r in sorted(set(SEM_PAGINA) - registradas):
        print("ERRO  %s esta em SEM_PAGINA mas o firmware nao registra" % r)
        mudas.append(r)
    for r, porque in sorted(SEM_PAGINA.items()):
        print("sem botao no painel, de proposito: %s -- %s" % (r, porque))

    ruim = 1 if mudas else 0
    if BANCO.exists():
        ruim |= confere_varredura(servidor, BANCO.read_text(encoding="utf-8"))
    if ruim:
        return 1
    print("conferir_rotas: %d rotas registradas, %d chamadas pela pagina, "
          "nenhuma muda, e toda rota de POST esta na varredura hostil"
          % (len(registradas), len(chamadas)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
