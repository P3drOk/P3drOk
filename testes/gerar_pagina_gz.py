#!/usr/bin/env python3
"""Gera RoboCNC/pagina_web_gz.h a partir de RoboCNC/pagina_web.h.

pagina_web.h continua sendo a fonte: e nele que se edita a interface.
O firmware serve a versao comprimida, o que economiza ~53 kB de flash e
faz a pagina chegar no celular umas 3 vezes mais rapido -- num ponto de
acesso de ESP32 isso e a diferenca entre abrir na hora e esperar.

Rode depois de mexer na interface:
    python3 testes/gerar_pagina_gz.py

O banco de testes reprova se o gerado estiver desatualizado.
"""
import gzip, hashlib, pathlib, sys, textwrap

RAIZ = pathlib.Path(__file__).resolve().parent.parent
FONTE = RAIZ / "RoboCNC" / "pagina_web.h"
SAIDA = RAIZ / "RoboCNC" / "pagina_web_gz.h"


def extrair_html(texto: str) -> bytes:
    i = texto.index('R"rawliteral(') + len('R"rawliteral(')
    j = texto.rindex(')rawliteral"')
    return texto[i:j].encode("utf-8")


def gerar():
    html = extrair_html(FONTE.read_text(encoding="utf-8"))
    sha = hashlib.sha256(html).hexdigest()
    # mtime=0 para a saida ser identica a cada rodada com a mesma entrada:
    # sem isso o arquivo gerado apareceria como alterado em todo commit.
    gz = gzip.compress(html, compresslevel=9, mtime=0)

    linhas = []
    for i in range(0, len(gz), 16):
        linhas.append("  " + " ".join("0x%02x," % b for b in gz[i:i + 16]))

    corpo = "\n".join(linhas)
    SAIDA.write_text(
        "#pragma once\n"
        "#include <Arduino.h>\n\n"
        "// =====================================================================\n"
        "//  GERADO AUTOMATICAMENTE - nao edite a mao.\n"
        "//\n"
        "//  Fonte: pagina_web.h   (edite la e rode testes/gerar_pagina_gz.py)\n"
        "//  HTML cru: %d bytes  ->  gzip: %d bytes  (%.0f%% menor)\n"
        "//\n"
        "//  O servidor manda com Content-Encoding: gzip. Todo navegador de\n"
        "//  celular descomprime sozinho.\n"
        "// =====================================================================\n\n"
        "// sha256 do HTML de origem: o banco de testes reprova se divergir.\n"
        "#define PAGINA_HTML_SHA \"%s\"\n\n"
        "const uint8_t PAGINA_HTML_GZ[] PROGMEM = {\n%s\n};\n\n"
        "const size_t PAGINA_HTML_GZ_LEN = sizeof(PAGINA_HTML_GZ);\n"
        % (len(html), len(gz), 100 * (1 - len(gz) / len(html)), sha, corpo),
        encoding="utf-8")
    return len(html), len(gz), sha


def conferir():
    if not SAIDA.exists():
        return False, "pagina_web_gz.h nao existe"
    html = extrair_html(FONTE.read_text(encoding="utf-8"))
    sha = hashlib.sha256(html).hexdigest()
    texto = SAIDA.read_text(encoding="utf-8")
    marca = '#define PAGINA_HTML_SHA "'
    if marca not in texto:
        return False, "pagina_web_gz.h sem marca de origem"
    atual = texto.split(marca)[1].split('"')[0]
    if atual != sha:
        return False, ("pagina_web_gz.h esta velho: pagina_web.h mudou desde "
                       "a ultima geracao")
    return True, "pagina_web_gz.h confere com pagina_web.h"


if __name__ == "__main__":
    if "--conferir" in sys.argv:
        ok, msg = conferir()
        cor = "\033[32mPASSA\033[0m" if ok else "\033[31mFALHA\033[0m"
        print("  %s    %s" % (cor, msg))
        if not ok:
            print("           rode: python3 testes/gerar_pagina_gz.py")
        sys.exit(0 if ok else 1)
    cru, comp, _ = gerar()
    print("pagina_web_gz.h gerado: %d -> %d bytes (%.0f%% menor)"
          % (cru, comp, 100 * (1 - comp / cru)))
