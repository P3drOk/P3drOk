#pragma once
#include <Arduino.h>

const char PAGINA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no,viewport-fit=cover">
<meta name="theme-color" content="#0f1216">
<!-- "Adicionar a tela inicial" no celular abre em tela cheia, sem barra
     de endereco: e o que faz a pagina se comportar como aplicativo. -->
<link rel="manifest" href="/manifest.webmanifest">
<link rel="apple-touch-icon" href="/icone.svg">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="apple-mobile-web-app-title" content="RoboCNC">
<meta name="mobile-web-app-capable" content="yes">
<title>Estacao de solda - Robo 2DOF</title>
<style>
:root{
  --fundo:#e8e4dc; --mesa:#f6f4ef; --painel:#fbfaf7; --face:#eceae3;
  --linha:#c9c4b8; --linha2:#a8a294;
  --letra:#1c2530; --letra2:#5d6875; --letra3:#8a9099;
  --arco:#1f4f8f; --arco2:#8fa8c8;
  --quente:#c2410c; --brasa:#b91c1c;
  --pronto:#15803d;
  --grade:31,79,143; --papel:#f6f4ef; --sombra:rgba(28,37,48,.16);
  --mono:ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,monospace;
}
html[data-tema="escuro"]{
  --fundo:#0f1216; --mesa:#141920; --painel:#1b222b; --face:#232c37;
  --linha:#2f3a47; --linha2:#3c4a59;
  --letra:#eef1f4; --letra2:#8e9aa8; --letra3:#5c6875;
  --arco:#7d9dff; --arco2:#4a5f9e;
  --quente:#ff6a2b; --brasa:#ff3b1f;
  --pronto:#38cf82;
  --grade:125,157,255; --papel:#0d1116; --sombra:rgba(0,0,0,.55);
}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html,body{margin:0;height:100%;width:100%;overflow-x:hidden;
 background:var(--fundo);color:var(--letra);
 font:14px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;overscroll-behavior:none}
button,input{font:inherit;color:inherit}
:focus-visible{outline:2px solid var(--arco);outline-offset:2px}
@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
.rot{font-family:var(--mono);font-size:9.5px;letter-spacing:.17em;
 text-transform:uppercase;color:var(--letra2)}

.app{display:grid;grid-template-rows:auto 1fr;height:100%;min-width:0;overflow:hidden}
.corpo{display:grid;grid-template-columns:1fr 400px;gap:10px;padding:10px;
 min-height:0;overflow:hidden}
/* Itens de grid nao encolhem abaixo do conteudo sem min-width:0.
   Sem isso o painel empurra a pagina e vaza na horizontal no celular. */
.corpo>*{min-width:0}
@media(max-width:1020px){
  .corpo{grid-template-columns:minmax(0,1fr);grid-template-rows:38vh 1fr}
}
@media(max-width:560px){
  .corpo{padding:7px;gap:7px}
  .regua{grid-template-columns:repeat(3,1fr)}
  .regua div:nth-child(4),.regua div:nth-child(5){border-top:1px solid var(--linha)}
  .regua div:nth-child(3){border-right:none}
  .regua b{font-size:14px}
  .placa{height:52px;padding:0 9px;gap:9px}
  .lamps{gap:5px;flex:1 1 auto;justify-content:flex-end}
  .lp{min-width:0;flex:0 1 auto;gap:4px;max-width:40px}
  .lp span{overflow:hidden;text-overflow:clip;white-space:nowrap;max-width:100%}
  .lp span{font-size:7px;letter-spacing:.04em}
  .estop{padding:10px 12px;letter-spacing:.06em;margin-left:2px;font-size:11px}
  .nome{font-size:10px;letter-spacing:.1em}
  .placa{gap:7px}
}

/* ---------- placa ---------- */
.placa{display:flex;align-items:center;gap:14px;padding:0 14px;height:58px;min-width:0;
 background:var(--painel);border-bottom:2px solid var(--linha2);
 box-shadow:0 1px 0 rgba(255,255,255,.5) inset}
.nome{font-family:var(--mono);font-size:13px;letter-spacing:.24em;font-weight:600;
 white-space:nowrap;flex:0 0 auto}
.nome b{color:var(--arco);font-weight:600}
.mod{font-family:var(--mono);font-size:8.5px;letter-spacing:.13em;color:var(--letra3);
 border-left:1px solid var(--linha);padding-left:12px;line-height:1.4}
@media(max-width:720px){.mod{display:none}}
.lamps{display:flex;gap:15px;margin-left:auto;min-width:0;overflow:hidden}
.lp{display:flex;flex-direction:column;align-items:center;gap:5px;min-width:52px}
.olho{width:11px;height:11px;border-radius:50%;background:var(--face);
 box-shadow:inset 0 1px 2px var(--sombra);border:1px solid var(--linha)}
.lp.on .olho{background:var(--pronto);box-shadow:0 0 9px var(--pronto)}
.lp.at .olho{background:var(--arco);box-shadow:0 0 9px var(--arco)}
.lp.hot .olho{background:var(--quente);box-shadow:0 0 14px var(--quente);animation:pi .7s infinite}
.lp.er .olho{background:var(--brasa);box-shadow:0 0 12px var(--brasa);animation:pi .45s infinite}
@keyframes pi{50%{opacity:.25;box-shadow:none}}
.lp span{font-family:var(--mono);font-size:7.5px;letter-spacing:.1em;color:var(--letra2);
 text-transform:uppercase}
.estop{flex:0 0 auto;background:var(--brasa);border:none;color:#fff;font-family:var(--mono);font-size:12px;
 font-weight:700;letter-spacing:.15em;padding:12px 20px;border-radius:4px;cursor:pointer;
 box-shadow:0 3px 0 rgba(0,0,0,.35);margin-left:8px}
.estop:active{box-shadow:0 1px 0 rgba(0,0,0,.35);transform:translateY(2px)}

/* ---------- mesa de tracado ---------- */
.quadro{background:var(--mesa);border:1px solid var(--linha);border-radius:5px;
 display:flex;flex-direction:column;min-height:0;overflow:hidden;position:relative}
.tela{flex:1;min-height:0;position:relative}
.tela canvas{position:absolute;inset:0;width:100%;height:100%}

.legenda{position:absolute;left:12px;top:12px;display:flex;flex-direction:column;gap:6px;
 pointer-events:none}
.lg{display:flex;align-items:center;gap:8px;font-family:var(--mono);font-size:9px;
 letter-spacing:.1em;color:var(--letra2);text-transform:uppercase}
.lg i{width:18px;border-top:3px solid var(--quente)}
.lg.d i{border-top:1px dashed #7b8795}
.lg.t i{border-top:2px solid #7b8795;opacity:.55}

.zoom{position:absolute;right:12px;top:12px;display:flex;flex-direction:column;gap:5px}
.zb{width:32px;height:32px;background:var(--painel);opacity:.94;border:1px solid var(--linha);
 border-radius:4px;color:var(--letra2);cursor:pointer;font-size:15px;line-height:1;
 backdrop-filter:blur(4px)}
.zb:hover{color:var(--letra);border-color:var(--linha2)}
.zb.pq{font-family:var(--mono);font-size:8.5px;letter-spacing:.04em}

.regua{display:grid;grid-template-columns:repeat(5,1fr);border-top:1px solid var(--linha);
 background:var(--painel)}
.regua div{padding:8px 4px;text-align:center;border-right:1px solid var(--linha)}
.regua div:last-child{border:none}
.regua b{display:block;font-family:var(--mono);font-size:16px;font-weight:500;margin-top:3px;
 font-variant-numeric:tabular-nums}
.regua b.mv{color:var(--arco)}
.regua b.hot{color:var(--quente)}

/* ---------- coluna ---------- */
.coluna{background:var(--mesa);border:1px solid var(--linha);border-radius:5px;
 display:flex;flex-direction:column;min-height:0;overflow:hidden}
.rol{overflow-y:auto;overflow-x:hidden;padding:10px;flex:1;scrollbar-width:thin;min-width:0}
/* Grudada no topo da coluna: a resposta de cada acao ("Ponto 3 gravado",
   "Movimento recusado: ...") tem que estar visivel sem rolar de volta. */
.tira{position:sticky;top:0;z-index:6;
 padding:10px 12px;background:var(--painel);border:1px solid var(--linha);
 border-left:3px solid var(--linha2);border-radius:3px;margin-bottom:9px;font-size:12px;
 color:var(--letra2);min-height:40px;line-height:1.45}
.tira.er{border-left-color:var(--brasa);color:#ffc6bc}
.tira.ok{border-left-color:var(--pronto)}

.et{border:1px solid var(--linha);border-radius:4px;margin-bottom:8px;
 background:var(--painel);overflow:hidden}
.et.feita{border-color:#2a5c42}
.et.agora{border-color:var(--arco);box-shadow:inset 3px 0 0 var(--arco)}
.cab{display:flex;align-items:center;gap:11px;padding:12px;cursor:pointer;user-select:none}
/* Sem seta = secao fixa, nao recolhe: o cursor nao pode prometer clique. */
.cab:not(:has(.chv)){cursor:default}
.mk{width:26px;height:26px;border-radius:3px;background:var(--face);color:var(--letra2);
 display:grid;place-items:center;font-family:var(--mono);font-size:12px;font-weight:700;
 flex:0 0 auto;border:1px solid var(--linha)}
.et.feita .mk{background:var(--pronto);color:#052a17;border-color:var(--pronto)}
.et.agora .mk{background:var(--arco);color:#0c1530;border-color:var(--arco)}
.tx{flex:1;min-width:0}
.tt{font-size:13px;font-weight:600}
.sb{font-family:var(--mono);font-size:9.5px;color:var(--letra2);margin-top:2px;display:block;
 overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.chv{color:var(--letra3);font-size:10px;transition:transform .15s}
.et.aberta .chv{transform:rotate(90deg)}
.dentro{display:none;padding:0 12px 12px}
.et.aberta .dentro{display:block}

h4{margin:15px 0 8px;font-family:var(--mono);font-size:9px;letter-spacing:.17em;
 color:var(--letra2);text-transform:uppercase;font-weight:600;padding-bottom:5px;
 border-bottom:1px solid var(--linha)}
h4:first-child{margin-top:0}

.b{display:block;width:100%;padding:11px;border-radius:3px;cursor:pointer;
 background:var(--face);border:1px solid var(--linha);margin-bottom:7px;text-align:center;
 font-size:13px;transition:background .12s}
.b:hover:not(:disabled){filter:brightness(.96)}
.b:active:not(:disabled){transform:translateY(1px)}
.b:disabled{opacity:.28;cursor:not-allowed}
.b.pri{background:var(--arco);border-color:var(--arco);color:#0c1530;font-weight:600}
.b.ok{background:var(--pronto);border-color:var(--pronto);color:#052a17;font-weight:600}
.b.quente{background:var(--quente);border-color:var(--quente);color:#fff;font-weight:600}
.b.rod{background:var(--brasa);border-color:var(--brasa);color:#fff;font-weight:600}
.b.mini{padding:8px;font-size:12px}

.eixo{display:grid;grid-template-columns:1fr 96px 1fr;gap:6px;align-items:center;
 margin-bottom:7px}
.eixo .id{text-align:center}
.eixo .fx{font-family:var(--mono);font-size:9px;color:var(--letra3);margin-top:2px}
.jb{background:var(--face);border:1px solid var(--linha);border-radius:3px;height:56px;
 font-size:19px;cursor:pointer;user-select:none;touch-action:none;color:var(--letra2)}
.jb:active,.jb.press{background:var(--arco);color:#0c1530;border-color:var(--arco)}

.lista{border:1px solid var(--linha);border-radius:3px;overflow:hidden;margin-bottom:8px}
.p{display:flex;align-items:center;gap:9px;padding:9px 10px;background:var(--face);
 border-bottom:1px solid var(--linha)}
.p.agora{background:var(--face);box-shadow:inset 3px 0 0 var(--quente)}
.p .n{width:20px;height:20px;border-radius:2px;background:var(--mesa);color:var(--letra2);
 display:grid;place-items:center;font-family:var(--mono);font-size:10px;font-weight:700;flex:0 0 auto}
.p .c{flex:1;font-family:var(--mono);font-size:11px;color:var(--letra2);
 font-variant-numeric:tabular-nums}
.p .c em{color:var(--letra);font-style:normal}
.mb{background:none;border:1px solid var(--linha);border-radius:2px;padding:4px 7px;
 font-family:var(--mono);font-size:9.5px;letter-spacing:.06em;cursor:pointer;
 color:var(--letra2);text-transform:uppercase}
.mb:hover{border-color:var(--letra2);color:var(--letra)}
.mb.x{color:var(--brasa);border-color:var(--linha)}
.tr{display:flex;align-items:center;gap:9px;padding:8px 10px 8px 39px;background:var(--painel);
 border-bottom:1px solid var(--linha);font-family:var(--mono);font-size:9.5px;
 letter-spacing:.05em;color:var(--letra2)}
.tr.q{color:var(--quente);font-weight:600}
.tr.ruim{background:rgba(185,28,28,.10)}
.avTr{background:rgba(185,28,28,.10);border-bottom:1px solid var(--linha);
 padding:2px 12px 9px 39px;font-size:11px;line-height:1.5;color:var(--brasa)}
.ch{width:40px;height:22px;border-radius:2px;background:var(--mesa);position:relative;
 cursor:pointer;flex:0 0 auto;border:1px solid var(--linha)}
.ch i{position:absolute;top:2px;left:2px;width:16px;height:16px;border-radius:1px;
 background:var(--letra3);transition:.14s}
.ch.on{background:var(--quente);border-color:var(--quente)}
.ch.on i{left:20px;background:#fff}
.lista .p:last-child,.lista .tr:last-child{border-bottom:none}
.nulo{padding:20px 12px;text-align:center;color:var(--letra2);font-size:12px;
 border:1px dashed var(--linha);border-radius:3px;margin-bottom:8px;line-height:1.5}

.cp{display:flex;align-items:center;gap:9px;margin-bottom:7px}
.cp label{flex:1;font-size:12px;color:var(--letra2)}
.cp input{width:88px;max-width:38%;flex:0 0 auto;background:var(--fundo);border:1px solid var(--linha);border-radius:2px;
 padding:8px 9px;text-align:right;font-family:var(--mono);font-size:12px}
.cp input:focus{outline:none;border-color:var(--arco)}
.cp .un{font-family:var(--mono);font-size:9px;color:var(--letra3);width:34px}
.nt{font-size:11.5px;color:var(--letra2);margin:0 0 10px;line-height:1.55}
/* Motivo de um botao estar fora de acao. Nada de botao morto e mudo. */
.pq2{display:none;font-size:11px;color:var(--quente);margin:-5px 0 10px;
 line-height:1.5;padding-left:2px}
.b:disabled{opacity:.42;cursor:not-allowed}
.nt b{color:var(--letra);font-weight:600}
.perigo{font-size:11.5px;background:var(--face);border-left:3px solid var(--quente);border-top:1px solid var(--linha);border-right:1px solid var(--linha);border-bottom:1px solid var(--linha);color:var(--letra);
 padding:10px 11px;border-radius:3px;margin-bottom:10px;line-height:1.55}
.res{font-family:var(--mono);font-size:10.5px;color:var(--arco);background:var(--face);
 border:1px solid var(--linha);border-radius:3px;padding:8px 10px;margin-bottom:9px;
 line-height:1.6}
.pgr{height:3px;background:var(--fundo);border-radius:2px;overflow:hidden;margin:9px 0 3px}
.pgr i{display:block;height:100%;background:var(--quente);width:0;transition:width .25s}

.veu{position:fixed;inset:0;background:rgba(20,25,32,.72);display:none;align-items:center;
 justify-content:center;padding:16px;z-index:70;backdrop-filter:blur(3px)}
.veu.on{display:flex}
.cx{background:var(--mesa);border:1px solid var(--linha);border-radius:5px;padding:20px;
 width:100%;max-width:400px;max-height:92vh;overflow-y:auto}
.cx h2{margin:0 0 4px;font-size:15px}
.pp{font-family:var(--mono);font-size:9.5px;letter-spacing:.15em;color:var(--arco);
 margin-bottom:12px}
.ins{background:var(--painel);border-left:3px solid var(--arco);border-radius:2px;
 padding:11px 12px;font-size:13px;margin-bottom:13px;line-height:1.55}

/* =====================================================================
   CASCA DE APLICATIVO
   No celular a pagina vira app: abas embaixo, no alcance do polegar,
   uma tela por vez e nada de rolagem horizontal. No computador a mesa
   de tracado fica sempre visivel e as abas comandam so a coluna.
   ===================================================================== */
.abas{display:none}
.pane{display:none}
.pane.on{display:block}

/* Botao de parada sempre alcancavel, em qualquer aba. */
.estop{flex:0 0 auto}

/* ---- JOYSTICK ---- */
.joyCx{display:grid;place-items:center;padding:6px 0 12px}
.joy{position:relative;width:min(74vw,300px);aspect-ratio:1;touch-action:none;
 user-select:none;-webkit-user-select:none}
.joyBase{position:absolute;inset:0;border-radius:50%;background:var(--mesa);
 border:1px solid var(--linha);
 box-shadow:inset 0 2px 14px var(--sombra)}
.joyCruz{position:absolute;inset:0;pointer-events:none;opacity:.5}
.joyCruz i{position:absolute;background:var(--linha)}
.joyCruz i:nth-child(1){left:8%;right:8%;top:50%;height:1px}
.joyCruz i:nth-child(2){top:8%;bottom:8%;left:50%;width:1px}
.joyMorta{position:absolute;left:50%;top:50%;width:24%;height:24%;
 transform:translate(-50%,-50%);border-radius:50%;
 border:1px dashed var(--linha2);pointer-events:none;opacity:.55}
/* O botao e MENOR que a zona morta de proposito: assim o circulo
   tracejado fica visivel em volta dele com o dedo fora, e o operador ve
   de onde o movimento comeca. */
.joyKnob{position:absolute;left:50%;top:50%;width:19%;height:19%;
 transform:translate(-50%,-50%);border-radius:50%;
 background:var(--painel);border:2px solid var(--arco);
 box-shadow:0 3px 12px var(--sombra);
 display:grid;place-items:center;transition:border-color .12s,background .12s}
.joy.ativo .joyKnob{background:var(--arco);border-color:var(--arco)}
.joy.ativo .joyKnob b{color:#fff}
/* Bloqueado: o disco nao pode parecer pronto quando o braco nao vai
   sair do lugar. */
.joy.bloq{opacity:.4}
.joy.bloq .joyKnob{border-color:var(--letra3)}
.joyMotivo{text-align:center;font-size:11.5px;color:var(--quente);
 margin:8px 0 0;line-height:1.5;min-height:1px}
.joyKnob b{font-family:var(--mono);font-size:9px;letter-spacing:.06em;
 color:var(--letra2);text-transform:uppercase;pointer-events:none}
.joyEix{position:absolute;font-family:var(--mono);font-size:9px;
 letter-spacing:.14em;color:var(--letra3);text-transform:uppercase;
 pointer-events:none}
.joyEix.jT{top:-16px;left:50%;transform:translateX(-50%)}
.joyEix.jB{bottom:-16px;left:50%;transform:translateX(-50%)}
.joyEix.jL{left:-6px;top:50%;transform:translate(-100%,-50%)}
.joyEix.jR{right:-6px;top:50%;transform:translate(100%,-50%)}
.joyLe{display:flex;gap:14px;justify-content:center;margin-top:20px;
 font-family:var(--mono);font-size:11px;color:var(--letra2);
 font-variant-numeric:tabular-nums}
.joyLe b{color:var(--letra);font-weight:600}

/* ---- CARTAO SD ---- */
.sdBar{display:flex;align-items:center;gap:9px;background:var(--painel);
 border:1px solid var(--linha);border-radius:3px;padding:9px 11px;margin-bottom:9px}
.sdBar .pt{width:9px;height:9px;border-radius:50%;background:var(--letra3);flex:0 0 auto}
.sdBar.ok .pt{background:var(--pronto)}
.sdBar.er .pt{background:var(--brasa)}
.sdBar.bz .pt{background:var(--quente);animation:pulsa 1s infinite}
@keyframes pulsa{50%{opacity:.25}}
.sdBar .tx{flex:1;font-family:var(--mono);font-size:10.5px;color:var(--letra2);
 line-height:1.45}
.sdBar .tx b{color:var(--letra);font-weight:600}
.seg{display:flex;gap:0;margin-bottom:9px;border:1px solid var(--linha);
 border-radius:3px;overflow:hidden}
.seg button{flex:1;background:var(--painel);border:none;padding:9px 4px;
 font-family:var(--mono);font-size:9.5px;letter-spacing:.09em;
 text-transform:uppercase;color:var(--letra2);cursor:pointer;
 border-right:1px solid var(--linha)}
.seg button:last-child{border-right:none}
.seg button.on{background:var(--arco);color:#fff}
.arq{display:flex;align-items:center;gap:8px;padding:9px 10px;
 background:var(--painel);border-bottom:1px solid var(--linha)}
.arq:last-child{border-bottom:none}
.arq .nm{flex:1;font-size:12.5px;overflow:hidden;text-overflow:ellipsis;
 white-space:nowrap}
.arq .kb{font-family:var(--mono);font-size:9.5px;color:var(--letra3);
 flex:0 0 auto}
.lista.arqs{border:1px solid var(--linha);border-radius:3px;overflow:hidden;
 margin-bottom:9px}
.linhaNome{display:flex;gap:7px;margin-bottom:9px}
.linhaNome input{flex:1;min-width:0;background:var(--fundo);
 border:1px solid var(--linha);border-radius:2px;padding:9px 10px;font-size:13px}
.linhaNome input:focus{outline:none;border-color:var(--arco)}
.linhaNome button{flex:0 0 auto}

@media(max-width:1020px){
  .corpo{grid-template-columns:minmax(0,1fr);grid-template-rows:minmax(0,1fr);
   padding:8px;gap:0;padding-bottom:0}
  /* Uma tela por vez: a mesa e uma aba como as outras. */
  .quadro{display:none}
  body[data-aba="mesa"] .quadro{display:flex}
  body[data-aba="mesa"] .coluna{display:none}
  .coluna{max-height:none}

  .app{grid-template-rows:auto minmax(0,1fr) auto}
  .abas{display:grid;grid-auto-flow:column;grid-auto-columns:1fr;
   background:var(--painel);border-top:1px solid var(--linha);
   padding-bottom:env(safe-area-inset-bottom)}
  .abas button{background:none;border:none;padding:9px 2px 8px;cursor:pointer;
   display:grid;justify-items:center;gap:3px;color:var(--letra3);
   border-top:2px solid transparent;margin-top:-1px}
  .abas button.on{color:var(--arco);border-top-color:var(--arco)}
  .abas svg{width:21px;height:21px;stroke:currentColor;fill:none;
   stroke-width:1.7;stroke-linecap:round;stroke-linejoin:round}
  .abas span{font-size:9.5px;letter-spacing:.03em}

  /* Cabecalho em duas linhas: cinco lampadas de 46px nao cabem em 390px
     ao lado do nome e do PARAR -- a primeira sai da tela. Nome em cima,
     lampadas embaixo, e o PARAR ocupando as duas linhas: alvo alto, do
     tamanho de um polegar, no canto onde o polegar ja esta. */
  .placa{display:grid;height:auto;gap:7px 10px;align-items:center;
   grid-template-columns:1fr auto;
   padding:calc(7px + env(safe-area-inset-top)) 10px 8px}
  .mod{display:none}
  .nome{font-size:12px;grid-column:1;grid-row:1}
  .lamps{grid-column:1;grid-row:2;margin-left:0;gap:2px;overflow:visible;
   display:grid;grid-auto-flow:column;grid-auto-columns:1fr}
  .lp{min-width:0;gap:4px}
  .estop{grid-column:2;grid-row:1/span 2;align-self:stretch;
   padding:0 17px;font-size:12px}

  /* As etapas viram secoes simples: a aba ja e a navegacao. */
  .rol{padding-bottom:14px}
}
@media(min-width:1021px){
  /* No computador a mesa nunca some; as abas comandam so a coluna. */
  .quadro{display:flex}
  .abasTopo{display:grid;grid-auto-flow:column;grid-auto-columns:1fr;
   border:1px solid var(--linha);border-radius:3px;overflow:hidden;
   margin-bottom:9px}
  .abasTopo button{background:var(--painel);border:none;border-right:1px solid var(--linha);
   padding:9px 4px;font-family:var(--mono);font-size:9.5px;letter-spacing:.09em;
   text-transform:uppercase;color:var(--letra2);cursor:pointer}
  .abasTopo button:last-child{border-right:none}
  .abasTopo button.on{background:var(--arco);color:#fff}
  .abasTopo button[data-aba="mesa"]{display:none}
}
@media(max-width:1020px){ .abasTopo{display:none} }
</style>
</head>
<body data-aba="mover">

<div class="app">
  <header class="placa">
    <div class="nome">ROBO<b>2DOF</b></div>
    <div class="mod">ESTACAO DE SOLDA<br>2 EIXOS · SERVO AC</div>
    <div class="lamps">
      <div class="lp" id="lModo"><i class="olho"></i><span id="lModoT">--</span></div>
      <div class="lp" id="lServo"><i class="olho"></i><span>servo</span></div>
      <div class="lp" id="lArco"><i class="olho"></i><span>arco</span></div>
      <div class="lp" id="lRede"><i class="olho"></i><span>rede</span></div>
      <div class="lp" id="lSd"><i class="olho"></i><span>cartao</span></div>
    </div>
    <button class="estop" id="btParar">PARAR</button>
  </header>

  <div class="corpo">
    <section class="quadro">
      <div class="tela">
        <canvas id="cv"></canvas>
        <div class="legenda">
          <div class="lg"><i></i>cordao · reta</div>
          <div class="lg d"><i></i>deslocamento · curva das juntas</div>
          <div class="lg t"><i></i>trajetoria gravada a mao livre</div>
        </div>
        <div class="zoom">
          <button class="zb" id="zMais" title="Aproximar">+</button>
          <button class="zb" id="zMenos" title="Afastar">&minus;</button>
          <button class="zb pq" id="zAuto" title="Enquadrar o braco">FIT</button>
          <button class="zb pq" id="zTema" title="Alternar tema">TEMA</button>
        </div>
      </div>
      <div class="regua">
        <div><span class="rot">junta 1</span><b id="hT1">0°</b></div>
        <div><span class="rot">junta 2</span><b id="hT2">0°</b></div>
        <div><span class="rot">X mm</span><b id="hX">0</b></div>
        <div><span class="rot">Y mm</span><b id="hY">0</b></div>
        <div><span class="rot">ponta mm/s</span><b id="hV">0</b></div>
      </div>
    </section>

    <aside class="coluna"><div class="rol">
      <div class="abasTopo" id="abasTopo"></div>
      <div class="tira" id="tira">Iniciando</div>

      <!-- ============================ MOVER ============================ -->
      <section class="pane" id="pnMover">
        <div class="et aberta">
          <div class="cab"><div class="mk">&#10021;</div>
            <div class="tx"><div class="tt">Comando manual</div>
            <span class="sb" id="sbMover">joystick das duas juntas</span></div></div>
          <div class="dentro">
            <div class="joyCx">
              <div class="joy" id="joy">
                <div class="joyBase"></div>
                <div class="joyCruz"><i></i><i></i></div>
                <div class="joyMorta"></div>
                <div class="joyKnob" id="joyKnob"><b id="joyTx">jog</b></div>
                <span class="joyEix jT">junta 2 +</span>
                <span class="joyEix jB">junta 2 &minus;</span>
                <span class="joyEix jL">j1 &minus;</span>
                <span class="joyEix jR">j1 +</span>
              </div>
            </div>
            <div class="joyLe">
              <span>J1 <b id="joyA">0%</b></span>
              <span>J2 <b id="joyB">0%</b></span>
            </div>
            <div class="joyMotivo" id="joyMotivo"></div>
            <div class="nt">Quanto mais longe do centro, mais rapido. O circulo
            tracejado e a zona morta. Soltando o dedo, o braco para.</div>
            <button class="b mini" id="btPrec">Precisao: desligada</button>

            <h4>Passo a passo</h4>
            <div class="eixo">
              <button class="jb" data-j="1" data-d="-1">&#8592;</button>
              <div class="id"><span class="rot">junta 1</span><div class="fx" id="fx1"></div></div>
              <button class="jb" data-j="1" data-d="1">&#8594;</button>
            </div>
            <div class="eixo">
              <button class="jb" data-j="2" data-d="-1">&#8595;</button>
              <div class="id"><span class="rot">junta 2</span><div class="fx" id="fx2"></div></div>
              <button class="jb" data-j="2" data-d="1">&#8593;</button>
            </div>

            <h4>Ir para um angulo</h4>
            <div class="cp"><label>Junta 1</label><input type="number" id="inMt1" step="0.5"><span class="un">°</span></div>
            <div class="cp"><label>Junta 2</label><input type="number" id="inMt2" step="0.5"><span class="un">°</span></div>
            <button class="b mini" id="btMover">Ir para esses angulos</button>
            <div class="pq2" id="qMover"></div>

            <h4>Atalhos</h4>
            <button class="b ok" id="btGravar">Gravar ponto na posicao atual</button>
            <div class="pq2" id="qGravar"></div>
            <button class="b mini" id="btHome">Ir para o zero da maquina</button>
            <div class="pq2" id="qHome"></div>
            <div class="nt">Tocar na mesa de tracado tambem leva a ponta ate o
            ponto tocado.</div>
          </div>
        </div>
      </section>

      <!-- ========================== PROGRAMA ========================== -->
      <section class="pane" id="pnProg">
        <div class="et aberta" id="e2" data-e="2">
          <div class="cab"><div class="mk">2</div>
            <div class="tx"><div class="tt">Ensinar o caminho</div>
            <span class="sb" id="sb2">nenhum ponto</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt"><b>Cordao reto numa chapa:</b> leve a ponta ate o inicio do cordao e grave o ponto 1. Leve ate o fim e grave o ponto 2. Ligue a chave do trecho 1&rarr;2. Pronto.</div>
            <div id="lista"></div>
            <button class="b mini" id="btLimpar">Apagar programa</button>
          </div>
        </div>

        <div class="et" id="e3" data-e="3">
          <div class="cab"><div class="mk">3</div>
            <div class="tx"><div class="tt">Ensaiar sem arco</div>
            <span class="sb">confirme o caminho antes de gastar chapa</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt">Percorre o programa inteiro com o rele travado desligado, na mesma velocidade e sequencia da solda.</div>
            <button class="b pri" id="btEnsaio">Executar ensaio</button>
            <div class="pq2" id="qEnsaio"></div>
          </div>
        </div>

        <div class="et" id="e4" data-e="4">
          <div class="cab"><div class="mk">4</div>
            <div class="tx"><div class="tt">Soldar</div>
            <span class="sb">execucao com arco aberto</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="perigo">O arco abre de verdade. Confira mascara, aterramento na peca e area livre em volta do braco.</div>
            <div class="cp"><label>Velocidade do cordao</label><input type="number" id="inVc" min="0.5" step="0.5"><span class="un">mm/s</span></div>
            <div class="nt">Mais devagar aquece e penetra mais. Vale so nos trechos com solda ligada.</div>
            <button class="b quente" id="btSoldar">Executar com arco</button>
            <div class="pq2" id="qSoldar"></div>
            <div class="pgr"><i id="pg"></i></div>
          </div>
        </div>

        <div class="et" id="eTraj">
          <div class="cab"><div class="mk">&#9209;</div>
            <div class="tx"><div class="tt">Trajetoria a mao livre</div>
            <span class="sb" id="sbTraj">nenhuma gravada</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt">Grava o caminho inteiro enquanto voce move o braco, com o
            estado do arco em cada instante. Serve para percurso organico; para
            cordao reto use os pontos acima, que saem em reta de verdade.</div>
            <button class="b" id="btGravIni">Iniciar gravacao</button>
            <div class="pq2" id="qGravIni"></div>
            <button class="b" id="btGravFim">Encerrar gravacao</button>
            <div class="pq2" id="qGravFim"></div>
            <h4>Arco durante a gravacao</h4>
            <div class="perigo">Este botao abre o arco de verdade, agora. E o
            que a gravacao registra em cada instante do percurso.</div>
            <button class="b quente" id="btArco">Abrir arco</button>
            <div class="pq2" id="qArco"></div>
            <h4>Reproduzir</h4>
            <div class="cp"><label>Velocidade da reproducao</label><input type="number" id="inEsc" min="10" max="200" step="5"><span class="un">%</span></div>
            <button class="b pri" id="btRepro">Reproduzir</button>
            <div class="pq2" id="qRepro"></div>
            <button class="b mini" id="btTrajLimpar">Apagar trajetoria</button>
            <div class="pq2" id="qTrajLimpar"></div>
          </div>
        </div>
      </section>

      <!-- ========================== ARQUIVOS ========================== -->
      <section class="pane" id="pnArq">
        <div class="et aberta">
          <div class="cab"><div class="mk">&#128190;</div>
            <div class="tx"><div class="tt">Cartao de memoria</div>
            <span class="sb" id="sbSd">procurando</span></div></div>
          <div class="dentro">
            <div class="sdBar" id="sdBar">
              <div class="pt"></div>
              <div class="tx"><b id="sdTit">--</b><br><span id="sdMsg">--</span></div>
            </div>
            <button class="b mini" id="btSdMontar">Procurar cartao de novo</button>

            <h4>Biblioteca</h4>
            <div class="seg" id="segTipo">
              <button data-t="prog" class="on">Programas</button>
              <button data-t="traj">Trajetorias</button>
              <button data-t="cfg">Ajustes</button>
            </div>
            <div class="linhaNome">
              <input id="sdNome" maxlength="24" placeholder="nome do arquivo" autocomplete="off">
              <button class="b mini" id="btSdSalvar" style="width:auto;margin:0">Salvar</button>
            </div>
            <div class="nt" id="sdDica">Salva o programa de pontos que esta na
            maquina agora. Letras, numeros, espaco, hifen e sublinhado.</div>
            <div id="sdLista"></div>
          </div>
        </div>

        <div class="et">
          <div class="cab"><div class="mk">&#8505;</div>
            <div class="tx"><div class="tt">Como o cartao e usado</div>
            <span class="sb">organizacao das pastas</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt">
            <b>/prog</b> &mdash; programas de solda, em texto e em graus. Da para
            escrever um no computador com um editor comum.<br><br>
            <b>/traj</b> &mdash; trajetorias gravadas a mao livre, em binario.<br><br>
            <b>/cfg</b> &mdash; copias dos ajustes da maquina. O NVS interno
            continua sendo o que a maquina usa ao ligar; o cartao serve de
            backup e para levar a mesma configuracao para outra maquina.<br><br>
            <b>/log</b> &mdash; um arquivo por partida, com alarme, emergencia,
            perda de conexao e inicio de cada execucao.
            </div>
            <div class="nt">Sem cartao no slot, tudo continua funcionando: o que
            se perde e a biblioteca e o registro.</div>
          </div>
        </div>
      </section>

      <!-- =========================== AJUSTES =========================== -->
      <section class="pane" id="pnAjuste">
        <div class="et aberta" id="e1" data-e="1">
          <div class="cab"><div class="mk">1</div>
            <div class="tx"><div class="tt">Preparar a maquina</div>
            <span class="sb" id="sb1">servos e calibracao</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <button class="b pri" id="btServos">Habilitar servos</button>
            <div class="nt">Sem servo habilitado o braco fica sem torque, o arco fica travado e o jog e recusado.</div>
            <h4>Medidas do braco</h4>
            <div class="cp"><label>Elo 1 · base ao cotovelo</label><input type="number" id="inL1" min="1"><span class="un">mm</span></div>
            <div class="cp"><label>Elo 2 · cotovelo a ponta</label><input type="number" id="inL2" min="1"><span class="un">mm</span></div>
            <div class="nt">Estas medidas mudam o desenho e a area util na mesma proporcao. Meca do centro de um eixo ao centro do outro.</div>
            <button class="b mini" id="btSalvarElos">Aplicar medidas</button>
            <h4>Curso das juntas</h4>
            <div class="nt">A calibracao mede ate onde cada junta pode ir. Sem ela o robo nao executa programa.</div>
            <button class="b" id="btCalib">Abrir assistente de calibracao</button>
            <h4>Bancada</h4>
            <button class="b mini" id="btTeste">Pulsar rele por 2 segundos</button>
          </div>
        </div>

        <div class="et" id="e5" data-e="5">
          <div class="cab"><div class="mk">&#9881;</div>
            <div class="tx"><div class="tt">Ajustes da maquina</div>
            <span class="sb">resolucao, velocidades, area util</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt">Os ajustes so sao aceitos com o robo parado no modo manual.</div>
            <h4>Resolucao da junta 1</h4>
            <div class="cp"><label>Pulsos por volta do motor</label><input type="number" id="inPv1" min="1"></div>
            <div class="cp"><label>Reducao mecanica</label><input type="number" id="inRd1" min="0.01" step="0.01"><span class="un">: 1</span></div>
            <h4>Resolucao da junta 2</h4>
            <div class="cp"><label>Pulsos por volta do motor</label><input type="number" id="inPv2" min="1"></div>
            <div class="cp"><label>Reducao mecanica</label><input type="number" id="inRd2" min="0.01" step="0.01"><span class="un">: 1</span></div>
            <div class="res" id="resumoRes">--</div>
            <div class="nt">Pulsos por volta e a engrenagem eletronica do T3D. Reducao e a relacao mecanica daquele eixo: <b>50</b> para um redutor 50:1. Os dois eixos sao independentes.</div>
            <h4>Velocidades</h4>
            <div class="cp"><label>Jog normal</label><input type="number" id="inVn" min="1"><span class="un">Hz</span></div>
            <div class="cp"><label>Jog precisao</label><input type="number" id="inVp" min="1"><span class="un">Hz</span></div>
            <div class="cp"><label>Deslocamento</label><input type="number" id="inVa" min="1"><span class="un">Hz</span></div>
            <div class="cp"><label>Cordao</label><input type="number" id="inVc2" min="0.5" step="0.5"><span class="un">mm/s</span></div>
            <div class="nt">O joystick usa a velocidade de jog como teto: no centro do disco o eixo fica parado, na borda ele anda nessa velocidade.</div>
            <h4>Rampa</h4>
            <div class="cp"><label>Junta 1</label><input type="number" id="inA1" min="100" step="100"></div>
            <div class="cp"><label>Junta 2</label><input type="number" id="inA2" min="100" step="100"></div>
            <button class="b pri" id="btSalvar">Salvar ajustes</button>
            <h4>Area util</h4>
            <div class="cp"><label>Folga de dobra</label><input type="number" id="inDb" min="0" max="90"><span class="un">°</span></div>
            <div class="cp"><label>Y minimo</label><input type="number" id="inEy"><span class="un">mm</span></div>
            <div class="cp"><label>Raio morto</label><input type="number" id="inEr" min="0"><span class="un">mm</span></div>
            <div class="nt">Dobra impede o elo 2 de fechar sobre o elo 1. Y minimo protege a mesa. Raio morto impede o antebraco de varrer sobre a base.</div>
            <button class="b pri" id="btSalvarGeo">Salvar area util</button>

            <h4>Protecoes ativas</h4>
            <div class="tr"><div class="ch" id="pCur"><i></i></div>
              <span>fim de curso das juntas</span></div>
            <div class="tr"><div class="ch" id="pDob"><i></i></div>
              <span>dobra do cotovelo</span></div>
            <div class="tr"><div class="ch" id="pEnv"><i></i></div>
              <span>mesa e base (Y minimo / raio morto)</span></div>
            <div class="nt">A protecao de mesa e base depende do comprimento dos elos estar correto. Ligue depois de conferir as medidas na etapa 1, senao ela recusa posicoes que sao validas.</div>
            <button class="b mini" id="btReset">Restaurar padroes</button>
          </div>
        </div>
      </section>
    </div></aside>
  </div>

  <nav class="abas" id="abas"></nav>
</div>

<div class="veu" id="veu"><div class="cx">
  <h2>Calibracao das juntas</h2>
  <div class="pp" id="cPasso">--</div>
  <div class="pgr"><i id="cBarra"></i></div>
  <div class="ins" id="cInstr"></div>
  <div class="eixo" id="cJ1">
    <button class="jb" data-j="1" data-d="-1">&#8592;</button>
    <div class="id"><span class="rot">junta 1</span></div>
    <button class="jb" data-j="1" data-d="1">&#8594;</button>
  </div>
  <div class="eixo" id="cJ2">
    <button class="jb" data-j="2" data-d="-1">&#8595;</button>
    <div class="id"><span class="rot">junta 2</span></div>
    <button class="jb" data-j="2" data-d="1">&#8593;</button>
  </div>
  <button class="b pri" id="cOk">Confirmar</button>
  <button class="b mini" id="cNao">Cancelar</button>
</div></div>

<script>
"use strict";
const $=function(i){return document.getElementById(i);};
const D={};
let erro="";

function post(u){
  return fetch(u,{method:"POST"}).then(function(r){
    if(!r.ok)return r.text().then(function(t){erro=t;throw new Error(t);});
    erro="";return r;
  }).catch(function(e){erro=e.message||"o robo nao respondeu";});
}
function ping(u){return fetch(u,{method:"POST"}).catch(function(){});}

/* ---------- jog com heartbeat ---------- */
const tm={};
function jogOn(j,d,el){
  if(tm[j])return;
  if(el)el.classList.add("press");
  ping("/api/jog?j="+j+"&d="+d);
  tm[j]=setInterval(function(){ping("/api/jog?j="+j+"&d="+d);},100);
}
function jogOff(j,el){
  if(tm[j]){clearInterval(tm[j]);delete tm[j];}
  if(el)el.classList.remove("press");
  ping("/api/jog?j="+j+"&d=0");
}
document.querySelectorAll(".jb").forEach(function(b){
  b.addEventListener("pointerdown",function(e){e.preventDefault();jogOn(b.dataset.j,b.dataset.d,b);});
  ["pointerup","pointerleave","pointercancel"].forEach(function(v){
    b.addEventListener(v,function(){jogOff(b.dataset.j,b);});});
});
const TK={ArrowLeft:["1","-1"],ArrowRight:["1","1"],ArrowUp:["2","1"],ArrowDown:["2","-1"]};
addEventListener("keydown",function(e){
  if(document.activeElement&&/INPUT/.test(document.activeElement.tagName))return;
  if(e.code==="Space"){e.preventDefault();post("/api/parar");return;}
  const m=TK[e.key];if(m&&!e.repeat){e.preventDefault();jogOn(m[0],m[1],null);}
});
addEventListener("keyup",function(e){const m=TK[e.key];if(m)jogOff(m[0],null);});
addEventListener("blur",function(){jogOff("1",null);jogOff("2",null);});

/* ---------- etapas ---------- */
/* Sanfona: SO nas secoes que tem a seta, e fechando apenas as do MESMO
   painel. Antes fechava todas as .et da pagina, entao abrir "Ensinar o
   caminho" na aba Programa fechava a secao do Mover -- e ao voltar para
   la o joystick, "Gravar ponto" e "Ir para o zero" tinham sumido, o que
   parecia botao que nao faz nada. A secao do joystick e a do cartao nao
   tem seta e nao recolhem: sao a superficie principal de cada aba. */
document.querySelectorAll(".cab").forEach(function(c){
  if(!c.querySelector(".chv"))return;
  c.addEventListener("click",function(){
    const et=c.parentElement,ja=et.classList.contains("aberta");
    const painel=et.closest(".pane")||document;
    painel.querySelectorAll(".et").forEach(function(x){
      if(x.querySelector(".cab .chv"))x.classList.remove("aberta");});
    if(!ja)et.classList.add("aberta");});
});
function abrir(n){
  /* As etapas moram em abas diferentes: abrir a etapa 2 sem trazer a aba
     junto deixaria o operador olhando para uma tela que nao mudou. */
  irAba(n<=1||n>=5?"ajuste":"prog");
  const e=$("e"+n);
  if(!e)return;
  const painel=e.closest(".pane")||document;
  painel.querySelectorAll(".et").forEach(function(x){
    if(x.querySelector(".cab .chv"))x.classList.remove("aberta");});
  e.classList.add("aberta");
}

/* ---------- acoes ---------- */
let carregou=false;
$("btParar").onclick =function(){post("/api/parar");};
$("btServos").onclick=function(){post("/api/servos?v="+(D.servos?0:1));};
$("btPrec").onclick  =function(){post("/api/precisao?v=-1");};
$("btTeste").onclick =function(){post("/api/teste/rele");};
$("btCalib").onclick =function(){post("/api/calib/iniciar");};
$("cOk").onclick     =function(){post("/api/calib/confirmar");};
$("cNao").onclick    =function(){post("/api/calib/cancelar");};
$("btGravar").onclick=function(){post("/api/ponto/gravar").then(lerPontos);};
$("btLimpar").onclick=function(){
  if(confirm("Apagar todos os pontos do programa?"))post("/api/prog/limpar").then(lerPontos);};
$("btEnsaio").onclick=function(){
  if(D.modo==="EXECUTANDO")post("/api/prog/parar");
  else post("/api/prog/executar?ensaio=1");};
$("btSoldar").onclick=function(){
  if(D.modo==="EXECUTANDO"){post("/api/prog/parar");return;}
  if(confirm("O ARCO VAI ABRIR.\n\nMascara, aterramento na peca e area livre conferidos?"))
    post("/api/prog/executar?ensaio=0");};

function geo(){
  return post("/api/geometria?l1="+$("inL1").value+"&l2="+$("inL2").value+
   "&dobra="+$("inDb").value+"&envY="+$("inEy").value+"&envR="+$("inEr").value)
   .then(function(){autoEnquadrar();});
}
$("btSalvarElos").onclick=geo;
$("btSalvarGeo").onclick =geo;

function salvar(vc){
  return post("/api/config?velN="+$("inVn").value+"&velP="+$("inVp").value+
    "&velA="+$("inVa").value+"&velCordao="+vc+"&acel1="+$("inA1").value+
    "&acel2="+$("inA2").value+"&ppv1="+$("inPv1").value+"&red1="+$("inRd1").value+
    "&ppv2="+$("inPv2").value+"&red2="+$("inRd2").value);
}
$("btSalvar").onclick=function(){salvar($("inVc2").value);};
$("inVc").onchange   =function(){salvar($("inVc").value);$("inVc2").value=$("inVc").value;};
function prot(){
  return post("/api/protecoes?curso="+(D.protCurso?1:0)+
    "&dobra="+(D.protDobra?1:0)+"&envelope="+(D.protEnv?1:0));
}
$("pCur").onclick=function(){D.protCurso=!D.protCurso;prot();};
$("pDob").onclick=function(){D.protDobra=!D.protDobra;prot();};
$("pEnv").onclick=function(){D.protEnv=!D.protEnv;prot();};

$("btReset").onclick =function(){
  /* carregou=false faz o proximo status repreencher os campos: sem isso o
     formulario continuava mostrando os valores antigos e o "Salvar"
     seguinte reaplicava tudo por cima do padrao de fabrica. */
  if(confirm("Restaurar todos os ajustes de fabrica?"))
    post("/api/config/reset").then(function(){carregou=false;});};

/* ---------- pontos ---------- */
let pontos=[];

/* Caminho gravado a mao livre. /api/trajetoria ja existia no firmware,
   reamostrado e convertido para XY, e nao tinha nenhum consumidor: a
   trajetoria era gravada e nunca aparecia no desenho. */
let traj=[],ultTrajN=-1;
function lerTraj(){
  return fetch("/api/trajetoria").then(function(r){return r.json();})
   .then(function(j){traj=j.pts||[];}).catch(function(){});
}

function lerPontos(){
  return fetch("/api/pontos").then(function(r){return r.json();})
   .then(function(j){pontos=j.pts||[];pintarLista();}).catch(function(){});
}
function pintarLista(){
  const cx=$("lista");
  if(!pontos.length){
    cx.innerHTML='<div class="nulo">Nenhum ponto ainda.<br>Mova o braco ate onde o cordao comeca e grave.</div>';
    return;}
  let h='<div class="lista">';
  pontos.forEach(function(p,i){
    const ag=(D.modo==="EXECUTANDO"&&D.progIdx===i)?" agora":"";
    h+='<div class="p'+ag+'"><div class="n">'+(i+1)+'</div>'+
       '<div class="c"><em>X'+p.x+' Y'+p.y+'</em> · '+p.t1.toFixed(0)+'°/'+p.t2.toFixed(0)+'°</div>'+
       '<button class="mb" data-ir="'+i+'">ir</button>'+
       '<button class="mb x" data-del="'+i+'">apagar</button></div>';
    if(i<pontos.length-1){
      const d=Math.round(Math.hypot(pontos[i+1].x-p.x,pontos[i+1].y-p.y));
      h+='<div class="tr'+(p.s?" q":"")+(p.av?" ruim":"")+'">'+
         '<div class="ch'+(p.s?" on":"")+'" data-sw="'+i+'"><i></i></div>'+
         '<span>'+(i+1)+'&rarr;'+(i+2)+' · '+d+' mm · '+
         (p.s?"CORDAO EM RETA":"apenas desloca")+'</span></div>';
      /* O trecho e conferido enquanto o operador ensina: descobrir que o
         cordao nao passa so na hora de apertar Executar e tarde. */
      if(p.av)h+='<div class="avTr">'+p.av+'</div>';}
  });
  cx.innerHTML=h+'</div>';
  cx.querySelectorAll("[data-sw]").forEach(function(e){e.onclick=function(){
    const i=+e.dataset.sw;post("/api/ponto/solda?i="+i+"&v="+(pontos[i].s?0:1)).then(lerPontos);};});
  cx.querySelectorAll("[data-del]").forEach(function(e){e.onclick=function(){
    post("/api/ponto/remover?i="+e.dataset.del).then(lerPontos);};});
  cx.querySelectorAll("[data-ir]").forEach(function(e){e.onclick=function(){
    post("/api/ponto/ir?i="+e.dataset.ir);};});
}

/* =====================================================================
   Mesa de tracado.
   A vista tem largura FIXA em milimetros (vistaMm). Assim o desenho e
   proporcional ao braco de verdade: encurtar um elo encurta o desenho,
   nao so a area util. O botao FIT reenquadra quando voce quiser.
   ===================================================================== */
const cv=$("cv"),ct=cv.getContext("2d");
const TAU=Math.PI*2;   /* usar 7 aqui sobra 0,72 rad e cria cunha no preenchimento */
let vistaMm=800, esc=1, ox=0, oy=0, jaEnquadrou=false;

function medir(){
  const d=window.devicePixelRatio||1,r=cv.parentElement.getBoundingClientRect();
  if(r.width<2||r.height<2)return;   /* aba escondida: mede depois */
  cv.width=Math.round(r.width*d);cv.height=Math.round(r.height*d);
  ct.setTransform(d,0,0,d,0,0);
}
addEventListener("resize",medir);
function autoEnquadrar(){
  /* A base fica em 56% da altura, entao sobram 56% para baixo e 44%
     para cima. O alcance precisa caber no menor dos dois: 0,44 da
     vista. Com folga: vista = alcance / 0,40. */
  const alc=(D.l1||200)+(D.l2||200);
  vistaMm=Math.max(60,Math.round(alc/0.40));
}
$("zMais").onclick =function(){vistaMm=Math.max(80,Math.round(vistaMm/1.25));};
$("zMenos").onclick=function(){vistaMm=Math.min(4000,Math.round(vistaMm*1.25));};
$("zAuto").onclick =autoEnquadrar;
$("zTema").onclick =function(){
  const e=document.documentElement.getAttribute("data-tema")==="escuro";
  document.documentElement.setAttribute("data-tema",e?"claro":"escuro");
};

function ponta(t1,t2,L1,L2){
  const a=t1*Math.PI/180,b=(t1+t2)*Math.PI/180;
  return [L1*Math.cos(a)+L2*Math.cos(b),L1*Math.sin(a)+L2*Math.sin(b)];
}

function cor(n){
  return getComputedStyle(document.documentElement).getPropertyValue(n).trim();
}
let PAL={}, palQuando="";
function paleta(){
  const t=document.documentElement.getAttribute("data-tema")||"claro";
  if(t===palQuando)return PAL;
  palQuando=t;
  PAL={papel:cor("--papel"),grade:cor("--grade"),arco:cor("--arco"),
       quente:cor("--quente"),brasa:cor("--brasa"),letra:cor("--letra"),
       letra2:cor("--letra2"),
       elo1:t==="escuro"?"#8b98a9":"#5a6675",
       elo2:t==="escuro"?"#b7c2d1":"#8794a5",
       juntaF:t==="escuro"?"#2a333e":"#ffffff",
       ponto:t==="escuro"?"#141920":"#ffffff"};
  return PAL;
}

function pintar(){
  /* Na aba errada o canvas tem largura zero: desenhar ali so gasta CPU
     e divide por zero na escala. */
  if(!cv.width||!cv.height)return;
  const C=paleta();
  const L1=D.l1||200,L2=D.l2||200,dp=window.devicePixelRatio||1;
  const w=cv.width/dp,h=cv.height/dp,alc=L1+L2;
  /* A escala respeita a MENOR das duas dimensoes. Calcular so pela
     largura cortava o braco em tela larga de computador. */
  esc=Math.min(w,h)/vistaMm;
  ox=w/2;oy=h*0.56;
  const P=function(x,y){return [ox+x*esc,oy-y*esc];};

  ct.clearRect(0,0,w,h);
  ct.fillStyle=C.papel;ct.fillRect(0,0,w,h);

  /* grade metrica: o passo se adapta ao zoom, sempre em mm redondos */
  let passo=10;
  const alvo=52;
  while(passo*esc<alvo)passo*= (String(passo)[0]==="1")?2.5:2;
  while(passo*esc>alvo*2.6)passo/= (String(passo)[0]==="2")?2.5:2;
  passo=Math.max(1,Math.round(passo));
  ct.lineWidth=1;
  const nx=Math.ceil((w/2)/esc/passo),ny=Math.ceil(h/esc/passo);
  for(let i=-nx;i<=nx;i++){
    const X=P(i*passo,0)[0];
    ct.strokeStyle="rgba("+C.grade+(i===0?",.45)":",.13)");
    ct.beginPath();ct.moveTo(X,0);ct.lineTo(X,h);ct.stroke();}
  for(let i=-ny;i<=ny;i++){
    const Y=P(0,i*passo)[1];
    ct.strokeStyle="rgba("+C.grade+(i===0?",.45)":",.13)");
    ct.beginPath();ct.moveTo(0,Y);ct.lineTo(w,Y);ct.stroke();}

  /* area util */
  ct.beginPath();ct.arc(ox,oy,alc*esc,0,TAU);
  ct.arc(ox,oy,Math.abs(L1-L2)*esc,0,TAU,true);
  ct.fillStyle="rgba("+C.grade+",.045)";ct.fill();
  ct.strokeStyle="rgba("+C.grade+",.5)";ct.lineWidth=1;
  ct.setLineDash([4,6]);ct.stroke();ct.setLineDash([]);

  /* As zonas proibidas so aparecem quando a protecao correspondente
     esta ligada. Desenhar limite que nao e aplicado engana o operador. */
  if(D.protEnv&&D.envY!==undefined){
    const yl=P(0,D.envY)[1];
    ct.fillStyle="rgba(185,28,28,.09)";ct.fillRect(0,yl,w,h-yl);
    ct.strokeStyle="rgba(185,28,28,.5)";
    ct.beginPath();ct.moveTo(0,yl);ct.lineTo(w,yl);ct.stroke();}
  if(D.protEnv&&D.envR){ct.beginPath();ct.arc(ox,oy,D.envR*esc,0,TAU);
    ct.fillStyle="rgba(185,28,28,.13)";ct.fill();}

  /* Caminho gravado a mao livre, por baixo dos pontos do programa:
     laranja onde o arco estava aberto, cinza onde era so deslocamento. */
  if(traj.length>1){
    ct.lineCap="round";ct.lineJoin="round";
    for(let i=0;i<traj.length-1;i++){
      const A=traj[i],B=traj[i+1],quente=A[2]===1;
      ct.strokeStyle=quente?C.quente:C.letra2;
      ct.globalAlpha=quente?.85:.4;
      ct.lineWidth=quente?3:1.5;
      ct.beginPath();
      ct.moveTo(P(A[0],A[1])[0],P(A[0],A[1])[1]);
      ct.lineTo(P(B[0],B[1])[0],P(B[0],B[1])[1]);
      ct.stroke();
    }
    ct.globalAlpha=1;
  }

  /* trechos */
  ct.lineCap="round";
  for(let i=0;i<pontos.length-1;i++){
    const A=pontos[i],B=pontos[i+1];
    if(A.av){
      /* Trecho que o robo nao consegue percorrer: vermelho tracejado, para
         nao se confundir com o cordao que vai sair. */
      const a=P(A.x,A.y),b=P(B.x,B.y);
      ct.strokeStyle=C.brasa;ct.lineWidth=2.5;ct.setLineDash([7,5]);
      ct.beginPath();ct.moveTo(a[0],a[1]);ct.lineTo(b[0],b[1]);ct.stroke();
      ct.setLineDash([]);
      continue;
    }
    if(A.s){
      const a=P(A.x,A.y),b=P(B.x,B.y);
      ct.save();ct.shadowColor=C.quente;ct.shadowBlur=13;
      ct.strokeStyle=C.quente;ct.lineWidth=Math.max(3,5*Math.min(1,esc*2.2));
      ct.beginPath();ct.moveTo(a[0],a[1]);ct.lineTo(b[0],b[1]);ct.stroke();ct.restore();
    }else{
      ct.strokeStyle=C.letra2;ct.globalAlpha=.55;ct.lineWidth=1.5;ct.setLineDash([5,5]);
      ct.beginPath();
      for(let k=0;k<=24;k++){
        const u=k/24;
        const q=ponta(A.t1+(B.t1-A.t1)*u,A.t2+(B.t2-A.t2)*u,L1,L2);
        const s=P(q[0],q[1]);
        if(k)ct.lineTo(s[0],s[1]);else ct.moveTo(s[0],s[1]);}
      ct.stroke();ct.setLineDash([]);ct.globalAlpha=1;}
  }
  const rp=Math.max(6,Math.min(12,alc*esc*0.035));
  pontos.forEach(function(p,i){
    const a=P(p.x,p.y),ag=(D.modo==="EXECUTANDO"&&D.progIdx===i);
    ct.beginPath();ct.arc(a[0],a[1],rp,0,TAU);
    ct.fillStyle=ag?C.quente:C.ponto;ct.fill();
    ct.strokeStyle=ag?C.quente:C.arco;ct.lineWidth=2;ct.stroke();
    if(rp>=8){
      ct.fillStyle=ag?"#ffffff":C.letra;
      ct.font="600 "+Math.round(rp*.95)+"px ui-monospace,Menlo,monospace";
      ct.textAlign="center";ct.textBaseline="middle";
      ct.fillText(String(i+1),a[0],a[1]);}
  });

  /* braco: espessura proporcional ao comprimento de cada elo */
  const t1=(D.t1||0)*Math.PI/180,t2=((D.t1||0)+(D.t2||0))*Math.PI/180;
  const c=P(L1*Math.cos(t1),L1*Math.sin(t1));
  const p=P(L1*Math.cos(t1)+L2*Math.cos(t2),L1*Math.sin(t1)+L2*Math.sin(t2));
  const e1=Math.max(4,L1*esc*0.085), e2=Math.max(3,L2*esc*0.068);

  ct.save();ct.shadowColor=cor("--sombra");ct.shadowBlur=8;ct.shadowOffsetY=3;
  ct.strokeStyle=C.elo1;ct.lineWidth=e1;
  ct.beginPath();ct.moveTo(ox,oy);ct.lineTo(c[0],c[1]);ct.stroke();
  ct.strokeStyle=C.elo2;ct.lineWidth=e2;
  ct.beginPath();ct.moveTo(c[0],c[1]);ct.lineTo(p[0],p[1]);ct.stroke();
  ct.restore();
  /* nervura clara ao longo dos elos */
  ct.strokeStyle="rgba(255,255,255,.22)";ct.lineWidth=Math.max(1,e1*.18);
  ct.beginPath();ct.moveTo(ox,oy);ct.lineTo(c[0],c[1]);ct.stroke();

  ct.fillStyle=C.juntaF;ct.beginPath();ct.arc(ox,oy,e1*.72,0,TAU);ct.fill();
  ct.strokeStyle=C.arco;ct.lineWidth=2;ct.stroke();
  ct.fillStyle=C.juntaF;ct.beginPath();ct.arc(c[0],c[1],e2*.72,0,TAU);ct.fill();
  ct.strokeStyle=C.elo1;ct.lineWidth=1.5;ct.stroke();

  /* rastro proporcional a velocidade da ponta */
  const vv=D.vPonta||0;
  if(vv>0.5){
    const dir=Math.atan2(p[1]-c[1],p[0]-c[0])+Math.PI/2;
    const cauda=Math.min(46,vv*esc*0.5+6);
    ct.strokeStyle=D.solda?C.quente:C.arco;ct.globalAlpha=.4;
    ct.lineWidth=3;ct.beginPath();
    ct.moveTo(p[0],p[1]);
    ct.lineTo(p[0]-Math.cos(dir)*cauda,p[1]-Math.sin(dir)*cauda);ct.stroke();ct.globalAlpha=1;}

  ct.save();
  if(D.solda){
    ct.shadowColor=C.quente;
    ct.shadowBlur=16+7*Math.sin(Date.now()/90);
    ct.fillStyle=C.quente;
  }else{ct.fillStyle=C.arco;}
  ct.beginPath();ct.arc(p[0],p[1],Math.max(4,e2*.6),0,TAU);ct.fill();
  ct.restore();

  if(!D.protEnv){
    ct.fillStyle=C.letra2;ct.globalAlpha=.75;
    ct.font="10px ui-monospace,Menlo,monospace";ct.textAlign="left";
    ct.fillText("protecao de mesa e base desligada",12,h-14);
    ct.globalAlpha=1;
  }

  /* barra de escala: a prova visual de que o desenho esta em mm reais */
  const larg=passo*esc;
  const bx=w-larg-18, by=h-18;
  ct.strokeStyle=C.letra2;ct.lineWidth=1.5;
  ct.beginPath();
  ct.moveTo(bx,by-5);ct.lineTo(bx,by);ct.lineTo(bx+larg,by);ct.lineTo(bx+larg,by-5);
  ct.stroke();
  ct.fillStyle=C.letra2;
  ct.font="10px ui-monospace,Menlo,monospace";ct.textAlign="center";
  ct.fillText(passo+" mm",bx+larg/2,by-9);
}
cv.addEventListener("click",function(e){
  const r=cv.getBoundingClientRect();
  const x=(e.clientX-r.left-ox)/esc,y=(oy-(e.clientY-r.top))/esc;
  post("/api/mover_xy?x="+x.toFixed(1)+"&y="+y.toFixed(1));
});

/* ---------- status ---------- */
const RM={MANUAL:"manual",GRAVANDO:"gravando",REPRODUZINDO:"repetindo",
 EXECUTANDO:"executa",POSICIONANDO:"movendo",CALIBRANDO:"calibra",FALHA:"falha"};
const PC={HOME:[1,"Leve o braco ate a posicao de referencia (o zero da maquina) e confirme."],
 J1_NEG:[2,"Mova a junta 1 ate o limite fisico negativo e confirme."],
 J1_VOLTA_NEG:[2,"Aguarde: a junta 1 volta ao zero."],
 J1_POS:[3,"Mova a junta 1 ate o limite fisico positivo e confirme."],
 J1_VOLTA_POS:[3,"Aguarde: a junta 1 volta ao zero."],
 J2_NEG:[4,"Mova a junta 2 ate o limite fisico negativo e confirme."],
 J2_VOLTA_NEG:[4,"Aguarde: a junta 2 volta ao zero."],
 J2_POS:[5,"Mova a junta 2 ate o limite fisico positivo e confirme."],
 J2_VOLTA_POS:[5,"Aguarde: a junta 2 volta ao zero."],
 CONCLUIDO:[6,"Curso medido. Confira os limites em graus: se nao baterem com a maquina real, o erro esta na resolucao daquele eixo."]};

let quedas=0,ultN=-1,ultCal="";
/* Um botao fora de acao tem que dizer por que. Desabilitar em silencio e
   o que faz o operador achar que o sistema esta quebrado. */
function acao(id,motivo){
  const b=$("bt"+id), q=$("q"+id);
  if(b)b.disabled=!!motivo;
  if(q){q.textContent=motivo||"";q.style.display=motivo?"block":"none";}
}
/* Motivo comum a tudo que move o braco, na ordem em que o operador
   precisa resolver. */
function porQueNaoMove(d){
  if(d.modo==="FALHA")            return "sistema em falha: rearme os servos primeiro";
  if(!d.servos)                   return "habilite os servos (aba Ajustes, etapa 1)";
  if(!d.cal1||!d.cal2)            return "calibre as juntas (aba Ajustes, etapa 1)";
  if(d.modo!=="MANUAL")           return "robo ocupado: "+(RM[d.modo]||d.modo);
  return "";
}

function lamp(el,cls,txt){
  el.className="lp"+(cls?" "+cls:"");
  if(txt!==undefined)$("lModoT").textContent=txt;}

function aplicar(d){
  Object.assign(D,d);
  if(!jaEnquadrou){jaEnquadrou=true;autoEnquadrar();}
  const pronto=d.cal1&&d.cal2&&d.servos;
  const rodando=(d.modo==="EXECUTANDO");
  const movendo=d.movendo;

  lamp($("lModo"),d.modo==="FALHA"?"er":(d.modo==="MANUAL"?"on":"at"),RM[d.modo]||d.modo);
  lamp($("lServo"),d.servos?"on":"");
  lamp($("lArco"),d.solda?"hot":"");
  lamp($("lRede"),"on");

  $("hT1").textContent=d.t1.toFixed(1)+"°";
  $("hT2").textContent=d.t2.toFixed(1)+"°";
  $("hX").textContent=d.x.toFixed(0);
  $("hY").textContent=d.y.toFixed(0);
  $("hV").textContent=d.vPonta.toFixed(1);
  $("hV").className=d.solda?"hot":(movendo?"mv":"");
  $("hT1").className=(d.v1>1)?"mv":"";
  $("hT2").className=(d.v2>1)?"mv":"";

  $("fx1").textContent=d.cal1?(d.j1min.toFixed(0)+"…"+d.j1max.toFixed(0)+"°"):"sem curso";
  $("fx2").textContent=d.cal2?(d.j2min.toFixed(0)+"…"+d.j2max.toFixed(0)+"°"):"sem curso";

  const t=$("tira");
  t.textContent=erro?("Recusado: "+erro):d.msg;
  t.className="tira"+(erro||d.modo==="FALHA"?" er":(pronto?" ok":""));

  $("btServos").textContent=d.servos?"Desabilitar servos":"Habilitar servos";
  $("btServos").className="b "+(d.servos?"":"pri");
  $("e1").classList.toggle("feita",pronto);
  $("sb1").textContent=pronto?("elos "+d.l1.toFixed(0)+"+"+d.l2.toFixed(0)+" mm · calibrado")
    :(d.servos?"falta calibrar":"servos desligados");

  const nq=pontos.filter(function(p,i){return i<pontos.length-1&&p.s;}).length;
  const nRuim=pontos.filter(function(p,i){return i<pontos.length-1&&p.av;}).length;
  $("e2").classList.toggle("feita",d.progN>=2&&!nRuim);
  $("sb2").textContent=d.progN===0?"nenhum ponto":
    (d.progN+" pontos · "+nq+" cordao(oes)"+(nRuim?" · "+nRuim+" trecho(s) com problema":""));
  acao("Gravar", d.modo==="FALHA" ? "sistema em falha: rearme os servos primeiro"
       : d.modo!=="MANUAL" ? "grave pontos com o robo parado: "+(RM[d.modo]||d.modo) : "");
  $("btPrec").textContent="Precisao: "+(d.precisao?"ligada":"desligada");

  $("btEnsaio").textContent=(rodando&&d.ensaio)?"Parar ensaio":"Executar ensaio";
  $("btEnsaio").className="b "+((rodando&&d.ensaio)?"rod":"pri");
  acao("Ensaio", (rodando&&d.ensaio) ? ""
       : (rodando&&!d.ensaio) ? "execucao com arco em andamento"
       : (d.progN<2) ? "grave pelo menos 2 pontos na aba Mover"
       : porQueNaoMove(d));
  $("e3").classList.toggle("feita",d.progN>=2&&!rodando);
  $("btSoldar").textContent=(rodando&&!d.ensaio)?"PARAR":"Executar com arco";
  $("btSoldar").className="b "+((rodando&&!d.ensaio)?"rod":"quente");
  acao("Soldar", (rodando&&!d.ensaio) ? ""
       : (rodando&&d.ensaio) ? "ensaio em andamento"
       : (d.progN<2) ? "grave pelo menos 2 pontos na aba Mover"
       : porQueNaoMove(d));
  $("pg").style.width=(rodando?d.progPct:0)+"%";

  const passo=!pronto?1:(d.progN<2?2:(rodando&&!d.ensaio?4:3));
  document.querySelectorAll(".et").forEach(function(x){
    x.classList.toggle("agora",+x.dataset.e===passo);});

  $("pCur").className="ch"+(d.protCurso?" on":"");
  $("pDob").className="ch"+(d.protDobra?" on":"");
  $("pEnv").className="ch"+(d.protEnv?" on":"");

  $("resumoRes").textContent=
    "J1 · "+d.ppg1.toFixed(2)+" pulsos por grau"+
    "\nJ2 · "+d.ppg2.toFixed(2)+" pulsos por grau";

  if(!carregou){
    carregou=true;
    $("inVn").value=d.velN;$("inVp").value=d.velP;$("inVa").value=d.velA;
    $("inVc").value=d.velCordao;$("inVc2").value=d.velCordao;
    $("inA1").value=d.acel1;$("inA2").value=d.acel2;
    $("inPv1").value=d.ppv1;$("inRd1").value=d.red1;
    $("inPv2").value=d.ppv2;$("inRd2").value=d.red2;
    $("inL1").value=d.l1;$("inL2").value=d.l2;$("inDb").value=d.dobra;
    $("inEy").value=d.envY;$("inEr").value=d.envR;
    $("inEsc").value=d.escala;
    $("inMt1").value=d.t1.toFixed(1);$("inMt2").value=d.t2.toFixed(1);
  }

  const veu=$("veu");
  if(d.calib==="INATIVO"){
    veu.classList.remove("on");
    if(ultCal&&ultCal!=="INATIVO")abrir(d.progN<2?2:3);
  }else{
    veu.classList.add("on");
    const p=PC[d.calib]||[0,""];
    $("cPasso").textContent="PASSO "+p[0]+" DE 6";
    $("cInstr").textContent=p[1];
    $("cBarra").style.width=(p[0]/6*100)+"%";
    $("cJ1").style.display=(d.calibEixo===1||d.calibEixo===0)?"grid":"none";
    $("cJ2").style.display=(d.calibEixo===2||d.calibEixo===0)?"grid":"none";
    $("cOk").disabled=/VOLTA/.test(d.calib);
    $("cOk").textContent=d.calib==="CONCLUIDO"?"Concluir e salvar":"Confirmar";
  }
  ultCal=d.calib;

  $("sbTraj").textContent=d.trajN<2?"nenhuma gravada":
    (d.trajN+" pontos · "+(d.trajMs/1000).toFixed(1)+" s");
  acao("GravIni", d.modo==="GRAVANDO" ? "ja esta gravando" : porQueNaoMove(d));
  acao("GravFim", d.modo==="GRAVANDO" ? "" : "nao ha gravacao em andamento");
  acao("Repro", (d.trajN<2) ? "nenhuma trajetoria gravada" : porQueNaoMove(d));
  $("btArco").textContent=d.solda?"FECHAR ARCO":"Abrir arco";
  $("btArco").className="b "+(d.solda?"rod":"quente");
  acao("Arco", d.solda ? ""
       : (d.modo!=="MANUAL"&&d.modo!=="GRAVANDO") ? "arco manual so no modo manual ou gravando"
       : !d.servos ? "habilite os servos (aba Ajustes, etapa 1)" : "");
  acao("Mover", porQueNaoMove(d));
  acao("TrajLimpar", (d.trajN<2) ? "nao ha trajetoria para apagar"
       : (d.modo!=="MANUAL") ? "so com o robo parado no modo manual" : "");
  const bloqJog=porQueNaoMove(d);
  joy.classList.toggle("bloq",!!bloqJog);
  $("joyMotivo").textContent=bloqJog;
  $("sbMover").textContent=bloqJog||
    (d.precisao?"precisao · joystick":"joystick das duas juntas");
  acao("Home", porQueNaoMove(d));

  if(d.trajN!==ultTrajN){ultTrajN=d.trajN;
    if(d.modo!=="GRAVANDO")lerTraj();else traj=[];}

  if(d.progN!==ultN){ultN=d.progN;lerPontos();}
  else if(rodando)pintarLista();
}

function tick(){
  /* O cartao so e consultado quando a aba de arquivos esta aberta: o
     WebServer atende uma conexao por vez e cada requisicao a mais
     concorre com o heartbeat do jog. */
  if(abaAtual==="arq")sdAtualizar(false);
  fetch("/api/status").then(function(r){return r.json();}).then(function(d){
    quedas=0;aplicar(d);
  }).catch(function(){
    quedas++;
    if(quedas>=2){
      lamp($("lRede"),"er");
      $("tira").textContent="Sem comunicacao com o robo. Movimento e arco foram cortados por seguranca.";
      $("tira").className="tira er";}
  });
}


/* =====================================================================
   NAVEGACAO POR ABAS
   No celular cada aba e uma tela; no computador a mesa de tracado fica
   sempre visivel e a aba comanda so a coluna da direita.
   ===================================================================== */
const ABAS=[
 ["mesa","Mesa","M4 19h16M6 19V9l6-5 6 5v10"],
 ["mover","Mover","M12 4v16M4 12h16M12 4l-3 3M12 4l3 3M12 20l-3-3M12 20l3-3M4 12l3-3M4 12l3 3M20 12l-3-3M20 12l-3 3"],
 ["prog","Programa","M5 6h14M5 12h9M5 18h5M17 15l2 2 3-4"],
 ["arq","Arquivos","M4 7a2 2 0 012-2h4l2 2h6a2 2 0 012 2v8a2 2 0 01-2 2H6a2 2 0 01-2-2z"],
 ["ajuste","Ajustes","M12 15a3 3 0 100-6 3 3 0 000 6zM19.4 15a1.7 1.7 0 00.3 1.9l.1.1a2 2 0 11-2.8 2.8l-.1-.1a1.7 1.7 0 00-2.9 1.2 2 2 0 11-4 0 1.7 1.7 0 00-2.9-1.2l-.1.1a2 2 0 11-2.8-2.8l.1-.1A1.7 1.7 0 003 15a2 2 0 110-4 1.7 1.7 0 001.2-2.9l-.1-.1a2 2 0 112.8-2.8l.1.1A1.7 1.7 0 0010 4.6a2 2 0 114 0 1.7 1.7 0 002.9 1.2l.1-.1a2 2 0 112.8 2.8l-.1.1A1.7 1.7 0 0021 11a2 2 0 110 4 1.7 1.7 0 00-1.6 0z"]
];
const PANES={mover:"pnMover",prog:"pnProg",arq:"pnArq",ajuste:"pnAjuste"};

(function montarAbas(){
  let h="",t="";
  ABAS.forEach(function(a){
    h+='<button data-aba="'+a[0]+'"><svg viewBox="0 0 24 24"><path d="'+a[2]+'"/></svg>'+
       '<span>'+a[1]+'</span></button>';
    t+='<button data-aba="'+a[0]+'">'+a[1]+'</button>';
  });
  $("abas").innerHTML=h;
  $("abasTopo").innerHTML=t;
  document.querySelectorAll("[data-aba]").forEach(function(b){
    b.addEventListener("click",function(){irAba(b.dataset.aba);});
  });
})();

let abaAtual="";
function irAba(nome){
  if(!PANES[nome]&&nome!=="mesa")nome="mover";
  abaAtual=nome;
  document.body.dataset.aba=nome;
  for(const k in PANES)$(PANES[k]).classList.toggle("on",k===nome);
  /* Na "mesa" nenhum painel aparece no celular, mas no computador a
     coluna precisa mostrar alguma coisa. */
  if(nome==="mesa"&&innerWidth>1020)$(PANES.mover).classList.add("on");
  document.querySelectorAll("[data-aba]").forEach(function(b){
    b.classList.toggle("on",b.dataset.aba===nome);});
  if(nome==="mesa")medir();
  if(nome==="arq")sdAtualizar(true);
  try{localStorage.setItem("aba",nome);}catch(e){}
}

/* =====================================================================
   JOYSTICK
   Manda um comando so para os dois eixos, a 10 Hz. O firmware exige
   confirmacao a cada 350 ms e aplica a mesma zona morta: se o navegador
   travar ou a tela apagar, o eixo para sozinho.
   ===================================================================== */
const joy=$("joy"),knob=$("joyKnob");
let joyAtivo=false,joyId=null,joyA=0,joyB=0,joyTimer=null,joyVibrou=false;
const ZONA=0.12;

function joyDesenhar(){
  const r=joy.getBoundingClientRect(),raio=r.width/2;
  /* left/top em % contam a partir do DISCO; um translate em % contaria a
     partir do proprio botao, que tem 19% do tamanho -- o botao andaria
     um quinto do que deveria. */
  knob.style.left=(50+joyA*40.5)+"%";
  knob.style.top =(50-joyB*40.5)+"%";
  $("joyA").textContent=Math.round(joyA*100)+"%";
  $("joyB").textContent=Math.round(joyB*100)+"%";
  const mag=Math.max(Math.abs(joyA),Math.abs(joyB));
  $("joyTx").textContent=mag<ZONA?"jog":Math.round(mag*100)+"";
  void raio;
}
function joyEnviar(){
  ping("/api/jogxy?a="+joyA.toFixed(3)+"&b="+joyB.toFixed(3));
}
function joyDe(e){
  const r=joy.getBoundingClientRect();
  const cx=r.left+r.width/2, cy=r.top+r.height/2, raio=r.width/2;
  let x=(e.clientX-cx)/raio, y=(cy-e.clientY)/raio;
  const m=Math.hypot(x,y);
  if(m>1){x/=m;y/=m;}
  joyA=x;joyB=y;
  const mag=Math.max(Math.abs(x),Math.abs(y));
  if(mag>=ZONA&&!joyVibrou){joyVibrou=true;try{navigator.vibrate(8);}catch(e2){}}
  if(mag<ZONA)joyVibrou=false;
  joyDesenhar();
}
function joyComecar(e){
  if(joyAtivo)return;
  joyAtivo=true;joyId=e.pointerId;
  joy.classList.add("ativo");
  try{joy.setPointerCapture(e.pointerId);}catch(e2){}
  joyDe(e);joyEnviar();
  joyTimer=setInterval(joyEnviar,100);
}
function joyParar(){
  if(!joyAtivo)return;
  joyAtivo=false;joyId=null;joyVibrou=false;
  joy.classList.remove("ativo");
  clearInterval(joyTimer);joyTimer=null;
  joyA=0;joyB=0;joyDesenhar();
  /* Manda o zero duas vezes: se a primeira se perder, a segunda para o
     eixo antes de o heartbeat de 350 ms expirar. */
  joyEnviar();setTimeout(joyEnviar,60);
}
joy.addEventListener("pointerdown",function(e){e.preventDefault();joyComecar(e);});
joy.addEventListener("pointermove",function(e){
  if(joyAtivo&&e.pointerId===joyId){e.preventDefault();joyDe(e);}});
/* Nada de "pointerleave" aqui: com setPointerCapture o disco continua
   recebendo os eventos fora dos proprios limites, e arrastar o polegar
   alem da borda so satura em 1 -- nao e para parar o jog. Com
   pointerleave na lista, levar o dedo ate o canto derrubaria o comando
   justamente na velocidade maxima. */
["pointerup","pointercancel","lostpointercapture"].forEach(function(v){
  joy.addEventListener(v,function(e){
    if(joyAtivo&&(e.pointerId===undefined||e.pointerId===joyId))joyParar();});});
/* Tela apagou, app foi para segundo plano ou a aba perdeu o foco: para.
   visibilitychange nasce em document; escutar em window depende do
   evento borbulhar, o que nem sempre acontece. */
document.addEventListener("visibilitychange",function(){
  if(document.hidden)joyParar();});
addEventListener("pagehide",joyParar);
addEventListener("blur",joyParar);
joyDesenhar();

/* =====================================================================
   CARTAO SD
   ===================================================================== */
let sdTipo="prog",sdSeq=-1,sdEstado="",sdArqs=[];
const DICA={
 prog:"Salva o programa de pontos que esta na maquina agora. Letras, numeros, espaco, hifen e sublinhado.",
 traj:"Salva a trajetoria gravada a mao livre que esta na memoria.",
 cfg:"Salva uma copia dos ajustes. A maquina continua usando a memoria interna ao ligar; isto e backup."
};
document.querySelectorAll("#segTipo button").forEach(function(b){
  b.onclick=function(){
    sdTipo=b.dataset.t;
    document.querySelectorAll("#segTipo button").forEach(function(x){
      x.classList.toggle("on",x===b);});
    $("sdDica").textContent=DICA[sdTipo];
    sdSeq=-1;sdAtualizar(true);
  };
});
$("btSdMontar").onclick=function(){post("/api/sd/montar").then(function(){sdSeq=-1;});};
$("btSdSalvar").onclick=function(){
  const n=$("sdNome").value.trim();
  if(!n){erro="informe um nome para o arquivo";return;}
  post("/api/sd/salvar?tipo="+sdTipo+"&nome="+encodeURIComponent(n))
   .then(function(){sdSeq=-1;});
};

function sdPintar(){
  const cx=$("sdLista");
  if(!sdArqs.length){
    cx.innerHTML='<div class="nulo">Nenhum arquivo nesta pasta.</div>';return;}
  let h='<div class="lista arqs">';
  sdArqs.forEach(function(a){
    h+='<div class="arq"><div class="nm">'+a.n+'</div>'+
       '<div class="kb">'+(a.b<1024?a.b+" B":(a.b/1024).toFixed(1)+" kB")+'</div>'+
       '<button class="mb" data-car="'+a.n+'">carregar</button>'+
       '<button class="mb x" data-apg="'+a.n+'">apagar</button></div>';
  });
  cx.innerHTML=h+'</div>';
  cx.querySelectorAll("[data-car]").forEach(function(e){e.onclick=function(){
    post("/api/sd/carregar?tipo="+sdTipo+"&nome="+encodeURIComponent(e.dataset.car))
     .then(function(){sdSeq=-1;});};});
  cx.querySelectorAll("[data-apg]").forEach(function(e){e.onclick=function(){
    if(!confirm('Apagar "'+e.dataset.apg+'" do cartao?'))return;
    post("/api/sd/apagar?tipo="+sdTipo+"&nome="+encodeURIComponent(e.dataset.apg))
     .then(function(){sdSeq=-1;});};});
}

function sdAtualizar(forcar){
  return fetch("/api/sd").then(function(r){return r.json();}).then(function(d){
    sdEstado=d.estado;
    const b=$("sdBar");
    b.className="sdBar"+(d.ocupado?" bz":(d.estado==="PRONTO"?" ok":
      (d.estado==="ERRO"?" er":"")));
    $("sdTit").textContent=
      d.estado==="PRONTO"?("cartao pronto · "+d.livreMB+" MB livres de "+d.totalMB+" MB"):
      d.estado==="OCUPADO"?"trabalhando no cartao":
      d.estado==="SEM_CARTAO"?"nenhum cartao no slot":
      d.estado==="DESLIGADO"?"firmware compilado sem cartao":"erro no cartao";
    $("sdMsg").textContent=d.msg||"";
    $("sbSd").textContent=
      d.estado==="PRONTO"?(d.livreMB+" MB livres"):d.estado.toLowerCase().replace("_"," ");
    lamp($("lSd"),d.estado==="PRONTO"?"on":(d.estado==="ERRO"?"er":""));
    if(forcar||d.seq!==sdSeq){sdSeq=d.seq;sdLer();}
  }).catch(function(){});
}
function sdLer(){
  return fetch("/api/sd/lista?tipo="+sdTipo).then(function(r){return r.json();})
   .then(function(j){
     sdArqs=j.arq||[];
     sdPintar();
     /* A tarefa do cartao pode ainda estar montando outra pasta: nesse
        caso o firmware avisa "pronto:false" e a gente volta no proximo
        ciclo, sem ficar martelando o SPI. */
     /* Sem cartao nao adianta insistir: o firmware recusa o pedido de
        listagem e a gente ficaria martelando o SPI a cada 400 ms. */
     if(!j.pronto&&sdEstado==="PRONTO")setTimeout(function(){sdSeq=-1;},400);
   }).catch(function(){});
}

/* =====================================================================
   TRAJETORIA A MAO LIVRE
   ===================================================================== */
$("btGravIni").onclick   =function(){post("/api/gravar/iniciar");};
$("btGravFim").onclick   =function(){post("/api/gravar/parar");};
$("btRepro").onclick     =function(){post("/api/reproduzir");};
$("btTrajLimpar").onclick=function(){
  if(confirm("Apagar a trajetoria gravada?"))post("/api/traj/limpar");};
$("btHome").onclick      =function(){post("/api/home");};
/* /api/solda existia desde sempre no firmware e nao tinha acionamento
   nenhum na interface: gravar a mao livre registrava o estado do rele em
   cada instante, mas nao havia como ligar o rele. */
$("btArco").onclick=function(){
  if(D.solda){post("/api/solda?v=0");return;}
  if(confirm("O ARCO VAI ABRIR AGORA.\n\nMascara, aterramento na peca e area livre conferidos?"))
    post("/api/solda?v=1");
};
$("btMover").onclick=function(){
  post("/api/mover?t1="+($("inMt1").value||0)+"&t2="+($("inMt2").value||0));
};
/* A escala de reproducao ja existia no firmware (escalaVelocidadeTraj) e
   nao tinha campo: ficava presa em 100%. */
$("inEsc").onchange=function(){
  post("/api/config?escala="+$("inEsc").value);
};

medir();

/* Abre na aba que o operador estava usando. localStorage e por origem,
   entao cada robo lembra da sua. */
let abaInicial="mover";
try{const a=localStorage.getItem("aba");if(a)abaInicial=a;}catch(e){}
irAba(abaInicial);

setInterval(tick,220);
setInterval(pintar,45);
tick();
lerPontos();
sdAtualizar(true);
</script>
</body>
</html>
)rawliteral";
