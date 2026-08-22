#!/usr/bin/env python3
"""Finge ser o ESP32 para exercitar a interface num navegador de verdade.

Serve a pagina extraida de pagina_web.h e responde as rotas da API com
dados plausiveis, registrando tudo que a interface pediu.
"""
import json, re, sys, threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

RAIZ = sys.argv[1] if len(sys.argv) > 1 else "RoboCNC/pagina_web.h"
PORTA = int(sys.argv[2]) if len(sys.argv) > 2 else 8099

# Serve exatamente o que o ESP32 serve: os bytes de pagina_web_gz.h com
# Content-Encoding: gzip. Servir o HTML cru daqui testaria um caminho que
# o robo nao usa.
import gzip as _gzip
import pathlib as _pl
import re as _re

fonte = open(RAIZ, encoding="utf-8").read()
i = fonte.index('R"rawliteral(') + len('R"rawliteral(')
j = fonte.rindex(')rawliteral"')
PAGINA = fonte[i:j].encode()

_gzh = _pl.Path(RAIZ).parent / "pagina_web_gz.h"
PAGINA_GZ = None
if _gzh.exists():
    _txt = _gzh.read_text(encoding="utf-8")
    _bytes = bytes(int(b, 16) for b in _re.findall(r"0x([0-9a-fA-F]{2}),", _txt))
    if _bytes:
        # Conferencia dura: o comprimido tem de bater com a fonte.
        if _gzip.decompress(_bytes) != PAGINA:
            raise SystemExit("pagina_web_gz.h nao corresponde a pagina_web.h "
                             "-- rode testes/gerar_pagina_gz.py")
        PAGINA_GZ = _bytes

pedidos = []          # tudo que a interface chamou
estado = {
    "modo": "MANUAL", "calib": "INATIVO", "calibEixo": 0,
    "p1": 250, "p2": -400, "t1": 9.0, "t2": -14.4, "x": 388.0, "y": -17.0,
    "precisao": False, "solda": False, "servos": True, "movendo": False,
    "alarme1": False, "alarme2": False, "cal1": True, "cal2": True,
    "j1min": -90.0, "j1max": 90.0, "j2min": -90.0, "j2max": 90.0,
    "trajN": 24, "trajMs": 590, "trajPct": 0, "escala": 100,
    "progN": 3, "progIdx": 0, "progPct": 0, "ensaio": False,
    "velCordao": 5.0, "velC": 5.0,
    "protCurso": True, "protDobra": True, "protEnv": False,
    "velN": 3000, "velP": 500, "velA": 1200, "acel1": 8000, "acel2": 8000,
    "ppv1": 10000, "red1": 1.0, "ppv2": 10000, "red2": 1.0,
    "v1": 0, "v2": 0, "vPonta": 0.0, "ppg1": 27.78, "ppg2": 27.78,
    "l1": 200.0, "l2": 200.0, "dobra": 20.0, "envY": -150.0, "envR": 40.0,
    "bt": False,
    "msg": "Pronto. Habilite os servos para comecar",
}
PONTOS = {"conferido": True, "pts": [
    {"t1": 10.0, "t2": -30.0, "x": 371, "y": -66, "s": 1},
    {"t1": 25.0, "t2": -30.0, "x": 375, "y": 40, "s": 1,
     "av": "cordao 2->3: junta 2 precisa ir a 132.8 graus a 41% do trecho, "
           "e o curso vai ate 89.5"},
    {"t1": 40.0, "t2": -50.0, "x": 340, "y": 111, "s": 0},
]}
# caminho a mao livre: um arco de circulo, metade com solda
import math as _m
TRAJ = [[round(300 * _m.cos(_a / 40.0), 1), round(300 * _m.sin(_a / 40.0) - 60, 1),
         1 if _a < 20 else 0] for _a in range(-20, 21)]

SD = {"estado": "PRONTO", "ocupado": False, "seq": 7,
      "totalMB": 3782, "livreMB": 3779, "msg": "cartao montado"}
LISTA = {
    "prog": [{"n": "chapa 30x60", "b": 340}, {"n": "flange-4-lados", "b": 512}],
    "traj": [{"n": "contorno organico", "b": 18420}],
    "cfg":  [{"n": "backup-oficina", "b": 288}],
}


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _envia(self, corpo, tipo="application/json", codigo=200):
        if isinstance(corpo, str):
            corpo = corpo.encode()
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(corpo)))
        self.end_headers()
        self.wfile.write(corpo)

    def _rota(self):
        u = urlparse(self.path)
        pedidos.append((self.command, u.path, parse_qs(u.query)))
        return u.path, parse_qs(u.query)

    def do_GET(self):
        caminho, q = self._rota()
        if caminho == "/":
            if PAGINA_GZ is not None:
                self.send_response(200)
                self.send_header("Content-Type", "text/html")
                self.send_header("Content-Encoding", "gzip")
                self.send_header("Content-Length", str(len(PAGINA_GZ)))
                self.end_headers()
                self.wfile.write(PAGINA_GZ)
                return
            return self._envia(PAGINA, "text/html")
        if caminho == "/api/status":
            return self._envia(json.dumps(estado))
        if caminho == "/api/pontos":
            return self._envia(json.dumps(PONTOS))
        if caminho == "/api/trajetoria":
            return self._envia(json.dumps({"pts": TRAJ}))
        if caminho == "/api/sd":
            return self._envia(json.dumps(SD))
        if caminho == "/api/sd/lista":
            t = q.get("tipo", ["prog"])[0]
            return self._envia(json.dumps({"tipo": t, "pronto": True,
                                           "arq": LISTA.get(t, [])}))
        if caminho == "/manifest.webmanifest":
            return self._envia(json.dumps({"name": "RoboCNC 2DOF",
                                           "short_name": "RoboCNC",
                                           "start_url": "/",
                                           "display": "standalone"}),
                               "application/manifest+json")
        if caminho == "/icone.svg":
            return self._envia("<svg xmlns='http://www.w3.org/2000/svg'/>",
                               "image/svg+xml")
        return self._envia("nao existe", "text/plain", 404)

    def do_POST(self):
        caminho, q = self._rota()
        # Gancho so do banco de testes: permite encenar outros estados da
        # maquina (sem calibracao, servos desligados, executando...).
        if caminho == "/teste/estado":
            tam = int(self.headers.get("Content-Length", 0))
            estado.update(json.loads(self.rfile.read(tam) or b"{}"))
            return self._envia("ok", "text/plain")
        return self._envia("ok", "text/plain")


def servir():
    s = HTTPServer(("127.0.0.1", PORTA), H)
    s.pedidos = pedidos
    s.serve_forever()


if __name__ == "__main__":
    print("servindo em http://127.0.0.1:%d" % PORTA, flush=True)
    servir()
