#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Confere o gerador de QR da interface contra um decodificador de verdade.

Um QR desenhado errado nao parece errado: ele fica quadradinho, bonito, e
simplesmente nao abre em nenhum celular. Nenhuma inspecao visual pega isso
-- e foi assim que o primeiro deste projeto saiu com os bits de formato na
ordem invertida.

O bloco entre /* <<QR>> */ e /* <</QR>> */ de pagina_web.h e extraido,
rodado no Node, renderizado como imagem e LIDO de volta pelo detector do
OpenCV. Se o texto nao voltar igual, reprova.

Sem Node ou sem OpenCV o guarda se declara pulado em vez de fingir que
passou -- guarda que passa sozinho e pior que guarda nenhum.
"""
import json
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

RAIZ = pathlib.Path(__file__).resolve().parent.parent
PAGINA = RAIZ / "Robo2dof" / "pagina_web.h"

# O que a maquina realmente codifica, mais casos que ja quebraram: rede
# com espaco no nome, texto acentuado (caminho UTF-8) e o teto de tamanho.
TEXTOS = [
    "WIFI:T:nopass;S:Robo2dof;;",
    "http://192.168.4.1/",
    "http://robo2dof.local/",
    "WIFI:T:nopass;S:Oficina 2G;;",
    "Robo2dof — braço de solda, peça nº 42, ângulo 37,5°",
    "A" * 100,
    "B" * 213,
]


def pular(motivo):
    print("  \033[33mPULADO\033[0m   guarda do QR: %s" % motivo)
    return 0


def main():
    if not shutil.which("node"):
        return pular("node nao encontrado")
    try:
        import cv2          # noqa: F401
        import numpy as np  # noqa: F401
    except ImportError:
        return pular("opencv/numpy nao instalados (pip install opencv-python-headless)")

    texto = PAGINA.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"/\* <<QR>>.*?\*/(.*?)/\* <</QR>> \*/", texto, re.S)
    if not m:
        print("ERRO  nao achei o bloco <<QR>> em pagina_web.h")
        return 1
    js = m.group(1)
    if "function qrGerar" not in js:
        print("ERRO  o bloco <<QR>> nao contem qrGerar()")
        return 1

    import cv2
    import numpy as np

    with tempfile.TemporaryDirectory() as tmp:
        arq = pathlib.Path(tmp) / "qr.js"
        arq.write_text(js + "\nmodule.exports={qrGerar};\n", encoding="utf-8")

        det = cv2.QRCodeDetector()
        falhas = 0
        for t in TEXTOS:
            r = subprocess.run(
                ["node", "-e",
                 "const {qrGerar}=require(process.argv[1]);"
                 "console.log(JSON.stringify(qrGerar(process.argv[2])));",
                 str(arq), t],
                capture_output=True, text=True)
            if r.returncode:
                print("ERRO  node falhou em %r: %s" % (t[:30], r.stderr.strip()[:200]))
                falhas += 1
                continue
            mat = json.loads(r.stdout)
            n = len(mat)
            quieto, esc = 4, 6          # zona de silencio e escala
            lado = (n + 2 * quieto) * esc
            img = np.ones((lado, lado), np.uint8) * 255
            for y in range(n):
                for x in range(n):
                    if mat[y][x]:
                        img[(y + quieto) * esc:(y + quieto + 1) * esc,
                            (x + quieto) * esc:(x + quieto + 1) * esc] = 0
            lido, _, _ = det.detectAndDecode(img)
            if lido != t:
                falhas += 1
                print("ERRO  QR de %r nao decodifica (leu %r)" % (t[:40], lido[:40]))

        if falhas:
            print("conferir_qr: %d de %d falharam" % (falhas, len(TEXTOS)))
            return 1
        print("  \033[32mPASSA\033[0m    os %d codigos QR da interface sao lidos por um "
              "decodificador de verdade" % len(TEXTOS))
        return 0


if __name__ == "__main__":
    sys.exit(main())
