#!/usr/bin/env python3
"""Finge ser o ESP32 para exercitar a interface num navegador de verdade.

Serve a pagina extraida de pagina_web.h e responde as rotas da API com
dados plausiveis, registrando tudo que a interface pediu.
"""
import json, re, sys, threading
from http.server import BaseHTTPRequestHandler, HTTPServer
from urllib.parse import urlparse, parse_qs

RAIZ = sys.argv[1] if len(sys.argv) > 1 else "Robo2dof/pagina_web.h"
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
    "j1min": -95.0, "j1max": 95.0, "j2min": -120.0, "j2max": 30.0,
    "trajN": 24, "trajMs": 590, "trajPct": 0, "escala": 100,
    "progN": 3, "progIdx": 0, "progPct": 0, "ensaio": False,
    "velCordao": 5.0, "velC": 5.0,
    "protCurso": True, "protDobra": True, "protEnv": False,
    "velN": 20.0, "velP": 2.0, "velA": 12.0, "acel1": 60.0, "acel2": 60.0,
    "ppv1": 10000, "red1": 16.5, "ppv2": 10000, "red2": 4.0,
    "inv1": False, "inv2": True,
    "suav": 120, "afer1": 0, "afer2": 0,
    "v1": 0, "v2": 0, "vPonta": 0.0, "ppg1": 458.33, "ppg2": 111.11,
    "l1": 450.0, "l2": 400.0, "dobra": 20.0, "envY": -150.0, "envR": 40.0,
    "aprBotao": False, "apr": False, "aprSolto": False, "aprN": 0,
    "op": False, "pausa": False, "desf": False, "ciclos": 137, "cicSes": 9,
    "mesaOn": True, "mesaX0": 180.0, "mesaX1": 640.0,
    "mesaY0": -260.0, "mesaY1": 260.0,
    "m1": 9.02, "m2": -14.38, "m1ok": True, "m2ok": True, "trecho": 0,
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

REDE = {"ssid": "Robo2dof", "ip": "192.168.4.1", "nome": "robo2dof"}

# Previa de uma peca do cartao. Elos DIFERENTES dos da maquina de
# proposito: e o caso que tem de acender o aviso de peca errada.
PREVIA = {"n": 4, "l1": 380.0, "l2": 300.0, "l1Maq": 450.0, "l2Maq": 400.0,
          "pts": [{"x": 300, "y": -40, "s": 1}, {"x": 380, "y": -40, "s": 0},
                  {"x": 380, "y": 60, "s": 1}, {"x": 300, "y": 60, "s": 0}]}

# Maquina saudavel com historico: e o estado em que a tela tem de mostrar
# tudo. O banco troca campos daqui para encenar defeito.
SAUDE = {
    "up": 9312, "heap": 236864, "heapMin": 198000,
    "flashUso": 1721921, "flashTot": 3145728,
    "ciclos": 137, "ciclosSes": 9, "abortados": 3,
    "arcoS": 4210, "manut": 62,
    "enc1": {"ok": 18402, "falha": 11, "taxa": 99, "idade": 48,
             "graus": 9.02, "vale": True},
    "enc2": {"ok": 0, "falha": 0, "taxa": 0, "idade": 0,
             "graus": 0.0, "vale": False},
    "trav": 0, "alerta": 2, "alarme1": False, "alarme2": False,
    "cartao": True, "cartaoLivre": 7412992, "cartaoTotal": 15204352,
    "apr": False, "aprBotao": False, "estop": False, "ota": False,
}

CALIBRACAO = {
    "cal1": True, "cal2": True,
    "ppv1": 10000, "ppv2": 10000, "red1": 16.5, "red2": 4.0,
    "ppg1": 458.33, "ppg2": 111.11,
    "g1min": -95.0, "g1max": 95.0, "g2min": -120.0, "g2max": 30.0,
    "marca1": False, "marca2": False, "voltas1": 0.0, "voltas2": 0.0,
    "passos1": 0, "passos2": 0, "enc1": True, "enc2": False,
    "mesaOn": True, "mesaN": 3,
    "mesaX0": 180.0, "mesaX1": 640.0, "mesaY0": -260.0, "mesaY1": 260.0,
    "envY": -150.0, "envR": 40.0,
}

REGISTRO = {"n": 4, "linhas": [
    {"s": 9280, "t": "programa concluido: 3 pontos"},
    {"s": 8940, "t": "PROGRAMA COM ARCO iniciado: 3 pontos, cordao a 5.0 mm/s"},
    {"s": 8901, "t": "programa carregado: cantoneira (3 pontos)"},
    {"s": 12,   "t": "sistema iniciado"},
]}

import math as _mm
# reg2 = 0 e a bancada do operador: um driver so, junta 2 nao ligada. O
# banco troca isso por /teste/encoder para ver tambem a junta ligada que
# nao responde -- os dois casos escrevem coisas diferentes na tela.
_enc = {"n": 0, "reg2": 0, "motivo2": 1,
        "trvOn": False, "trvJ": 0, "trvN": 0,
        "zEn1": True,
        # Sem registrador de som gravado: e como a maquina sai de fabrica,
        # e e o estado em que o botao do som NAO pode funcionar.
        "pmReg": 0, "pmOn": 1, "pmOff": 0, "pmF16": False,
        "rel": "Registradores que MUDARAM entre as duas fotos:\n\n"
               "  reg  98 :     0 -> 1      (0x0000 -> 0x0001)\n\n"
               "UM registrador so mudou: e esse o endereco do parametro."}
# Ultima escrita de parametro, como o firmware a devolve em
# /api/encoder/escrita: "mandei" e "entrou" sao coisas diferentes, e a
# tela so pode dizer a segunda depois da releitura.
_esc = {"pedida": False, "fim": False, "ok": False,
        "reg": 0, "valor": 0, "lido": 0, "motivo": ""}
def _encoder():
    _enc["n"] += 1
    # erro pequeno e oscilante na junta 1, junta 2 sem leitura
    e1 = 0.12 * _mm.sin(_enc["n"] / 6.0)
    return {"ativo": True, "baud": 19200, "par": 0, "func": 3, "per": 50,
            "b32": True, "lo": False,
            "crOn": True, "crVig": True, "crTol": 0.10, "crMax": 3.00,
            "crAlr": 1.00, "crTent": 3,
            "crEst": 3, "crN": 1, "crOk": 12, "crFalha": 1, "crAlerta": 0,
            "crMotivo": "posicao conferida pelo encoder",
            "trvOn": _enc["trvOn"], "trvJ": _enc["trvJ"], "trvN": _enc["trvN"],
            "zSin": True, "zIr": True, "zTol": 0.30,
            "zEn1": _enc["zEn1"], "zEn2": False,
            "zEst": 3, "zG1": 12.40, "zG2": 0.0,
            "zMot": "posicao recuperada do encoder",
            "pmReg": _enc["pmReg"], "pmOn": _enc["pmOn"],
            "pmOff": _enc["pmOff"], "pmF16": _enc["pmF16"],
            "id1": 1, "id2": 2, "reg1": 90, "reg2": _enc["reg2"],
            "cv1": 10000, "cv2": 10000,
            "t1": estado["t1"], "t2": estado["t2"],
            "j1min": estado["j1min"], "j1max": estado["j1max"],
            "j2min": estado["j2min"], "j2max": estado["j2max"],
            "j": [{"ok": True, "bruto": 123456 + _enc["n"] * 7, "ref": 123456,
                   "graus": estado["t1"] - e1, "erro": e1,
                   "idade": 20, "n": _enc["n"], "falhas": 2, "motivo": 0,
                   "delta": 7, "vel": 140.0, "rpm": 0.84, "sent": 1,
                   "passos": _enc["n"] * 7, "inv": 2,
                   "bmin": 123456, "bmax": 123456 + _enc["n"] * 7,
                   "vmax": 220.0, "vmin": -95.0},
                  {"ok": False, "bruto": 0, "ref": 0, "graus": 0.0, "erro": 0.0,
                   "idade": 9999, "n": 0, "falhas": 0,
                   "motivo": _enc["motivo2"],
                   "delta": 0, "vel": 0.0, "rpm": 0.0, "sent": 0,
                   "passos": 0, "inv": 0,
                   "bmin": 0, "bmax": 0, "vmax": 0.0, "vmin": 0.0}],
            "quadro": "junta 1  2 registradores  -> 01 03 10 00 00 02 C1 0C"
                      "   <- 01 03 04 E2 40 00 01 5B 2E"}

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
        if caminho == "/api/encoder":
            return self._envia(json.dumps(_encoder()))
        if caminho == "/api/encoder/escrita":
            return self._envia(json.dumps(_esc))
        if caminho == "/api/encoder/teste":
            return self._envia(_enc["rel"], "text/plain")
        if caminho == "/api/calibracao":
            return self._envia(json.dumps(CALIBRACAO))
        if caminho == "/api/saude":
            return self._envia(json.dumps(SAUDE))
        if caminho == "/api/registro":
            return self._envia(json.dumps(REGISTRO))
        if caminho == "/api/sd/previa":
            return self._envia(json.dumps(PREVIA))
        if caminho == "/api/rede":
            return self._envia(json.dumps(REDE))
        if caminho == "/api/sd":
            return self._envia(json.dumps(SD))
        if caminho == "/api/sd/lista":
            t = q.get("tipo", ["prog"])[0]
            return self._envia(json.dumps({"tipo": t, "pronto": True,
                                           "arq": LISTA.get(t, [])}))
        if caminho == "/manifest.webmanifest":
            return self._envia(json.dumps({"name": "Robo2dof",
                                           "short_name": "Robo2dof",
                                           "start_url": "/",
                                           "display": "standalone"}),
                               "application/manifest+json")
        if caminho == "/icone.svg":
            return self._envia("<svg xmlns='http://www.w3.org/2000/svg'/>",
                               "image/svg+xml")
        return self._envia("nao existe", "text/plain", 404)

    def do_POST(self):
        caminho, q = self._rota()
        # O corpo precisa ser drenado sempre: deixar bytes no socket
        # atrapalha a proxima requisicao da mesma conexao.
        tam = int(self.headers.get("Content-Length", 0) or 0)
        corpo = self.rfile.read(tam) if tam else b""
        # Gancho so do banco de testes: permite encenar outros estados da
        # maquina (sem calibracao, servos desligados, executando...).
        if caminho == "/teste/estado":
            estado.update(json.loads(corpo or b"{}"))
            return self._envia("ok", "text/plain")
        if caminho == "/teste/previa":
            PREVIA.update(json.loads(corpo or b"{}"))
            return self._envia("ok", "text/plain")
        if caminho == "/teste/calibracao":
            CALIBRACAO.update(json.loads(corpo or b"{}"))
            return self._envia("ok", "text/plain")
        if caminho == "/teste/saude":
            SAUDE.update(json.loads(corpo or b"{}"))
            return self._envia("ok", "text/plain")
        if caminho == "/teste/encoder":
            _enc.update(json.loads(corpo or b"{}"))
            return self._envia("ok", "text/plain")
        if caminho == "/teste/escrita":
            _esc.update(json.loads(corpo or b"{}"))
            return self._envia("ok", "text/plain")
        # As rotas de escrita respondem como o firmware: 200 e texto. O
        # que a tela mostra depois vem de /api/encoder/escrita, e o banco
        # encena esse resultado por /teste/escrita.
        if caminho == "/api/param/config":
            _enc["pmReg"] = int(q.get("reg", ["0"])[0])
            _enc["pmOn"] = int(q.get("on", ["1"])[0])
            _enc["pmOff"] = int(q.get("off", ["0"])[0])
            _enc["pmF16"] = q.get("f16", ["0"])[0] == "1"
            return self._envia("ok", "text/plain")
        return self._envia("ok", "text/plain")


def servir():
    s = HTTPServer(("127.0.0.1", PORTA), H)
    s.pedidos = pedidos
    s.serve_forever()


if __name__ == "__main__":
    print("servindo em http://127.0.0.1:%d" % PORTA, flush=True)
    servir()
