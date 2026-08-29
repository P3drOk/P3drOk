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
<meta name="apple-mobile-web-app-title" content="Robo2dof">
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
/* PUXAR PARA ATUALIZAR, NAO.
   Num painel de maquina, recarregar a pagina no meio de um trabalho e um
   acidente: o celular perde a aba aberta, os campos meio preenchidos e o
   heartbeat que segura o movimento. E acontecia sozinho -- rolar uma
   secao ate o topo e continuar puxando faz o Chrome do Android
   interpretar como "atualize".
   overscroll-behavior:none no body nao bastava: quem rola aqui sao os
   paineis de dentro (.rol, .cfgRol, .cx), e o excesso deles SOBE em
   cadeia ate o documento. A trava tem de estar em cada rolagem, e
   tambem no html -- que e o elemento que define a janela. */
html,body{margin:0;height:100%;width:100%;overflow-x:hidden;
 background:var(--fundo);color:var(--letra);
 font:14px/1.5 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;
 overscroll-behavior:none;-webkit-overflow-scrolling:touch}
/* Todo container que rola para a cadeia nele mesmo. */
.rol,.cfgRol,.cx,.dockEnc,.tabAmostras,.res,.grelha,.lista{overscroll-behavior:contain}
button,input{font:inherit;color:inherit}
:focus-visible{outline:2px solid var(--arco);outline-offset:2px}
@media(prefers-reduced-motion:reduce){*{animation:none!important;transition:none!important}}
.rot{font-family:var(--mono);font-size:9.5px;letter-spacing:.17em;
 text-transform:uppercase;color:var(--letra2)}

.app{display:grid;grid-template-rows:auto 1fr;height:100%;min-width:0;overflow:hidden}
.corpo{display:grid;grid-template-columns:1fr 400px;gap:10px;padding:10px;
 min-height:0;overflow:hidden}

/* ---------- coluna do Encoder ----------
   Em tela larga ela e uma TERCEIRA coluna, sempre aberta: a leitura do
   encoder existe para ser acompanhada enquanto se mexe no resto, e
   trocar de aba para olhar o erro e perder o momento em que ele
   acontece. Abaixo de 1300px nao ha largura honesta para tres colunas,
   e ela volta a ser uma aba como as outras. */
.dockEnc{display:none}
@media(min-width:1301px){
  .corpo{grid-template-columns:380px minmax(0,1fr) 400px}
  .dockEnc{display:block;overflow-y:auto;overscroll-behavior:contain;
   min-width:0;padding-right:2px}
  .dockEnc #pnEnc{display:block}
  /* Fica aberta em qualquer aba, inclusive na "mesa". */
  .dockEnc .et{margin-bottom:9px}
  /* Sem o botao de aba: a coluna ja esta na tela. */
  .abas button[data-aba="enc"],
  .abasTopo button[data-aba="enc"]{display:none}
}
/* Itens de grid nao encolhem abaixo do conteudo sem min-width:0.
   Sem isso o painel empurra a pagina e vaza na horizontal no celular. */
.corpo>*{min-width:0}
@media(max-width:1020px){
  .corpo{grid-template-columns:minmax(0,1fr);grid-template-rows:38vh 1fr}
}
@media(max-width:1300px){
  /* Volta a ser aba: aparece so quando escolhida, como as outras. */
  .dockEnc{display:block;min-width:0;overflow-y:auto;overscroll-behavior:contain}
  body[data-aba="enc"] .coluna{display:none}
  body:not([data-aba="enc"]) .dockEnc{display:none}
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
/* TIRA DE ESTADO.
   Eram cinco pares soltos de bolinha-sobre-rotulo, separados por 15 px de
   nada: no celular a fila encostava no PARAR e a primeira lampada saia da
   tela. Agora e UMA peca -- moldura, divisorias entre os campos, ponto e
   rotulo lado a lado -- que se le da esquerda para a direita como o
   painel de uma maquina, e que encolhe inteira em vez de perder um
   campo. */
.lamps{display:flex;margin-left:auto;min-width:0;
 background:var(--face);border:1px solid var(--linha);border-radius:5px;
 overflow:hidden}
.lp{display:flex;flex-direction:row;align-items:center;gap:6px;min-width:0;
 padding:6px 10px;border-left:1px solid var(--linha)}
.lp:first-child{border-left:none}
.olho{flex:0 0 auto;width:8px;height:8px;border-radius:50%;background:var(--linha2);
 box-shadow:inset 0 1px 2px var(--sombra)}
.lp.on .olho{background:var(--pronto);box-shadow:0 0 9px var(--pronto)}
.lp.at .olho{background:var(--arco);box-shadow:0 0 9px var(--arco)}
.lp.hot .olho{background:var(--quente);box-shadow:0 0 14px var(--quente);animation:pi .7s infinite}
.lp.er .olho{background:var(--brasa);box-shadow:0 0 12px var(--brasa);animation:pi .45s infinite}
@keyframes pi{50%{opacity:.25;box-shadow:none}}
.lp span{font-family:var(--mono);font-size:8.5px;letter-spacing:.09em;color:var(--letra2);
 text-transform:uppercase;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
/* Aceso, o rotulo acende junto: ponto colorido com legenda apagada ao
   lado obriga a olhar duas vezes para ler o mesmo estado. */
.lp.on span,.lp.at span,.lp.hot span,.lp.er span{color:var(--letra)}
/* O campo do modo e o unico que muda de palavra: reservar largura evita
   a tira inteira pular de tamanho a cada troca de estado. */
#lModo{min-width:96px}
#lModo span{font-weight:700}
.estop{flex:0 0 auto;background:var(--brasa);border:none;color:#fff;font-family:var(--mono);font-size:12px;
 font-weight:700;letter-spacing:.15em;padding:12px 20px;border-radius:4px;cursor:pointer;
 box-shadow:0 3px 0 rgba(0,0,0,.35);margin-left:8px}
.estop:active{box-shadow:0 1px 0 rgba(0,0,0,.35);transform:translateY(2px)}

/* O motor tem botao proprio no cabecalho, ao lado do PARAR e visivel de
   toda aba: ligar e desligar torque e a coisa que mais se aperta na
   maquina, e estava enterrada numa gaveta de Ajustes. A cor diz o
   estado -- verde tem torque, cinza nao, ambar esperando o barramento
   responder. Nao e o PARAR e nao pode ser confundido com ele. */
.ch.indo{opacity:.55;animation:pi .7s infinite}
.motores{display:flex;gap:6px;margin-left:auto;flex:0 0 auto}
.motor{flex:0 0 auto;border:none;font-family:var(--mono);font-size:12px;font-weight:700;
 letter-spacing:.08em;padding:12px 14px;border-radius:4px;cursor:pointer;color:#fff;
 background:var(--linha2);box-shadow:0 3px 0 rgba(0,0,0,.25)}
.motor.on{background:var(--pronto)}
.motor.indo{background:var(--quente)}
.motor.ruim{background:var(--brasa)}
.motor:active{box-shadow:0 1px 0 rgba(0,0,0,.25);transform:translateY(2px)}
@media(max-width:760px){
  .motores{gap:4px}
  .motor{padding:9px 8px;font-size:10px;letter-spacing:.02em}
}

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
.lg.c i{border-top:2px solid var(--arco)}

.zoom{position:absolute;right:12px;top:12px;display:flex;flex-direction:column;gap:5px}
.zb{width:32px;height:32px;background:var(--painel);opacity:.94;border:1px solid var(--linha);
 border-radius:4px;color:var(--letra2);cursor:pointer;font-size:15px;line-height:1;
 backdrop-filter:blur(4px)}
.zb:hover{color:var(--letra);border-color:var(--linha2)}
.zb.pq{font-family:var(--mono);font-size:8.5px;letter-spacing:.04em}
.zb.on{background:var(--arco);border-color:var(--arco);color:#fff}
.tela canvas.des{cursor:crosshair;touch-action:none}
.barraDes{position:absolute;left:12px;right:12px;bottom:12px;display:none;
 align-items:center;gap:8px;flex-wrap:wrap;background:var(--painel);opacity:.97;
 border:1px solid var(--linha);border-radius:5px;padding:8px 10px}
body[data-des="1"] #barraDes{display:flex}
.barraDes .cnt{flex:1;min-width:120px;font-family:var(--mono);font-size:9.5px;
 letter-spacing:.06em;color:var(--letra2);text-transform:uppercase}
.barraDes .b{margin:0;width:auto;flex:0 0 auto;white-space:nowrap}
body[data-pos="1"] #barraPos{display:flex}
body[data-pos="1"] .tela canvas{cursor:move;touch-action:none}
.barraDes .cnt.ruim{color:var(--brasa)}
.barraDes .cnt.bom{color:var(--pronto)}

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
.rol{overflow-y:auto;overflow-x:hidden;padding:10px;flex:1;scrollbar-width:thin;min-width:0;
 overscroll-behavior:contain}
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
.sprite{display:none}
/* Todo icone da interface sai deste molde: mesma grade, mesmo traco, cor
   do texto ao redor. E o que faz vinte cartoes parecerem um sistema em
   vez de vinte enfeites. */
.ic{width:15px;height:15px;fill:none;stroke:currentColor;stroke-width:1.7;
 stroke-linecap:round;stroke-linejoin:round}
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
/* Onde a junta esta DENTRO do curso calibrado. Numero sozinho nao diz
   se o braco esta sobrando ou raspando no limite. */
.fxB{display:block;position:relative;height:4px;margin-top:4px;border-radius:2px;
 background:var(--fundo);border:1px solid var(--linha);overflow:hidden}
.fxB i{position:absolute;top:-1px;bottom:-1px;width:3px;margin-left:-1.5px;
 background:var(--arco);border-radius:1px;transition:left .12s}
.fxB.perto i{background:var(--brasa)}
.fxB u{position:absolute;top:0;bottom:0;background:rgba(185,28,28,.20)}
.jb{background:var(--face);border:1px solid var(--linha);border-radius:5px;height:56px;
 font-size:26px;line-height:1;cursor:pointer;user-select:none;touch-action:none;
 color:var(--letra2);transition:background .08s,color .08s,border-color .08s;
 box-shadow:0 1px 0 rgba(255,255,255,.5) inset}
.jb:hover{color:var(--arco);border-color:var(--arco2)}
.jb:active,.jb.press{background:var(--arco);color:#fff;border-color:var(--arco);
 box-shadow:none;transform:translateY(1px)}
/* O rotulo do sentido vive no botao, nao so no title: no celular nao ha
   ponteiro, e title nunca aparece. */
.jb small{display:block;font-size:8px;letter-spacing:.08em;margin-top:3px;
 font-family:var(--mono);opacity:.75}

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
.b.x{color:var(--brasa);border-color:var(--brasa)}
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
.cp select{flex:0 0 auto;background:var(--fundo);border:1px solid var(--linha);
 border-radius:2px;padding:8px 9px;font-family:var(--mono);font-size:12px;color:var(--letra)}
.cp .un{font-family:var(--mono);font-size:9px;color:var(--letra3);width:34px}
.nt{font-size:11.5px;color:var(--letra2);margin:0 0 10px;line-height:1.55}
/* Aviso: mesma nota, mas com a tarja. Nao e decoracao -- e o que
   separa "leia se quiser" de "leia antes de apertar". */
.nt.av{border-left:3px solid var(--quente);padding-left:8px;color:var(--letra)}
/* As explicacoes ensinam quem esta comecando e atrapalham quem ja opera
   todo dia: elas ocupam mais coluna que os controles. O botao "?" no
   cabecalho esconde todas de uma vez, e a escolha fica gravada. Esconder
   nao e apagar -- um clique traz tudo de volta. */
/* A gaveta tem o SEU proprio interruptor de explicacoes, e ele nasce
   desligado.
   O motivo e de proporcao, nao de gosto: na tela de trabalho as notas
   sao poucas e curtas, e ensinam enquanto se opera. Na gaveta sao
   dezenas de paragrafos de manual -- entre um campo e o proximo cabe uma
   pagina de texto, e quem so quer mudar a velocidade rola cinco telas
   ate achar o campo. Escondidas por padrao, a gaveta vira uma lista de
   ajustes; um toque no "?" traz o manual de volta, e a escolha fica
   gravada. */
body.semNotasCfg .cfgRol .nt{display:none}
/* Pagina escondida: nao e segredo nem senha, e um tranco para nao se
   mexer sem querer. O que esta atras dela desloca a AREA UTIL INTEIRA. */
.trancado .trancavel{display:none}
.cadeado{display:flex;align-items:center;gap:9px;background:var(--face);
 border:1px dashed var(--linha2);border-radius:4px;padding:11px 12px;
 margin-bottom:10px;cursor:pointer}
.cadeado b{font-size:12px;color:var(--letra2)}
.cadeado span{font-size:10.5px;color:var(--letra3);display:block;margin-top:2px}
.cadeado .ic{font-size:17px;color:var(--letra3);flex:0 0 auto}
.cadeado:hover{border-color:var(--arco2)}
.cadeado:hover .ic{color:var(--arco)}
.ajd{background:var(--face);border:1px solid var(--linha);border-radius:3px;
 width:30px;height:30px;flex:0 0 auto;cursor:pointer;color:var(--letra3);
 font-family:var(--mono);font-size:13px;font-weight:600;margin-left:10px}
.ajd:hover{color:var(--arco);border-color:var(--arco2)}
.ajd.on{background:var(--arco);border-color:var(--arco);color:#fff}
@media(max-width:560px){.ajd{width:26px;height:26px;font-size:11px;margin-left:6px}}
/* Motivo de um botao estar fora de acao. Nada de botao morto e mudo. */
.pq2{display:none;font-size:11px;color:var(--quente);margin:-5px 0 10px;
 line-height:1.5;padding-left:2px}
.b:disabled{opacity:.42;cursor:not-allowed}
.nt b{color:var(--letra);font-weight:600}
/* Zona de perigo: a borda avisa antes de o dedo chegar. Sem isto o
   cartao de apagar tudo parece igual ao de trocar o idioma. */
.et.zPerigo{border-color:var(--brasa)}
.et.zPerigo .mk{color:var(--brasa);border-color:var(--brasa)}
.b.perigoso{background:var(--brasa);border-color:var(--brasa);color:#fff}
.b.perigoso:hover{filter:brightness(1.12)}
/* GRAVANDO: um estado que so aparecia no rotulo minusculo do cabecalho
   do cartao. O operador apertava "Iniciar gravacao", nada visivel mudava
   na tela em que ele estava, e ele concluia que o botao nao funcionava.
   Agora o proprio cartao diz em que pe esta, e quantas amostras ja
   entraram -- que e a prova de que mover o braco esta sendo registrado. */
.gravBox{display:flex;align-items:center;gap:10px;margin-bottom:10px;
 background:var(--face);border:1px solid var(--linha);border-radius:4px;padding:10px 11px}
.gravBox .pt{flex:0 0 auto;width:10px;height:10px;border-radius:50%;
 background:var(--linha2)}
.gravBox .tx{min-width:0;font-size:11.5px;line-height:1.45}
.gravBox b{font-size:12px}
.gravBox span{color:var(--letra2)}
.gravBox.on{border-color:var(--brasa)}
.gravBox.on .pt{background:var(--brasa);animation:pi .8s infinite}
.gravBox.tem{border-color:var(--pronto)}
.gravBox.tem .pt{background:var(--pronto)}
.perigo{font-size:11.5px;background:var(--face);border-left:3px solid var(--quente);border-top:1px solid var(--linha);border-right:1px solid var(--linha);border-bottom:1px solid var(--linha);color:var(--letra);
 padding:10px 11px;border-radius:3px;margin-bottom:10px;line-height:1.55}
/* Estado do modo aprendizado. Precisa ser visivel de longe: quando ele
   esta ligado o braco esta SOLTO, e isso nao pode depender de o operador
   estar olhando para a letra miuda. */
.aprEst{font-family:var(--mono);font-size:10.5px;color:var(--fraca);
 background:var(--face);border:1px solid var(--linha);border-radius:3px;
 padding:7px 10px;margin-bottom:9px}
.aprEst.on{color:var(--arco);border-color:var(--arco2);font-weight:600}
/* Grelha da tela de saude: rotulo a esquerda, numero a direita. Linha a
   linha, para ser lida em pe na frente da maquina. */
/* Angulo medido pelo encoder, debaixo do comandado. Fica discreto quando
   os dois concordam e vermelho quando divergem: o operador so precisa
   olhar quando ha o que olhar. */
/* Botao do arco depois do primeiro toque: pisca ate confirmar ou expirar. */
.b.armado{background:var(--quente);color:#fff;animation:pulsa 1s ease-in-out infinite}
@keyframes pulsa{50%{opacity:.55}}
.med{display:block;font-family:var(--mono);font-size:9.5px;font-weight:400;
 color:var(--fraca);text-decoration:none;line-height:1.5;letter-spacing:0}
.med.dif{color:var(--quente)}
.med.sem{opacity:.45}
/* Barra de producao: pausar, repetir e a contagem de pecas. */
.prod{display:flex;flex-wrap:wrap;gap:8px;align-items:center;margin-top:10px}
.prod .b{flex:1 1 130px;margin:0}
.prod .pq2{flex-basis:100%;order:9}
.cont{flex:1 1 100%;font-family:var(--mono);font-size:11px;color:var(--fraca);
 background:var(--face);border:1px solid var(--linha);border-radius:3px;
 padding:7px 10px;text-align:center}
.pvTela{display:block;width:100%;max-width:340px;height:auto;background:var(--face);
 border:1px solid var(--linha);border-radius:3px;margin-bottom:10px}
.grelha{display:flex;flex-direction:column;gap:1px;background:var(--linha);
 border:1px solid var(--linha);border-radius:3px;overflow:hidden;margin-bottom:10px}
.sl{display:flex;justify-content:space-between;gap:10px;background:var(--face);
 padding:7px 10px;font-size:11.5px}
.sl span{color:var(--fraca)}
.sl b{font-family:var(--mono);font-size:11px;color:var(--letra);text-align:right}
.sb.alerta{color:var(--quente)}
/* Os dois QR lado a lado, e fundo branco fixo: leitor espera escuro
   sobre claro, e no tema escuro um codigo invertido nao abre. */
.linhaB{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:8px}
.linhaB .b{flex:1 1 auto;margin:0;width:auto}
.qrPar{display:flex;gap:12px;flex-wrap:wrap;margin-bottom:10px}
.qrCx{background:#fff;border:1px solid var(--linha);border-radius:4px;padding:8px;
 text-align:center}
.qrCx canvas{display:block;width:180px;height:180px;image-rendering:pixelated}
.qrLg{font-size:10.5px;color:#333;margin-top:5px}
.res i{color:var(--fraca);font-style:normal;font-size:10px;
 display:inline-block;min-width:74px}
input[type=file]{font-size:11px;color:var(--fraca);margin-bottom:8px;max-width:100%}
.res{font-family:var(--mono);font-size:10.5px;color:var(--arco);background:var(--face);
 border:1px solid var(--linha);border-radius:3px;padding:8px 10px;margin-bottom:9px;
 line-height:1.6}
.pgr{height:3px;background:var(--fundo);border-radius:2px;overflow:hidden;margin:9px 0 3px}
.pgr i{display:block;height:100%;background:var(--quente);width:0;transition:width .25s}

.veu{position:fixed;inset:0;background:rgba(20,25,32,.72);display:none;align-items:center;
 justify-content:center;padding:16px;z-index:70;backdrop-filter:blur(3px)}
.veu.on{display:flex}
.cx{background:var(--mesa);border:1px solid var(--linha);border-radius:5px;padding:20px;
 width:100%;max-width:400px;max-height:92vh;overflow-y:auto;overscroll-behavior:contain}
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

/* ---------------------------------------------------------------------
   GAVETA DE CONFIGURACAO
   Ela e larga e alta de proposito: o que mora aqui sao formularios de
   instalacao, nao botoes de turno. Ocupar a tela toda enquanto se ajusta
   e melhor do que espremer campos numa coluna.
   --------------------------------------------------------------------- */
/* A gaveta NAO cobre o cabecalho.
   O botao PARAR mora la em cima, e parada de emergencia que exige fechar
   uma janela antes nao e parada de emergencia. O veu continua cobrindo o
   resto (para o toque fora fechar), mas o cabecalho sobe acima dele e
   continua clicavel -- PARAR, as lampadas, a ajuda e a propria
   engrenagem.
   A altura do cabecalho e medida em tempo de execucao: ela muda com a
   largura da tela, e chutar um valor deixaria a gaveta escondida atras
   dele em algum telefone. */
.placa{position:relative;z-index:80}
.cfgVeu{align-items:flex-start;
 padding-top:calc(var(--altCab, 82px) + 8px);
 padding-bottom:calc(var(--altAbas, 0px) + 8px)}
.cfgVeu .cfgCx{max-width:760px;width:100%;
 height:calc(100vh - var(--altCab, 82px) - var(--altAbas, 0px) - 24px);
 display:flex;flex-direction:column;padding:0;overflow:hidden}
.cfgTopo{display:flex;align-items:center;gap:10px;padding:14px 16px 10px;
 border-bottom:1px solid var(--linha)}
.cfgTopo h2{flex:1;margin:0}
.cfgTopo .ajd{flex:0 0 auto}
.cfgTopo .b{margin:0;width:auto;flex:0 0 auto}
.cfgAbas{display:flex;gap:4px;padding:10px 16px 0;border-bottom:1px solid var(--linha)}
.cfgAbas button{flex:1;background:none;border:none;border-bottom:2px solid transparent;
 color:var(--fraca);font:inherit;font-size:12px;padding:8px 4px 9px;cursor:pointer;
 border-radius:3px 3px 0 0}
.cfgAbas button:hover{color:var(--letra)}
.cfgAbas button.on{color:var(--arco);border-bottom-color:var(--arco)}
.cfgRol{flex:1;overflow-y:auto;overflow-x:hidden;padding:12px 16px 18px;overscroll-behavior:contain;
 scrollbar-width:thin;min-width:0}
/* A engrenagem gira devagar ao passar o dedo: e a unica animacao da tela
   e existe para dizer que ali se MEXE em coisa, em vez de operar. */
.ajd.eng{padding:0;display:inline-flex;align-items:center;justify-content:center}
.ajd.eng svg{transition:transform .4s ease}
.ajd.eng:hover svg{transform:rotate(45deg)}
.ajd.eng.on{background:var(--arco);border-color:var(--arco);color:#fff}
/* No modo operador some o que e instalacao; sobra o painel Sistema, que
   e por onde ele sai do modo. */

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
/* Bloco que so aparece quando o operador pede. Nada e removido da tela:
   o que muda e quem precisa olhar. */
.oculto{display:none}
/* Titulo que abre um bloco. A seta diz que ali ha mais coisa -- sem ela o
   operador nao descobre o "Avancado" e reclama que sumiu. */
h4.dobra{cursor:pointer;user-select:none;display:flex;align-items:center;gap:6px}
h4.dobra::before{content:"\25B8";font-size:9px;transition:transform .15s;display:inline-block}
h4.dobra.aberto::before{transform:rotate(90deg)}
/* Lista de passos da calibracao guiada. Cada linha diz o estado antes de
   dizer o nome: o operador precisa saber o que FALTA, nao o que existe. */
.guia{display:grid;gap:6px;margin-bottom:10px}
.gp{display:flex;align-items:center;gap:9px;padding:9px 10px;cursor:pointer;
 background:var(--painel);border:1px solid var(--linha);border-radius:4px}
.gp .n{flex:0 0 auto;width:20px;height:20px;border-radius:50%;display:grid;
 place-items:center;font-family:var(--mono);font-size:9.5px;font-weight:700;
 background:var(--face);color:var(--letra2);border:1px solid var(--linha)}
.gp .tt2{flex:1;min-width:0;font-size:12.5px}
.gp .tt2 small{display:block;font-family:var(--mono);font-size:9px;
 color:var(--letra2);letter-spacing:.04em;text-transform:uppercase}
.gp.ok{border-color:var(--pronto)}
.gp.ok .n{background:var(--pronto);color:#052a17;border-color:var(--pronto)}
.gp.agora{border-color:var(--arco);box-shadow:inset 3px 0 0 var(--arco)}
.gp.agora .n{background:var(--arco);color:#0c1530;border-color:var(--arco)}
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
/* ---------- encoder ---------- */
.rodas{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:10px}
.roda{position:relative;aspect-ratio:1/1;background:var(--mesa);
 border:1px solid var(--linha);border-radius:5px;min-width:0}
.roda canvas{position:absolute;inset:0;width:100%;height:100%}
.roda .rot{position:absolute;left:0;right:0;bottom:5px;text-align:center;
 font-family:var(--mono);font-size:8.5px;letter-spacing:.1em;
 color:var(--letra3);text-transform:uppercase}
.encGrade{display:grid;grid-template-columns:repeat(3,1fr);gap:6px;margin-bottom:10px}
/* Tabela das amostras: rola sozinha, para nao empurrar a coluna toda.
   Numero em fonte mono, senao coluna de digito nao alinha e comparar
   duas linhas vira trabalho. */
.tabAmostras{max-height:190px;overflow-y:auto;overscroll-behavior:contain;
 border:1px solid var(--linha);border-radius:4px;margin-bottom:9px;
 background:var(--painel)}
.tabAmostras table{width:100%;border-collapse:collapse;font-family:var(--mono);
 font-size:10.5px}
.tabAmostras th{position:sticky;top:0;background:var(--face);z-index:1;
 padding:5px 6px;text-align:right;color:var(--letra3);font-weight:600;
 letter-spacing:.05em;border-bottom:1px solid var(--linha)}
.tabAmostras th:first-child{text-align:left}
.tabAmostras td{padding:3px 6px;text-align:right;color:var(--letra2);
 border-bottom:1px solid var(--linha)}
.tabAmostras td:first-child{text-align:left;color:var(--letra3)}
.tabAmostras tr:last-child td{border-bottom:none}
.tabAmostras td.ruim{color:var(--brasa)}
.encCel{background:var(--painel);border:1px solid var(--linha);border-radius:4px;
 padding:8px 6px;text-align:center;min-width:0}
.encCel .rot{display:block;font-family:var(--mono);font-size:8.5px;
 letter-spacing:.08em;color:var(--letra3);text-transform:uppercase}
.encCel b{display:block;font-family:var(--mono);font-size:15px;font-weight:500;
 margin-top:3px;font-variant-numeric:tabular-nums;overflow:hidden;
 text-overflow:ellipsis}
.encCel.err b{color:var(--arco)}
.encCel.err.ruim b{color:var(--brasa)}
.grafico{position:relative;height:170px;background:var(--mesa);
 border:1px solid var(--linha);border-radius:5px;margin-bottom:10px}
.grafico canvas{position:absolute;inset:0;width:100%;height:100%}
.lg.g1 i{border-top:2px solid var(--arco)}
.lg.g2 i{border-top:2px solid var(--quente)}
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
  /* A barra de abas fica ACIMA da gaveta, como o cabecalho: tocar numa
     aba de trabalho com a configuracao aberta e o gesto natural de
     "voltar ao trabalho", e nao pode esbarrar num veu. */
  .abas{display:grid;grid-auto-flow:column;grid-auto-columns:1fr;
   background:var(--painel);border-top:1px solid var(--linha);
   position:relative;z-index:80;
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
  .lamps{grid-column:1;grid-row:2;margin-left:0;
   display:grid;grid-auto-flow:column;grid-auto-columns:1fr}
  .lp{min-width:0;gap:5px;padding:6px 6px;justify-content:center}
  #lModo{min-width:0}
  .lp span{font-size:8px;letter-spacing:.04em}
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
<!-- =====================================================================
     UM SO CONJUNTO DE ICONES
     Antes cada cartao trazia um dingbat diferente -- ✉ ⚙ ♆ ♥ ☰ ☉ ▣ ⏉ --
     de blocos Unicode distintos, e dois deles (cadeado e disquete) sao
     emoji COLORIDO em quase todo sistema. Traco, peso e cor mudavam de
     linha para linha, e no celular mudavam de novo: era a maior fonte de
     poluicao visual da gaveta.
     Aqui e um desenho so, mesma grade de 24, mesmo traco, na cor do
     texto. Cada uso custa ~40 bytes.
     ===================================================================== -->
<svg class="sprite" aria-hidden="true"><defs>
<symbol id="i-medidor" viewBox="0 0 24 24"><path d="M4 17a8 8 0 1116 0"/><path d="M12 17l4-5"/></symbol>
<symbol id="i-grafico" viewBox="0 0 24 24"><path d="M4 5v14h16"/><path d="M8 16v-4M12 16V8M16 16v-6"/></symbol>
<symbol id="i-onda" viewBox="0 0 24 24"><path d="M2 12h4l3-7 4 14 3-7h6"/></symbol>
<symbol id="i-cruz" viewBox="0 0 24 24"><path d="M12 4v16M4 12h16"/><path d="M12 4l-2.5 2.5M12 4l2.5 2.5M12 20l-2.5-2.5M12 20l2.5-2.5M4 12l2.5-2.5M4 12l2.5 2.5M20 12l-2.5-2.5M20 12l-2.5 2.5"/></symbol>
<symbol id="i-arquivo" viewBox="0 0 24 24"><path d="M14 3H7a2 2 0 00-2 2v14a2 2 0 002 2h10a2 2 0 002-2V8z"/><path d="M14 3v5h5"/></symbol>
<symbol id="i-caminho" viewBox="0 0 24 24"><path d="M4 18c6 0 4-12 10-12 3 0 5 2 5 5"/><circle cx="4" cy="18" r="1.7"/><circle cx="19" cy="11" r="1.7"/></symbol>
<symbol id="i-cartao" viewBox="0 0 24 24"><rect x="5" y="3" width="14" height="18" rx="2"/><path d="M9 3v5M12 3v5M15 3v5"/></symbol>
<symbol id="i-info" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="M12 11v5M12 8h.01"/></symbol>
<symbol id="i-rede" viewBox="0 0 24 24"><path d="M5 12a10 10 0 0114 0"/><path d="M8.5 15.5a5 5 0 017 0"/><path d="M12 19h.01"/></symbol>
<symbol id="i-regua" viewBox="0 0 24 24"><path d="M4 7h10M18 7h2M4 17h4M12 17h8"/><circle cx="16" cy="7" r="2"/><circle cx="10" cy="17" r="2"/></symbol>
<symbol id="i-alvo" viewBox="0 0 24 24"><circle cx="12" cy="12" r="8"/><circle cx="12" cy="12" r="3"/></symbol>
<symbol id="i-disco" viewBox="0 0 24 24"><path d="M19 21H5a2 2 0 01-2-2V5a2 2 0 012-2h11l5 5v11a2 2 0 01-2 2z"/><path d="M17 21v-8H7v8M7 3v5h8"/></symbol>
<symbol id="i-mira" viewBox="0 0 24 24"><circle cx="12" cy="12" r="7"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3"/></symbol>
<symbol id="i-casa" viewBox="0 0 24 24"><path d="M4 11l8-7 8 7"/><path d="M6 10v10h12V10"/><path d="M10 20v-6h4v6"/></symbol>
<symbol id="i-plug" viewBox="0 0 24 24"><path d="M9 7V3M15 7V3"/><path d="M7 7h10v5a5 5 0 01-10 0z"/><path d="M12 17v4"/></symbol>
<symbol id="i-escudo" viewBox="0 0 24 24"><path d="M12 3l7 3v6c0 4-3 7-7 9-4-2-7-5-7-9V6z"/><path d="M9 12l2 2 4-4"/></symbol>
<symbol id="i-lista" viewBox="0 0 24 24"><path d="M8 6h12M8 12h12M8 18h12M4 6h.01M4 12h.01M4 18h.01"/></symbol>
<symbol id="i-qr" viewBox="0 0 24 24"><rect x="4" y="4" width="6" height="6"/><rect x="14" y="4" width="6" height="6"/><rect x="4" y="14" width="6" height="6"/><path d="M14 14h2v2h-2zM18 14h2M14 18h2M18 18h2"/></symbol>
<symbol id="i-cima" viewBox="0 0 24 24"><path d="M12 19V5"/><path d="M6 11l6-6 6 6"/><path d="M4 21h16"/></symbol>
<symbol id="i-cadeado" viewBox="0 0 24 24"><rect x="5" y="10" width="14" height="11" rx="2"/><path d="M8 10V7a4 4 0 018 0v3"/></symbol>
<symbol id="i-lixo" viewBox="0 0 24 24"><path d="M4 7h16"/><path d="M9 7V5h6v2"/><path d="M6 7l1 13h10l1-13"/><path d="M10 11v6M14 11v6"/></symbol>
<symbol id="i-engrenagem" viewBox="0 0 24 24"><circle cx="12" cy="12" r="3.2"/><circle cx="12" cy="12" r="7"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9l2.1 2.1M17 17l2.1 2.1M19.1 4.9L17 7M7 17l-2.1 2.1"/></symbol>
</defs></svg>


<div class="app">
  <header class="placa">
    <div class="nome">ROBO<b>2DOF</b></div>
    <div class="mod">ESTACAO DE SOLDA<br>2 EIXOS · SERVO AC</div>
    <button class="ajd eng" id="btCfg" title="Configuracao">
      <svg class="ic" aria-hidden="true"><use href="#i-engrenagem"/></svg>
    </button>
    <div class="lamps">
      <div class="lp" id="lModo"><i class="olho"></i><span id="lModoT">--</span></div>
      <div class="lp" id="lServo"><i class="olho"></i><span>servo</span></div>
      <div class="lp" id="lArco"><i class="olho"></i><span>arco</span></div>
      <div class="lp" id="lRede"><i class="olho"></i><span>rede</span></div>
      <div class="lp" id="lSd"><i class="olho"></i><span>cartao</span></div>
    </div>
    <div class="motores">
      <button class="motor" id="btMotor1"><span id="btMotor1T">EIXO 1</span></button>
      <button class="motor" id="btMotor2"><span id="btMotor2T">EIXO 2</span></button>
    </div>
    <button class="estop" id="btParar">PARAR</button>
  </header>

  <div class="corpo">

    <!-- ===================== ENCODER (coluna propria) =====================
         Em tela larga ele nao e uma aba: fica aberto ao lado, porque a
         leitura do encoder e para ser ACOMPANHADA enquanto se mexe no
         resto. Trocar de aba para olhar o erro e perder justamente o
         momento em que ele acontece. No celular, onde nao ha largura
         para duas colunas, volta a ser aba. -->
    <aside class="dockEnc" id="dockEnc">
      <section class="pane" id="pnEnc">
        <div class="et aberta">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-medidor"/></svg></div>
            <div class="tx"><div class="tt">Encoder dos drivers</div>
            <span class="sb" id="sbEnc">desligado</span></div></div>
          <div class="dentro">
            <div class="rodas">
              <div class="roda"><canvas id="cvR1"></canvas>
                <span class="rot">junta 1</span></div>
              <div class="roda"><canvas id="cvR2"></canvas>
                <span class="rot">junta 2</span></div>
            </div>
            <div class="nt">O ponteiro grosso e onde o <b>encoder</b> diz que o
            eixo esta; o fino e onde o firmware <b>mandou</b> ele estar. A
            abertura entre os dois e o erro. O disco pequeno no centro gira
            junto com o eixo do motor &mdash; se ele para de girar enquanto o
            braco anda, a leitura morreu.</div>
            <div class="encGrade">
              <div class="encCel"><span class="rot">junta 1 comandado</span><b id="eC1">--</b></div>
              <div class="encCel"><span class="rot">junta 1 medido</span><b id="eM1">--</b></div>
              <div class="encCel err"><span class="rot">junta 1 erro</span><b id="eE1">--</b></div>
              <div class="encCel"><span class="rot">junta 2 comandado</span><b id="eC2">--</b></div>
              <div class="encCel"><span class="rot">junta 2 medido</span><b id="eM2">--</b></div>
              <div class="encCel err"><span class="rot">junta 2 erro</span><b id="eE2">--</b></div>
            </div>
            <div class="grafico"><canvas id="cvEnc"></canvas>
              <div class="legenda">
                <div class="lg g1"><i></i>erro junta 1</div>
                <div class="lg g2"><i></i>erro junta 2</div>
              </div>
            </div>
            <div class="nt">O grafico mostra <b>comandado menos medido</b>, em
            graus da junta, nos ultimos instantes. Linha reta em zero quer dizer
            que o braco foi para onde foi mandado. <b>Degrau ou deriva quer dizer
            passo perdido</b> &mdash; e o valor nao volta sozinho.</div>
          </div>
        </div>

        <div class="et">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-grafico"/></svg></div>
            <div class="tx"><div class="tt">Analise detalhada</div>
            <span class="sb" id="sbAnal">tudo que foi captado</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="grafico"><canvas id="cvPos"></canvas>
              <div class="legenda">
                <div class="lg g1"><i></i>junta 1 medida</div>
                <div class="lg g2"><i></i>junta 2 medida</div>
              </div>
            </div>
            <div class="nt">Aqui e a <b>posicao medida</b> em si, nao o erro.
            Uma rampa limpa e movimento bom; degrau vertical sem o braco ter
            andado e leitura falhando; linha reta com o braco andando e leitura
            morta.</div>

            <h4>Numeros da junta 1</h4>
            <div class="encGrade tres">
              <div class="encCel"><span class="rot">leituras</span><b id="anN1">--</b></div>
              <div class="encCel"><span class="rot">falhas</span><b id="anF1">--</b></div>
              <div class="encCel"><span class="rot">por segundo</span><b id="anHz1">--</b></div>
              <div class="encCel"><span class="rot">erro medio</span><b id="anMe1">--</b></div>
              <div class="encCel"><span class="rot">pior erro</span><b id="anMx1">--</b></div>
              <div class="encCel"><span class="rot">oscilacao</span><b id="anSd1">--</b></div>
              <div class="encCel"><span class="rot">bruto</span><b id="anBr1">--</b></div>
              <div class="encCel"><span class="rot">voltas do motor</span><b id="anVo1">--</b></div>
              <div class="encCel"><span class="rot">idade da leitura</span><b id="anId1">--</b></div>
              <div class="encCel"><span class="rot">velocidade</span><b id="anVe1">--</b></div>
              <div class="encCel"><span class="rot">RPM do motor</span><b id="anRp1">--</b></div>
              <div class="encCel"><span class="rot">sentido</span><b id="anSe1">--</b></div>
              <div class="encCel"><span class="rot">passos andados</span><b id="anPa1">--</b></div>
              <div class="encCel"><span class="rot">inversoes</span><b id="anIv1">--</b></div>
              <div class="encCel"><span class="rot">faixa percorrida</span><b id="anFx1">--</b></div>
            </div>

            <h4>Numeros da junta 2</h4>
            <div class="encGrade tres">
              <div class="encCel"><span class="rot">leituras</span><b id="anN2">--</b></div>
              <div class="encCel"><span class="rot">falhas</span><b id="anF2">--</b></div>
              <div class="encCel"><span class="rot">por segundo</span><b id="anHz2">--</b></div>
              <div class="encCel"><span class="rot">erro medio</span><b id="anMe2">--</b></div>
              <div class="encCel"><span class="rot">pior erro</span><b id="anMx2">--</b></div>
              <div class="encCel"><span class="rot">oscilacao</span><b id="anSd2">--</b></div>
              <div class="encCel"><span class="rot">bruto</span><b id="anBr2">--</b></div>
              <div class="encCel"><span class="rot">voltas do motor</span><b id="anVo2">--</b></div>
              <div class="encCel"><span class="rot">idade da leitura</span><b id="anId2">--</b></div>
              <div class="encCel"><span class="rot">velocidade</span><b id="anVe2">--</b></div>
              <div class="encCel"><span class="rot">RPM do motor</span><b id="anRp2">--</b></div>
              <div class="encCel"><span class="rot">sentido</span><b id="anSe2">--</b></div>
              <div class="encCel"><span class="rot">passos andados</span><b id="anPa2">--</b></div>
              <div class="encCel"><span class="rot">inversoes</span><b id="anIv2">--</b></div>
              <div class="encCel"><span class="rot">faixa percorrida</span><b id="anFx2">--</b></div>
            </div>
            <div class="nt"><b>Velocidade, RPM e sentido saem do proprio
            encoder</b>, medidos pelo firmware entre duas leituras &mdash; nao e
            a velocidade que o firmware mandou, e a que o eixo fez. Comparar as
            duas e o jeito de ver escorregamento.
            <br><b>Passos andados</b> soma o caminho, nao a diferenca entre as
            pontas: ir e voltar nao da zero, da o dobro. <b>Inversoes</b> conta
            trocas de sentido de verdade &mdash; tremor de um passo nao conta, e
            e por isso que esse numero serve para achar folga.</div>
            <div class="nt"><b>Oscilacao</b> e o quanto o erro balanca em torno
            da media. Media alta com oscilacao baixa e desalinhamento &mdash; da
            para corrigir na referencia. Oscilacao alta e folga ou ruido, e
            corrigir a referencia nao resolve.</div>

            <h4>Ultimas amostras</h4>
            <div class="tabAmostras"><table id="tabEnc"><tbody></tbody></table></div>
            <button class="b mini" id="btEncCsv">Baixar tudo em CSV</button>
            <div class="pq2" id="qEncCsv"></div>
            <div class="nt">O CSV traz <b>toda</b> a janela guardada, nao so o
            que cabe na tabela: instante, bruto, medido, comandado e erro das
            duas juntas. E para abrir na planilha e olhar a curva com calma.</div>
          </div>
        </div>

        <div class="et">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-onda"/></svg></div>
            <div class="tx"><div class="tt">Diagnostico da linha</div>
            <span class="sb">quando nao esta lendo</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <button class="b mini" id="btEncTestar">Testar a linha agora</button>
            <div class="pq2" id="qEncTestar"></div>
            <div class="res" id="encRel">--</div>
            <div class="nt">O mesmo autoteste do programa de bancada, so que
            <b>aqui dentro</b>, com Wi-Fi, cartao e os motores rodando &mdash; que
            e onde o problema aparece. Leia de cima para baixo:
            <br><b>eco</b> &mdash; os proprios bytes voltam? Se sim, a ligacao
            ESP32&harr;MAX485 esta boa <i>dentro do sistema</i> e o que sobra e o
            barramento. Se nao, o problema nem chegou no par A/B.
            <br><b>f3 r0</b> e <b>f4 r0</b> &mdash; e assim que se acha o driver.
            Ate <b>EXCECAO</b> e boa noticia: quer dizer que ele esta ai e
            respondeu, so a pergunta e que nao serve.
            <br>A ultima linha e a pergunta de verdade, com o registrador que
            esta configurado.</div>
            <button class="b mini" id="btEncCacar">Procurar o registrador</button>
            <button class="b mini" id="btEncComparar">Comparar agora</button>
            <div class="pq2" id="qEncCacar"></div>
            <div class="nt">Nao existe manual do mapa Modbus do T3D. O jeito
            honesto de achar o registrador da posicao e este: aperte
            <b>Procurar o registrador</b>, depois <b>mova o braco a mao,
            bastante</b>, e aperte <b>Comparar agora</b>. O registrador que
            andou junto com o eixo e a posicao &mdash; os outros nao andam. O
            que variar <b>mais</b> e a palavra baixa; o vizinho de cima, que
            variou pouco, e a alta.</div>
            <button class="b mini" id="btEncZerar">Zerar a contagem aqui</button>
            <div class="pq2" id="qEncZerar"></div>
            <div class="res" id="encEstado">--</div>
            <div class="nt">Ultima conversa no fio, byte a byte &mdash; a
            mesma coisa que o programa de teste de bancada mostra. Se depois
            da seta de volta vier <b>(silencio)</b>, ninguem respondeu: veja
            fio A/B, o DE/RE e o <b>endereco</b>. Se vierem bytes mas a
            leitura nao vale, o driver respondeu outra coisa: veja a
            <b>funcao</b> e o <b>registrador</b>.</div>
            <div class="res" id="encQuadro">--</div>
          </div>
        </div>
      </section>

      <!-- =========================== AJUSTES =========================== -->
    </aside>

    <section class="quadro">
      <div class="tela">
        <canvas id="cv"></canvas>
        <div class="legenda">
          <div class="lg"><i></i>cordao · reta</div>
          <div class="lg d"><i></i>deslocamento · curva das juntas</div>
          <div class="lg t"><i></i>trajetoria gravada a mao livre</div>
          <div class="lg c"><i></i>curso calibrado das juntas</div>
        </div>
        <div class="zoom">
          <button class="zb" id="zMais" title="Aproximar">+</button>
          <button class="zb" id="zMenos" title="Afastar">&minus;</button>
          <button class="zb pq" id="zAuto" title="Enquadrar o braco">FIT</button>
          <button class="zb pq" id="zDes" title="Desenhar o caminho com o dedo">DES</button>
          <button class="zb pq" id="zTema" title="Alternar tema">TEMA</button>
          <button class="zb pq" id="z3D" title="Alternar vista 2D / 3D">3D</button>
        </div>
        <div class="barraDes" id="barraDes">
          <span class="cnt" id="dCnt">risque com o dedo sobre a mesa</span>
          <button class="b mini" id="dSolda">cordao: nao</button>
          <button class="b mini" id="dLimpar">Refazer</button>
          <button class="b pri mini" id="dEnviar">Virar programa</button>
        </div>
        <div class="barraDes" id="barraPos">
          <span class="cnt" id="pCnt">arraste o desenho para posicionar</span>
          <button class="b mini" id="pGirarM">&#8630;</button>
          <button class="b mini" id="pGirarP">&#8631;</button>
          <button class="b mini" id="pMenor">&minus;</button>
          <button class="b mini" id="pMaior">+</button>
          <button class="b mini" id="pEsp">espelhar</button>
          <button class="b mini" id="pSolda">cordao: sim</button>
          <button class="b mini" id="pCentro">centralizar</button>
          <button class="b mini" id="pOrigem">origem com o braco</button>
          <button class="b mini x" id="pCancel">Cancelar</button>
          <button class="b pri mini" id="pAplicar">Virar programa</button>
        </div>
      </div>
      <!-- Leitura de angulo. Duas colunas por junta: o angulo COMANDADO
           (a conta de pulsos do firmware) e o MEDIDO pelo encoder. Ver os
           dois lado a lado e o que transforma "acho que esta em 30 graus"
           em "esta em 30,12 graus" -- e o que denuncia um desvio antes de
           ele virar peca torta. -->
      <div class="regua">
        <div><span class="rot">junta 1</span><b id="hT1">0°</b>
             <u class="med" id="hM1">--</u></div>
        <div><span class="rot">junta 2</span><b id="hT2">0°</b>
             <u class="med" id="hM2">--</u></div>
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
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-cruz"/></svg></div>
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
            <div class="nt">Mais longe do centro, mais rapido. Soltou, parou.</div>
            <button class="b mini" id="btPrec">Precisao: desligada</button>

            <h4>Passo a passo</h4>
            <div class="eixo">
              <button class="jb" data-j="1" data-d="1" title="anti-horario">&#8634;<small>ANTI-HOR</small></button>
              <div class="id"><span class="rot">junta 1</span><div class="fx" id="fx1"></div></div>
              <button class="jb" data-j="1" data-d="-1" title="horario">&#8635;<small>HORARIO</small></button>
            </div>
            <div class="eixo">
              <button class="jb" data-j="2" data-d="1" title="anti-horario">&#8634;<small>ANTI-HOR</small></button>
              <div class="id"><span class="rot">junta 2</span><div class="fx" id="fx2"></div></div>
              <button class="jb" data-j="2" data-d="-1" title="horario">&#8635;<small>HORARIO</small></button>
            </div>
            <div class="nt">Girou ao contrario do botao? <b>Ajustes &rarr;
            Sentido dos eixos</b>.</div>

            <h4>Ir para um angulo</h4>
            <div class="cp"><label>Junta 1</label><input type="number" id="inMt1" step="0.5"><span class="un">°</span></div>
            <button class="b mini" id="btMover1">Levar a junta 1</button>
            <div class="pq2" id="qMover1"></div>
            <div class="cp"><label>Junta 2</label><input type="number" id="inMt2" step="0.5"><span class="un">°</span></div>
            <button class="b mini" id="btMover2">Levar a junta 2</button>
            <div class="pq2" id="qMover2"></div>
            <button class="b mini" id="btMover">Levar as duas</button>
            <div class="pq2" id="qMover"></div>
            <div class="nt">Vai na velocidade de <b>deslocamento</b>. Com
            <b>Precisao</b> ligada (botao acima) ele anda fino, para chegar
            perto da peca sem susto. A velocidade se muda em <b>Ajustes &rarr;
            Velocidades</b>.</div>

            <h4>Atalhos</h4>
            <button class="b ok" id="btGravar">Gravar ponto na posicao atual</button>
            <div class="pq2" id="qGravar"></div>
            <button class="b mini" id="btHome">Ir para o zero da maquina</button>
            <div class="pq2" id="qHome"></div>
            <button class="b mini x" id="btRefer">Zerar a maquina aqui</button>
            <div class="pq2" id="qRefer"></div>
            <div class="nt">O curso e contado a partir da referencia: zerar
            fora dela desloca a area util inteira.</div>
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
            <h4>Aprendizado guiado</h4>
            <div class="perigo">Solto, o braco desce pelo proprio peso. Apoie a
            ponta <b>antes</b> de soltar, e nada embaixo da ponteira.</div>
            <div class="guia" id="aprGuia"></div>
            <button class="b pri" id="btApr">1 &middot; Soltar o braco</button>
            <div class="pq2" id="qApr"></div>
            <button class="b ok" id="btAprMarcar">2 &middot; Marcar ponto aqui</button>
            <div class="pq2" id="qAprMarcar"></div>
            <button class="b mini" id="btAprFim">3 &middot; Encerrar</button>
            <div class="pq2" id="qAprFim"></div>
            <div class="aprEst" id="aprEst">desligado</div>
            <div class="nt">O braco so solta com <b>zero absoluto ensinado nas
            duas juntas</b>. Sem isso o modo vale igual, com torque, pelas setas.
            Ao encerrar, o torque <b>nao</b> volta sozinho.</div>
            <div id="lista"></div>
            <button class="b mini" id="btLimpar">Apagar programa</button>
            <button class="b mini" id="btDesf">Desfazer</button>
            <div class="pq2" id="qDesf"></div>
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

            <div class="prod">
              <button class="b" id="btPausa">Pausar</button>
              <div class="pq2" id="qPausa"></div>
              <button class="b" id="btRepetir">Mais uma peca</button>
              <div class="pq2" id="qRepetir"></div>
              <div class="cont" id="contPecas">--</div>
            </div>
            <div class="nt"><b>Pausar</b> fecha o arco e guarda em que ponto do
            cordao parou; retomar continua dali, em vez de refazer o trecho por
            cima do que ja foi soldado. O arco reabre com o mesmo tempo de
            abertura do inicio &mdash; a poca esfriou na pausa.<br><br>
            <b>Mais uma peca</b> repete o mesmo programa sem reabrir o arquivo:
            e o caso normal de producao.</div>
          </div>
        </div>

        <div class="et" id="eDxf">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-arquivo"/></svg></div>
            <div class="tx"><div class="tt">Importar desenho DXF</div>
            <span class="sb" id="sbDxf">nenhum arquivo</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt">Desenhe a peca no CAD, salve como <b>DXF</b> e traga
            o arquivo para ca. Ele e lido aqui no proprio aparelho &mdash; o robo
            recebe so a lista de pontos ja pronta, entao arquivo grande nao
            entope o ESP32.</div>
            <div class="nt">Sao aproveitadas as entidades que viram caminho:
            <b>LINE</b>, <b>LWPOLYLINE</b>, <b>POLYLINE</b>, <b>ARC</b> e
            <b>CIRCLE</b>. Texto, cotas e hachuras sao ignorados. Contornos
            separados viram cordoes separados, com deslocamento entre eles.</div>
            <input type="file" id="dxfArq" accept=".dxf,text/plain" hidden>
            <button class="b pri" id="btDxfAbrir">Escolher arquivo DXF</button>
            <div class="res" id="dxfInfo">--</div>
            <div class="cp"><label>1 unidade do arquivo vale</label>
              <input type="number" id="dxfEsc" value="1" min="0.001" step="0.1"><span class="un">mm</span></div>
            <div class="nt">Deixe em 1 se o CAD estava em milimetros. Use 25,4
            para arquivo em polegadas.</div>
            <button class="b" id="btDxfPos">Posicionar na mesa</button>
            <div class="pq2" id="qDxfPos"></div>
            <div class="nt">Na mesa de tracado voce arrasta o desenho com o dedo,
            gira, espelha e redimensiona. A barra mostra em tempo real quantos
            pontos caem <b>fora</b> da area que o braco alcanca &mdash; posicione
            ate zerar e so entao aplique.</div>
          </div>
        </div>

        <div class="et" id="eTraj">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-caminho"/></svg></div>
            <div class="tx"><div class="tt">Trajetoria a mao livre</div>
            <span class="sb" id="sbTraj">nenhuma gravada</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt">Grava o caminho inteiro enquanto voce move o braco, com o
            estado do arco em cada instante. Serve para percurso organico; para
            cordao reto use os pontos acima, que saem em reta de verdade.</div>
            <div class="nt"><b>Sem mover o braco:</b> na mesa de tracado, o botao
            <b>DES</b> deixa riscar o caminho com o dedo em cima do desenho. O
            traco vira programa de pontos na hora.</div>
            <div class="gravBox" id="gravBox">
              <div class="pt"></div>
              <div class="tx"><b id="gravTit">--</b><br><span id="gravMsg">--</span></div>
            </div>
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
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-cartao"/></svg></div>
            <div class="tx"><div class="tt">Cartao de memoria</div>
            <span class="sb" id="sbSd">procurando</span></div></div>
          <div class="dentro">
            <div class="sdBar" id="sdBar">
              <div class="pt"></div>
              <div class="tx"><b id="sdTit">--</b><br><span id="sdMsg">--</span></div>
            </div>
            <button class="b mini" id="btSdMontar">Procurar cartao de novo</button>

            <h4>Programas salvos</h4>
            <div class="res" id="sdOqueProg">--</div>
            <div class="linhaNome">
              <input id="sdNomeProg" maxlength="24" placeholder="nome do programa" autocomplete="off">
              <button class="b mini" id="btSdSalvarProg" style="width:auto;margin:0">Salvar</button>
            </div>
            <div class="pq2" id="qSdSalvarProg"></div>
            <div id="sdListaProg"></div>
            <div class="nt">Sao os desenhos e programas de ponto que voce fez
            na maquina. <b>Apagar tudo nao mexe neles</b> &mdash; so na memoria
            interna da maquina.</div>
          </div>
        </div>

        <div class="et">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-caminho"/></svg></div>
            <div class="tx"><div class="tt">Trajetorias salvas</div>
            <span class="sb" id="sbSdTraj">--</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="res" id="sdOqueTraj">--</div>
            <div class="linhaNome">
              <input id="sdNomeTraj" maxlength="24" placeholder="nome da trajetoria" autocomplete="off">
              <button class="b mini" id="btSdSalvarTraj" style="width:auto;margin:0">Salvar</button>
            </div>
            <div class="pq2" id="qSdSalvarTraj"></div>
            <div id="sdListaTraj"></div>
            <div class="nt">O caminho inteiro gravado a mao livre, com o estado
            do arco em cada instante. Grava-se em <b>Programa &rsaquo; Trajetoria
            a mao livre</b>.</div>
          </div>
        </div>
      </section>

      <!-- =========================== ENCODER =========================== -->

      <!-- ========================== MAQUINA =========================== -->
    </div></aside>
  </div>

  <nav class="abas" id="abas"></nav>
</div>

<!-- Miniatura de uma peca do cartao, sem trocar a que esta na maquina. -->
<div class="veu" id="veuPeca"><div class="cx">
  <h2 id="pvNome">--</h2>
  <div class="pp">previa do cartao</div>
  <canvas id="pvTela" width="340" height="240" class="pvTela"></canvas>
  <div class="res" id="pvInfo">--</div>
  <div class="perigo" id="pvAviso" style="display:none"></div>
  <div class="nt">Linha grossa e cordao; tracejado e deslocamento sem arco.
  O ponto maior e o inicio. Isto le o arquivo <b>sem</b> trocar o programa
  que esta na maquina.</div>
  <button class="b pri" id="pvCarregar">Carregar esta peca na maquina</button>
  <button class="b" id="pvFechar">Fechar</button>
</div></div>

<!-- =====================================================================
     GAVETA DE CONFIGURACAO (a engrenagem do topo)
     Tudo que se ajusta UMA VEZ mora aqui dentro. A tela de trabalho fica
     com o que se usa o dia inteiro: mesa, mover, programa e arquivos.
     ===================================================================== -->
<div class="veu cfgVeu" id="veuCfg"><div class="cx cfgCx">
  <div class="cfgTopo">
    <h2>Configuracao</h2>
    <button class="ajd" id="cfgAjuda" title="Mostrar ou esconder as explicacoes">?</button>
    <button class="b mini" id="cfgFechar">Fechar</button>
  </div>
  <nav class="cfgAbas" id="cfgAbas">
    <button data-cfg="maquina" class="on">Maquina</button>
    <button data-cfg="calib">Calibracao</button>
    <button data-cfg="encoder">Encoder</button>
    <button data-cfg="sistema">Sistema</button>
  </nav>
  <div class="cfgRol">
    <div class="pane on" id="cfgMaquina">
      
        <div class="et aberta" id="e1" data-e="1">
          <div class="cab"><div class="mk">1</div>
            <div class="tx"><div class="tt">Preparar a maquina</div>
            <span class="sb" id="sb1">servos e calibracao</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="tr"><div class="ch" id="chSrv1"><i></i></div>
              <span>torque no eixo 1</span></div>
            <div class="tr"><div class="ch" id="chSrv2"><i></i></div>
              <span>torque no eixo 2</span></div>
            <div class="nt">Uma chave por eixo, porque cada driver responde
            sozinho no barramento. Com um driver ligado voce trabalha no eixo
            que existe: o <b>jog</b> dele anda e <b>ir para 0 grau</b> leva so
            ele. Programa, trajetoria e cordao continuam precisando dos dois
            &mdash; com um eixo sem torque nao sai meio desenho, sai desenho
            torto.</div>
            <div class="pq2" id="qSon"></div>
            <div class="nt" id="ntSonFio">O habilita vai pelo <b>RS485</b>, nao por fio.
            Se o barramento cair, o driver fica como estava &mdash; quem corta de
            verdade e o <b>contator</b> da emergencia.</div>
            <h4>Medidas do braco</h4>
            <div class="cp"><label>Elo 1 · base ao cotovelo</label><input type="number" id="inL1" min="1"><span class="un">mm</span></div>
            <div class="cp"><label>Elo 2 · cotovelo a ponta</label><input type="number" id="inL2" min="1"><span class="un">mm</span></div>
            <div class="nt">Estas medidas mudam o desenho e a area util na mesma proporcao. Meca do centro de um eixo ao centro do outro.</div>
            <button class="b mini" id="btSalvarElos">Aplicar medidas</button>
            <h4>Bancada</h4>
            <button class="b mini" id="btTeste">Pulsar rele por 2 segundos</button>
          </div>
        </div>

        <div class="et" id="eRede">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-rede"/></svg></div>
            <div class="tx"><div class="tt">Endereco do painel</div>
            <span class="sb" id="sbRede">--</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="res" id="redeEnd">--</div>
            <div class="nt">A maquina tem <b>Wi-Fi proprio</b> e so isso. Ela nao
            entra na rede de ninguem, nao procura roteador e nao fala com a
            internet: o painel nao depende de nada de fora para funcionar.</div>
            <div class="nt">Entre no Wi-Fi da maquina e abra qualquer um dos dois
            enderecos. Depois de entrar na rede, o celular costuma oferecer abrir
            o painel sozinho &mdash; e digitar qualquer coisa na barra de
            endereco tambem cai aqui.</div>
            <div class="nt">Ja houve aqui um modo de entrar na rede da oficina.
            Ele saiu porque o ESP32 tem <b>um radio so</b>: ligado nas duas redes,
            o ponto de acesso e obrigado a acompanhar o canal do roteador e o
            radio passa a dividir tempo. Isso vira atraso e tremor no joystick, e
            o heartbeat do jog e justamente o que nao pode atrasar.</div>
          </div>
        </div>

        <div class="et" id="e5" data-e="5">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-regua"/></svg></div>
            <div class="tx"><div class="tt">Ajustes da maquina</div>
            <span class="sb" id="sbAjustes">--</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt">Os ajustes so sao aceitos com o robo parado no modo
            manual.</div>

            <h4>Velocidade do braco</h4>
            <div class="seg" id="segVel">
              <button data-v="lento">Lento</button>
              <button data-v="normal">Normal</button>
              <button data-v="rapido">Rapido</button>
              <button data-v="custom">Ajustar</button>
            </div>
            <div class="res" id="resumoVel">--</div>
            <div class="nt">Tres velocidades prontas cobrem o dia a dia.
            <b>Lento</b> para posicionar peca e aprender a maquina, <b>Normal</b>
            para trabalhar, <b>Rapido</b> so para deslocamento longo entre
            cordoes. O cordao anda mais devagar que o resto de proposito: e ele
            que define a penetracao.</div>
            <div id="velCustom" class="oculto">
              <div class="cp"><label>Jog normal</label><input type="number" id="inVn" min="0.1" step="0.5"><span class="un">°/s</span></div>
              <div class="cp"><label>Jog precisao</label><input type="number" id="inVp" min="0.1" step="0.1"><span class="un">°/s</span></div>
              <div class="cp"><label>Deslocamento</label><input type="number" id="inVa" min="0.1" step="0.5"><span class="un">°/s</span></div>
              <div class="cp"><label>Cordao</label><input type="number" id="inVc2" min="0.5" step="0.5"><span class="un">mm/s</span></div>
              <div class="nt">Em <b>graus por segundo</b>, nao em pulsos. Hz
              significa velocidades diferentes em cada junta: com reducao 16,5
              numa e 4 na outra, os mesmos 3000 Hz davam 6,5 °/s numa e 27 na
              outra. Em graus por segundo as duas acompanham. O joystick usa a
              velocidade de jog como teto: no centro do disco o eixo fica
              parado, na borda ele anda nessa velocidade.</div>
            </div>

            <h4>Partida e parada</h4>
            <div class="seg" id="segRampa">
              <button data-r="macia">Macia</button>
              <button data-r="media">Media</button>
              <button data-r="firme">Firme</button>
              <button data-r="custom">Ajustar</button>
            </div>
            <div class="res" id="resumoRampa">--</div>
            <div class="nt"><b>Macia</b> sai e para sem tranco: e o que se usa
            quando o braco esta perdendo posicao, porque tranco e a causa numero
            um de perda de passo. <b>Firme</b> chega antes, e util em movimento
            curto. Na duvida, <b>Media</b>.</div>
            <div id="rampaCustom" class="oculto">
              <div class="cp"><label>Aceleracao da junta 1</label><input type="number" id="inA1" min="1" step="5"><span class="un">°/s²</span></div>
              <div class="cp"><label>Aceleracao da junta 2</label><input type="number" id="inA2" min="1" step="5"><span class="un">°/s²</span></div>
              <div class="cp"><label>Suavidade da partida</label><input type="number" id="inSuav" min="0" max="255" step="10"></div>
              <div class="nt">A aceleracao esta em graus por segundo ao quadrado
              &mdash; quantos °/s o eixo ganha a cada segundo. 60 quer dizer que
              ele leva um segundo para chegar a 60 °/s. A <b>suavidade</b> e a
              rampa em S: com zero a aceleracao entra de uma vez e a partida da
              o tranco que voce sente; quanto maior, mais devagar a propria
              aceleracao cresce.</div>
            </div>

            <button class="b pri" id="btSalvar">Salvar velocidade e partida</button>
            <div class="pq2" id="qSalvar"></div>

            <h4>Ate onde o braco pode ir</h4>
            <div class="res" id="resumoArea">--</div>
            <div class="nt">Sao os travoes que recusam um movimento <b>antes</b>
            de ele acontecer. Nao mudam o que a maquina sabe fazer: mudam o que
            ela aceita fazer.
            <br><b>Fim de curso</b> &mdash; nao passa dos limites medidos na
            calibracao. <b>Cotovelo</b> &mdash; nao deixa o antebraco fechar
            sobre o braco e bater nele mesmo. <b>Mesa e base</b> &mdash; nao
            deixa a ponta descer sobre a mesa nem varrer por cima da propria
            base.</div>
            <div class="tr"><div class="ch" id="pCur"><i></i></div>
              <span>fim de curso das juntas</span></div>
            <div class="tr"><div class="ch" id="pDob"><i></i></div>
              <span>cotovelo</span></div>
            <div class="tr"><div class="ch" id="pEnv"><i></i></div>
              <span>mesa e base</span></div>
            <div class="nt av">Desligar um destes nao aumenta a area de trabalho
            &mdash; tira o aviso. O braco passa a aceitar a postura que ia bater.
            Desligue so para diagnosticar, e ligue de volta.</div>

            <h4 class="dobra" id="hAvancado">Avancado &mdash; numeros da montagem</h4>
            <div id="avancado" class="oculto">
              <div class="nt av">Daqui para baixo sao numeros de montagem. Mexer
              neles sem medir muda a escala de tudo: o desenho na tela para de
              bater com o braco, e a area util passa a proteger o lugar errado.
              A pagina <b>Calibracao</b> acha estes valores medindo, em vez de
              adivinhar.</div>

              <h4>Resolucao da junta 1</h4>
              <div class="cp"><label>Pulsos por volta do motor</label><input type="number" id="inPv1" min="1"></div>
              <div class="cp"><label>Reducao mecanica</label><input type="number" id="inRd1" min="0.01" step="0.01"><span class="un">: 1</span></div>
              <h4>Resolucao da junta 2</h4>
              <div class="cp"><label>Pulsos por volta do motor</label><input type="number" id="inPv2" min="1"></div>
              <div class="cp"><label>Reducao mecanica</label><input type="number" id="inRd2" min="0.01" step="0.01"><span class="un">: 1</span></div>
              <div class="res" id="resumoRes">--</div>

              <h4>Sentido dos eixos</h4>
              <div class="tr"><div class="ch" id="sInv1"><i></i></div>
                <span>inverter a junta 1</span></div>
              <div class="tr"><div class="ch" id="sInv2"><i></i></div>
                <span>inverter a junta 2</span></div>
              <div class="nt">Se o braco vai para um lado e o desenho na tela vai
              para o outro, o sinal do eixo esta trocado &mdash; nenhuma
              calibracao conserta isso, porque o erro nao e de escala, e de
              sinal. Marque aqui em vez de trocar fio no driver.<br>A cinematica
              espera angulo crescente no sentido <b>anti-horario</b>, com a junta
              1 em zero apontando para a <b>direita</b>.</div>

              <h4>Margens de seguranca</h4>
              <div class="cp"><label>Folga de dobra</label><input type="number" id="inDb" min="0" max="90"><span class="un">°</span></div>
              <div class="cp"><label>Y minimo</label><input type="number" id="inEy"><span class="un">mm</span></div>
              <div class="cp"><label>Raio morto</label><input type="number" id="inEr" min="0"><span class="un">mm</span></div>
              <div class="nt">Sao os numeros por tras das tres protecoes acima.
              <b>Folga de dobra</b>: quantos graus antes de o cotovelo fechar de
              todo o movimento e recusado. <b>Y minimo</b>: a linha, em
              milimetros, abaixo da qual a ponta nao desce. <b>Raio morto</b>: o
              circulo em volta do centro da base por onde o antebraco nao
              passa.</div>
              <button class="b pri" id="btSalvarGeo">Salvar margens</button>
              <div class="pq2" id="qSalvarGeo"></div>
            </div>
          </div>
        </div>
      
      
    </div>
    <div class="pane" id="cfgCalib">
      <div class="et aberta" id="etGuia">
        <div class="cab"><div class="mk"><svg class="ic"><use href="#i-mira"/></svg></div>
          <div class="tx"><div class="tt">Calibracao guiada</div>
          <span class="sb" id="sbGuia">--</span></div><div class="chv">&#9654;</div></div>
        <div class="dentro">
          <div class="nt">Quatro passos, nesta ordem. <b>A ordem importa</b>: cada
          um usa o resultado do anterior, e fazer fora de ordem obriga a refazer.
          O que ja estiver pronto aparece marcado &mdash; da para parar no meio e
          voltar depois.</div>
          <div class="guia" id="guiaLista"></div>
          <div class="res" id="guiaAgora">--</div>
          <button class="b mini" id="btGuiaSentidoOk">Ja conferi o sentido dos eixos</button>
          <div class="pq2" id="qGuiaSentidoOk"></div>
          <div class="nt">Cada passo abre o cartao que faz o trabalho, logo
          abaixo. Nada aqui e atalho: e o mesmo caminho, na ordem certa.</div>
        </div>
      </div>

      <div class="et aberta">
        <div class="cab"><div class="mk"><svg class="ic"><use href="#i-alvo"/></svg></div>
          <div class="tx"><div class="tt">Como a maquina esta agora</div>
          <span class="sb" id="sbCalib">--</span></div><div class="chv">&#9654;</div></div>
        <div class="dentro">
          <div class="grelha" id="calResumo"></div>
          <div class="nt">A resolucao de cada junta e
          <b>passos por volta &times; reducao &divide; 360</b>. Sao dois numeros, e
          cada um erra de um jeito diferente &mdash; por isso os passos 1 e 2
          abaixo medem um de cada vez.</div>
          <div class="res" id="calVivo">--</div>
          <div class="nt">Comparar o <b>comandado</b> com o <b>medido</b> depois de
          mover e a conferencia final: se os dois andarem juntos, a resolucao
          esta certa. Se o medido andar menos, a reducao declarada esta maior
          que a real (e vice-versa).</div>
        </div>
      </div>

      <div class="et">
        <div class="cab"><div class="mk">1</div>
          <div class="tx"><div class="tt">Engrenagem eletronica</div>
          <span class="sb">sem instrumento nenhum</span></div><div class="chv">&#9654;</div></div>
        <div class="dentro">
          <div class="nt">Quantos <b>passos</b> o driver precisa para dar uma volta
          no motor. E um parametro do T3D, e o numero que mais se erra: troca-se o
          driver, refaz-se um parametro, e o declarado aqui deixa de bater. O
          sintoma e o braco andar menos (ou mais) do que a tela diz.<br><br>
          O encoder mede isso <b>sozinho</b>: manda-se um tanto conhecido de
          passos e pergunta-se quantas voltas o motor deu.</div>
          <div class="cp"><label>Junta</label>
            <select id="afJ"><option value="1">junta 1</option><option value="2">junta 2</option></select></div>
          <button class="b mini" id="btAfMarcar">1 &middot; Marcar o inicio aqui</button>
          <div class="pq2" id="qAfMarcar"></div>
          <div class="nt">Agora mova o eixo com as setas, <b>bastante</b>
          &mdash; pelo menos um quarto de volta do motor.</div>
          <div class="res" id="afConta">--</div>
          <button class="b pri mini" id="btAfEnc">2 &middot; Medir passos por volta</button>
          <div class="pq2" id="qAfEnc"></div>
        </div>
      </div>

      <div class="et">
        <div class="cab"><div class="mk">2</div>
          <div class="tx"><div class="tt">Reducao mecanica</div>
          <span class="sb">precisa de uma referencia, uma so</span></div><div class="chv">&#9654;</div></div>
        <div class="dentro">
          <div class="nt">O encoder esta no eixo do <b>motor</b>, antes do
          redutor, e o angulo que ele mostra ja e calculado com a reducao
          &mdash; entao <b>nao da para tirar a reducao dele</b>: seria tirar o
          numero de uma conta que usa o proprio numero. Com um sensor so, e
          antes do redutor, a relacao do redutor e invisivel. Isso e fisica,
          nao limitacao de programa.
          <br>O que ele da de graca, e com muita precisao, e a contagem de
          <b>voltas do motor</b>. Falta <b>uma</b> referencia do lado da junta,
          e a reducao sai exata:<br>
          &nbsp;&nbsp;<b>reducao = voltas do motor &times; 360 &divide; angulo real</b>
          <br>Contar voltas reais e melhor que contar pulsos comandados: pulso
          erra junto com a engrenagem eletronica e junto com perda de passo. O
          encoder mede o eixo, nao a intencao.</div>
          <div class="cp"><label>Junta</label>
            <select id="rdJ"><option value="1">junta 1</option><option value="2">junta 2</option></select></div>
          <button class="b mini" id="btRdMarcar">1 &middot; Marcar o inicio aqui</button>
          <div class="pq2" id="qRdMarcar"></div>
          <div class="res" id="rdConta">--</div>
          <div class="nt">De onde tirar a referencia, da melhor para a pior:</div>
          <div class="tr"><b>1.</b>&nbsp;<span><b>Esquadro (90&deg;)</b> &mdash; um
          esquadro de carpinteiro da 90 graus com precisao muito boa e todo mundo
          tem um. Encoste numa face, marque, gire ate a outra, aplique 90.</span></div>
          <div class="tr"><b>2.</b>&nbsp;<span><b>Curso entre batentes</b> &mdash; o
          maior angulo disponivel, e quanto maior o angulo menor o erro relativo.
          Precisa do curso real, medido uma vez.</span></div>
          <div class="tr"><b>3.</b>&nbsp;<span><b>Volta completa</b>, se a junta der
          uma &mdash; nao precisa de instrumento: basta reconhecer que voltou ao
          mesmo lugar.</span></div>
          <div class="cp"><label>2 &middot; Angulo real</label>
            <input type="number" id="rdG" min="5" step="1" placeholder="90"><span class="un">°</span></div>
          <div class="linhaB">
            <button class="b mini" data-rdg="90">esquadro 90&deg;</button>
            <button class="b mini" data-rdg="180">meia volta</button>
            <button class="b mini" data-rdg="360">volta inteira</button>
          </div>
          <button class="b pri mini" id="btRdAplicar">3 &middot; Gravar a reducao medida</button>
          <div class="pq2" id="qRdAplicar"></div>
          <div class="nt">Depois de gravar, <b>confira</b>: mande o braco um tanto
          conhecido e veja se o medido acompanha o comandado, no quadro do topo
          desta pagina. E o que fecha o laco.</div>
        </div>
      </div>

      <div class="et">
        <div class="cab"><div class="mk">3</div>
          <div class="tx"><div class="tt">Curso das juntas</div>
          <span class="sb" id="sbCurso">--</span></div><div class="chv">&#9654;</div></div>
        <div class="dentro">
          <div class="nt">Onde cada junta bate. E o assistente que leva voce ate
          os batentes e grava os limites &mdash; e deles que sai toda protecao de
          curso.</div>
          <button class="b pri" id="btCalIni2">Abrir o assistente de calibracao</button>
          <div class="pq2" id="qCalIni2"></div>
          <button class="b mini x" id="btCalApagar2">Apagar a calibracao gravada</button>
          <div class="pq2" id="qCalApagar2"></div>
        </div>
      </div>

      <div class="et">
        <div class="cab"><div class="mk">4</div>
          <div class="tx"><div class="tt">Area da mesa</div>
          <span class="sb" id="sbMesa">--</span></div><div class="chv">&#9654;</div></div>
        <div class="dentro">
          <div class="nt">A area util deixou de ser dois numeros digitados e passou
          a ser <b>ensinada</b>: leve a ponta a cada canto da mesa e grave. O
          retangulo e a caixa que contem os cantos ensinados.<br><br>
          Dali para fora o braco <b>nao anda</b> &mdash; nem por programa, nem
          pelas setas. Se a ponta parar fora da area, so o movimento que a traz de
          volta e liberado: o braco nunca se prende do lado de fora.</div>
          <div class="perigo">Ensine os cantos com a <b>ponta</b> da tocha, nao com
          o cotovelo. A area e da ferramenta &mdash; o cotovelo passa por cima da
          mesa o tempo todo e nao solda nada.</div>
          <button class="b pri" id="btMesaCanto">Gravar canto na posicao atual</button>
          <div class="pq2" id="qMesaCanto"></div>
          <div class="res" id="mesaEstado">--</div>
          <button class="b mini x" id="btMesaLimpar">Apagar a area ensinada</button>
          <div class="pq2" id="qMesaLimpar"></div>
          <div class="nt">Sem area ensinada a maquina volta a se proteger so pelo
          <b>Y minimo</b> e pelo <b>raio morto da base</b>, que continuam valendo
          em qualquer caso &mdash; o raio da base e mecanica, nao mesa.</div>
        </div>
      </div>

    </div>
    <div class="pane" id="cfgEncoder">

        <div class="et">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-mira"/></svg></div>
            <div class="tx"><div class="tt">Correcao de posicao</div>
            <span class="sb" id="sbCorr">assentamento pelo encoder</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="encGrade">
              <div class="encCel"><span class="rot">estado</span><b id="crEst">--</b></div>
              <div class="encCel"><span class="rot">assentamentos</span><b id="crOk">--</b></div>
              <div class="encCel"><span class="rot">avisos</span><b id="crAlerta">--</b></div>
            </div>
            <div class="res" id="crMotivo">--</div>
            <div id="crTrav" style="display:none">
              <div class="res" id="crTravTxt"></div>
              <button class="b mini" id="btTravOk">Resolvido, limpar o aviso</button>
              <div class="pq2" id="qTravOk"></div>
            </div>
            <div class="nt">Quando o braco chega, o encoder diz onde ele
            <b>realmente</b> parou e o sistema da um retoque curto se precisar.
            E isto que faz <b>sair de uma posicao e voltar cair no mesmo
            lugar</b>: sem assentamento, o erro de um movimento entra no
            proximo e o desvio cresce sem nunca voltar.</div>

            <div class="tr"><div class="ch" id="crOnCh"><i></i></div>
              <span>assentar no fim de cada movimento</span></div>
            <div class="tr"><div class="ch" id="crVigCh"><i></i></div>
              <span>avisar quando o eixo sair de posicao parado</span></div>
            <div class="cp"><label>Tolerancia</label><input type="number" id="crTol" min="0.01" max="5" step="0.01"><span class="un">&deg;</span></div>
            <div class="cp"><label>Teto do retoque</label><input type="number" id="crMax" min="0.05" max="15" step="0.05"><span class="un">&deg;</span></div>
            <div class="cp"><label>Aviso de desvio</label><input type="number" id="crAlr" min="0.05" max="30" step="0.05"><span class="un">&deg;</span></div>
            <div class="cp"><label>Tentativas</label><input type="number" id="crTent" min="1" max="10" step="1"></div>
            <button class="b pri" id="btCorrSalvar">Salvar correcao</button>
            <div class="pq2" id="qCorrSalvar"></div>
            <div class="nt"><b>Tolerancia</b>: abaixo disso ja esta bom, e o
            eixo nao fica cutucando. <b>Teto do retoque</b>: acima disso o
            sistema NAO corrige, ele <b>denuncia</b> &mdash; erro de varios
            graus nao e folga, e acoplamento solto, registrador errado ou
            reducao errada, e empurrar o braco achando que esta consertando e
            o jeito mais rapido de bater a ferramenta em alguma coisa.
            <br>O retoque nunca sai do curso calibrado, nunca acontece com a
            solda ligada, e a parada de emergencia cancela ele junto com todo
            o resto.</div>
          </div>
        </div>

        <div class="et" id="etZero">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-casa"/></svg></div>
            <div class="tx"><div class="tt">Zero absoluto da maquina</div>
            <span class="sb" id="sbZero">avancado</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="res" id="zEstado">--</div>
            <div class="nt">O encoder do servo guarda a posicao com a maquina
            <b>desligada</b>. Se alguem empurrar o braco a mao com tudo apagado,
            ao ligar ele sabe onde esta. Isso dispensa fim de curso: em vez de
            procurar batente, a maquina <b>le</b> onde esta.
            <br>Para isso ela precisa saber uma coisa so: <b>qual contagem do
            encoder corresponde a 0 grau</b>. Ensina-se uma vez.</div>

            <div class="cadeado" id="zCadeado">
              <div class="ic">&#128274;</div>
              <div><b>Ajustes de origem</b>
              <span>errar aqui desloca a area util inteira &mdash; toque para abrir</span></div>
            </div>

            <div class="trancavel">
              <h4>Ensinar o zero</h4>
              <div class="nt">Leve o braco ate uma postura que voce sabe medir
              (o batente, um gabarito, o esquadro), meca o angulo <b>de
              verdade</b> e informe. Nao precisa ser 0: informe o angulo em que
              a junta esta agora, e o sistema calcula o resto.</div>
              <div class="cp"><label>Junta</label>
                <select id="zJ"><option value="1">junta 1</option><option value="2">junta 2</option></select></div>
              <div class="cp"><label>Esta agora em</label><input type="number" id="zG" step="0.1" value="0"><span class="un">&deg;</span></div>
              <button class="b pri mini" id="btZensinar">Gravar este angulo como referencia</button>
              <div class="pq2" id="qZensinar"></div>
              <button class="b mini" id="btZesquecer">Esquecer o zero absoluto</button>
              <div class="pq2" id="qZesquecer"></div>
              <div class="nt">Esquecer faz a maquina voltar a ligar como antes:
              sem se localizar, e sem ir a lugar nenhum sozinha.</div>

              <h4>Ao ligar a maquina</h4>
              <div class="tr"><div class="ch" id="zSinCh"><i></i></div>
                <span>recuperar a posicao pelo encoder</span></div>
              <div class="tr"><div class="ch" id="zIrCh"><i></i></div>
                <span>e depois ir para 0 grau</span></div>
              <div class="cp"><label>Ja considero no zero</label><input type="number" id="zTol" min="0.05" max="10" step="0.05"><span class="un">&deg;</span></div>
              <button class="b pri mini" id="btZsalvar">Salvar</button>
              <div class="pq2" id="qZsalvar"></div>
              <div class="nt"><b>O braco so anda depois que voce habilita os
              servos</b>, que e uma acao sua na tela. Enquanto ninguem habilitar,
              ele nao tem como se mexer &mdash; por mais que esta chave esteja
              ligada. E se o zero estiver fora do curso calibrado, ele nao vai:
              furar a protecao seria pior que nao ir.</div>
            </div>
          </div>
        </div>

        <div class="et">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-plug"/></svg></div>
            <div class="tx"><div class="tt">Ligacao Modbus</div>
            <span class="sb">endereco, registrador, formato</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="tr"><div class="ch" id="encAtivo"><i></i></div>
              <span>ler o encoder pelos drivers</span></div>
            <h4>Barramento</h4>
            <div class="cp"><label>Velocidade</label><input type="number" id="encBaud" min="1200"><span class="un">bps</span></div>
            <div class="cp"><label>Paridade</label>
              <select id="encPar"><option value="0">8N1</option><option value="1">8E1</option><option value="2">8O1</option></select></div>
            <div class="cp"><label>Funcao Modbus</label>
              <select id="encFunc"><option value="3">3 &middot; holding</option><option value="4">4 &middot; input</option></select></div>
            <div class="cp"><label>Periodo de leitura</label><input type="number" id="encPer" min="20" max="2000" step="10"><span class="un">ms</span></div>
            <h4>Junta 1</h4>
            <div class="cp"><label>Endereco do driver</label><input type="number" id="encId1" min="1" max="247"></div>
            <div class="cp"><label>Registrador da posicao</label><input type="number" id="encReg1" min="0" max="65535"></div>
            <div class="cp"><label>Contagens por volta</label><input type="number" id="encCv1" min="1"></div>
            <h4>Junta 2</h4>
            <div class="cp"><label>Endereco do driver</label><input type="number" id="encId2" min="1" max="247"></div>
            <div class="cp"><label>Registrador da posicao</label><input type="number" id="encReg2" min="0" max="65535"></div>
            <div class="cp"><label>Contagens por volta</label><input type="number" id="encCv2" min="1"></div>
            <h4 class="dobra">Habilita (SON)</h4>
            <div class="sub">
            <div class="nt">O habilita dos servos vai por este mesmo barramento.
            O registrador nao se adivinha: ache com <b>ferramentas/teste_rs485</b>
            (modos <b>d</b>, <b>d2</b>, <b>s</b>) e grave o numero aqui.
            Escrever em parametro errado de um servo drive troca engrenagem
            eletronica, modo de controle ou limite de torque &mdash; e isso nao
            se desfaz por esta tela.</div>
            <div class="cp"><label>Registrador do habilita</label><input type="number" id="sonReg" min="0" max="65535"></div>
            <div class="cp"><label>Valor que habilita</label><input type="number" id="sonL" min="0" max="65535"></div>
            <div class="cp"><label>Valor que desabilita</label><input type="number" id="sonD" min="0" max="65535"></div>
            <div class="tr"><div class="ch" id="sonF16"><i></i></div>
              <span>escrever pela funcao 16 (em vez da 06)</span></div>
            <div class="nt">Ha driver que recusa a funcao 06 mesmo para um
            registrador so. Se a escrita nao confirmar, marque aqui.</div>
            <button class="b pri" id="btSonSalvar">Salvar habilita</button>
            <div class="pq2" id="qSonSalvar"></div>
            <div class="nt">Registrador <b>0</b> significa <b>nao configurado</b>:
            nesse estado a maquina recusa habilitar e diz o motivo, em vez de
            escrever num endereco chutado.</div>
            </div>

            <h4>Formato do valor</h4>
            <div class="tr"><div class="ch" id="enc32"><i></i></div>
              <span>posicao em 32 bits (dois registradores)</span></div>
            <div class="tr"><div class="ch" id="encLo"><i></i></div>
              <span>palavra baixa vem primeiro</span></div>
            <div class="nt">Muito driver Modbus manda a palavra baixa antes da
            alta. Errar isto faz a posicao dar saltos de dezenas de milhares em
            vez de crescer suave &mdash; se for o que voce ve, marque aqui.</div>
            <button class="b pri" id="btEncSalvar">Salvar ligacao</button>
            <div class="pq2" id="qEncSalvar"></div>
            <button class="b mini" id="btEncPadroes">Voltar aos padroes medidos</button>
            <div class="pq2" id="qEncPadroes"></div>
            <div class="nt">Configuracao salva por uma <b>versao anterior</b> do
            firmware continua valendo depois de atualizar &mdash; o que esta
            gravado ganha do padrao novo. Se voce atualizou e a leitura parou,
            este botao e o primeiro a tentar: ele volta tudo para o que foi
            medido nesta maquina (19200 8N1, funcao 4, registrador 5, palavra
            baixa primeiro, 131072 contagens).</div>
            <div class="nt"><b>Registrador 0 quase nunca e a posicao.</b> Nos
            drivers T3D a faixa baixa e a tabela de parametros. A posicao costuma
            estar mais acima; use <code>ferramentas/teste_rs485</code> para achar,
            ou tente um endereco aqui e olhe o grafico &mdash; o certo acompanha o
            eixo quando voce move o braco.</div>
          </div>
        </div>

    </div>
    <div class="pane" id="cfgSistema">
      
        <div class="et aberta">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-escudo"/></svg></div>
            <div class="tx"><div class="tt">Saude da maquina</div>
            <span class="sb" id="sbSaude">--</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="grelha" id="saudeG"></div>
            <div class="nt">Estes numeros respondem "esta tudo bem?" sem cabo
            nenhum. A <b>taxa</b> do encoder e a que mais diz: 100% e barramento
            saudavel; 60% nao e "meio quebrado", e cabo, terminacao ou
            aterramento &mdash; e vai piorar.</div>
            <button class="b mini" id="btManut">Registrar manutencao feita</button>
            <div class="pq2" id="qManut"></div>
            <div class="nt">Zera o contador de ciclos desde a ultima
            manutencao. O total da maquina continua contando.</div>
          </div>
        </div>

        <div class="et">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-lista"/></svg></div>
            <div class="tx"><div class="tt">Registro de eventos</div>
            <span class="sb">o que a maquina fez nas ultimas horas</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="res" id="regLista">--</div>
            <div class="nt">Sai da memoria da maquina, entao funciona sem
            cartao &mdash; que e justamente quando isto costuma ser consultado.
            Com cartao, o registro completo fica em <b>/log/</b>.</div>
          </div>
        </div>

        <div class="et">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-qr"/></svg></div>
            <div class="tx"><div class="tt">Conectar no painel</div>
            <span class="sb">aponte a camera do celular</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="qrPar">
              <div class="qrCx"><canvas id="qrRede" width="180" height="180"></canvas>
                <div class="qrLg">1. entrar na rede</div></div>
              <div class="qrCx"><canvas id="qrPainel" width="180" height="180"></canvas>
                <div class="qrLg">2. abrir o painel</div></div>
            </div>
            <div class="nt">O primeiro codigo entra na rede Wi-Fi da maquina; o
            segundo abre esta tela. Na maioria dos celulares o painel abre
            sozinho ao entrar na rede &mdash; o segundo codigo e para quando
            nao abre.</div>
          </div>
        </div>

        <div class="et">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-cima"/></svg></div>
            <div class="tx"><div class="tt">Atualizar o firmware</div>
            <span class="sb" id="sbOta">--</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="perigo">A maquina reinicia ao terminar. Nao atualize com
            peca no meio de um cordao.</div>
            <div id="otaCaixa">
              <input type="file" id="otaArq" accept=".bin">
              <button class="b pri" id="btOta">Enviar firmware</button>
              <div class="pq2" id="qOta"></div>
              <div class="pgr"><i id="otaBarra"></i></div>
            </div>
            <div class="nt" id="otaNota"></div>
          </div>
        </div>

        <div class="et">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-info"/></svg></div>
            <div class="tx"><div class="tt">Idioma / Language</div>
            <span class="sb" id="sbIdioma">portugues</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <button class="b mini" id="btIdioma">English</button>
            <div class="pq2" id="qIdioma"></div>
            <div class="nt">Portugues e ingles, e so. O padrao e portugues.
            Traduz o que o operador toca: abas, botoes, rotulos, a tela de
            saude e a tira de estado. <b>As notas longas de explicacao
            continuam em portugues</b> &mdash; elas sao o manual embutido desta
            maquina, escritas para quem a monta, e traduzir mal um texto que
            explica por que o arco fecha na pausa e pior do que deixa-lo como
            esta.</div>
          </div>
        </div>

        <div class="et">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-disco"/></svg></div>
            <div class="tx"><div class="tt">Copia da configuracao no cartao</div>
            <span class="sb" id="sbCfgCartao">--</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt">Tudo que a maquina tem de configuracao &mdash;
            calibracao, curso das juntas, reducao, area da mesa, zero, encoder,
            velocidades &mdash; <b>se copia sozinho para o cartao</b>, num
            arquivo reservado. A maquina continua lendo a memoria interna ao
            ligar; o cartao existe para o dia em que ela se perde: placa
            trocada, firmware regravado, "apagar tudo" apertado sem querer.
            <br>Nao e ponto de restauracao: e um espelho do estado atual.
            Calibracao refeita errado e espelhada errada.</div>
            <button class="b mini" id="btCfgRestaurar">Restaurar do cartao</button>
            <div class="pq2" id="qCfgRestaurar"></div>
            <div class="nt">Programas e trajetorias sao outra coisa: ficam na
            aba Arquivos e <b>nao</b> sao tocados nem por isto nem por "apagar
            tudo".</div>
          </div>
        </div>

        <div class="et zPerigo">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-lixo"/></svg></div>
            <div class="tx"><div class="tt">Apagar tudo</div>
            <span class="sb">a maquina volta a ser recem-montada</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt">Ha dois botoes aqui, e eles nao fazem a mesma
            coisa. O de cima e o que quase sempre se quer.</div>

            <h4>Restaurar os padroes</h4>
            <button class="b mini" id="btReset">Restaurar padroes</button>
            <div class="pq2" id="qReset"></div>
            <div class="nt">Devolve <b>parametros</b> de fabrica: velocidades,
            aceleracao, resolucao, medidas dos elos, protecoes. <b>Nao</b> mexe
            na calibracao, na mesa ensinada nem no zero absoluto &mdash; quem so
            queria as velocidades de volta nao perde a instalacao.</div>

            <h4>Apagar tudo</h4>
            <div class="perigo"><b>Isto apaga a instalacao inteira</b> e
            reinicia a maquina: calibracao das juntas, area da mesa ensinada,
            zero absoluto, ligacao do encoder, contadores de producao e
            manutencao &mdash; tudo que esta gravado na memoria interna. Depois
            disso o braco precisa ser calibrado de novo antes de trabalhar.
            <br><b>O cartao SD nao e tocado.</b> As pecas salvas sao trabalho
            seu, nao configuracao da maquina; apagar programa continua sendo na
            aba Arquivos, um a um.</div>
            <div class="cp"><label>Digite APAGAR</label>
              <input type="text" id="apgConf" placeholder="APAGAR" autocomplete="off"></div>
            <button class="b mini perigoso" id="btApagarTudo">Apagar tudo e reiniciar</button>
            <div class="pq2" id="qApagarTudo"></div>
            <div class="nt">A palavra e pedida porque toque errado na tela
            acontece &mdash; digitar APAGAR sem querer, nao. So funciona com o
            robo parado no modo manual.</div>
          </div>
        </div>

    </div>
  </div>
</div></div>

<div class="veu" id="veu"><div class="cx">
  <h2>Calibracao das juntas</h2>
  <div class="pp" id="cPasso">--</div>
  <div class="pgr"><i id="cBarra"></i></div>
  <div class="ins" id="cInstr"></div>

  <!-- Aparece so nas etapas em que os numeros significam alguma coisa:
       o angulo da referencia no HOME e o curso realmente medido no fim. -->
  <div id="cMed" style="display:none">
    <div class="cp"><label id="cMedL1">Junta 1</label>
      <input type="number" id="cG1" step="0.1"><span class="un">°</span></div>
    <div class="cp"><label id="cMedL2">Junta 2</label>
      <input type="number" id="cG2" step="0.1"><span class="un">°</span></div>
    <div class="nt" id="cMedNota"></div>
  </div>
  <div class="eixo" id="cJ1">
    <button class="jb" data-j="1" data-d="1" title="anti-horario">&#8634;</button>
    <div class="id"><span class="rot">junta 1</span></div>
    <button class="jb" data-j="1" data-d="-1" title="horario">&#8635;</button>
  </div>
  <div class="eixo" id="cJ2">
    <button class="jb" data-j="2" data-d="1" title="anti-horario">&#8634;</button>
    <div class="id"><span class="rot">junta 2</span></div>
    <button class="jb" data-j="2" data-d="-1" title="horario">&#8635;</button>
  </div>

  <!-- So na etapa de referencia: e aqui que o operador descobre que o
       braco gira ao contrario, e mandar cancelar para consertar era
       pedir para ele desistir do assistente. Depois desta etapa ja ha
       limite medido, e trocar o sinal inverteria o que foi medido. -->
  <div id="cSent" style="display:none">
    <div class="nt"><b>Confira o sentido antes de medir.</b> Aperte
    &#8635; (horario) em cada junta e veja para que lado ela vai de
    verdade. Se for ao contrario, marque aqui.</div>
    <div class="tr"><div class="ch" id="cInv1"><i></i></div>
      <span>inverter a junta 1</span></div>
    <div class="tr"><div class="ch" id="cInv2"><i></i></div>
      <span>inverter a junta 2</span></div>
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
/* Ha algum eixo sendo comandado agora -- pelas setas ou pelo joystick.
   Quem pergunta e o tick(), para nao roubar a vez do heartbeat. */
function jogando(){
  for(const k in tm) if(tm[k]) return true;
  return joyAtivo === true;
}
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
/* Setas do teclado no mesmo sentido dos botoes: esquerda = anti-horario. */
const TK={ArrowLeft:["1","1"],ArrowRight:["1","-1"],ArrowUp:["2","1"],ArrowDown:["2","-1"]};
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
  /* As etapas moram em lugares diferentes: abrir a etapa 2 sem trazer a
     tela junto deixaria o operador olhando para algo que nao mudou.
     As etapas 1 e 5+ sao de instalacao e agora vivem na gaveta da
     engrenagem, nao mais numa aba. */
  if(n<=1||n>=5){ abrirCfg(); irCfg("maquina"); }
  else            irAba("prog");
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
/* Uma chave por eixo. Nao alterna sozinha ao clique: quem manda no
   estado dela e o que o driver respondeu, e mudar na hora do toque
   mostraria "ligado" com o barramento ainda calado. */
[1,2].forEach(function(k){
  $("chSrv"+k).onclick=function(){
    const ligado=(k===1)?D.srv1:D.srv2;
    post("/api/servos?v="+(ligado?0:1)+"&j="+k);
  };
});

/* Um botao por eixo, porque cada driver e um escravo Modbus proprio: com
   um driver so na bancada, exigir os dois nao habilitava nada. E o mesmo
   comando do botao de Ajustes, so com a junta dita -- dois caminhos
   diferentes para ligar o motor acabam discordando. */
[1,2].forEach(function(k){
  $("btMotor"+k).onclick=function(){
    const ligado=(k===1)?D.srv1:D.srv2;
    post("/api/servos?v="+(ligado?0:1)+"&j="+k);
  };
});
$("btPrec").onclick  =function(){post("/api/precisao?v=-1");};
$("btTeste").onclick =function(){post("/api/teste/rele");};
/* Sentido do eixo: uma rota so, chamada dos dois lugares onde o assunto
   aparece -- Ajustes e a etapa de referencia da calibracao. Duas telas,
   um conceito, um caminho. */
function inverterEixo(j,v){
  return post("/api/sentido?j="+j+"&v="+(v?1:0));
}
$("sInv1").onclick=function(){inverterEixo(1,!D.inv1);};
$("sInv2").onclick=function(){inverterEixo(2,!D.inv2);};
$("cInv1").onclick=function(){inverterEixo(1,!D.inv1);};
$("cInv2").onclick=function(){inverterEixo(2,!D.inv2);};
$("cOk").onclick=function(){
  /* Os dois campos so sao enviados nas etapas em que eles significam
     alguma coisa; vazio vira 0, que o firmware entende como "nao mexer". */
  const usa=$("cMed").style.display!=="none";
  const g1=usa?(parseFloat($("cG1").value)||0):0;
  const g2=usa?(parseFloat($("cG2").value)||0):0;
  post("/api/calib/confirmar?g1="+g1+"&g2="+g2);
};
$("cNao").onclick    =function(){post("/api/calib/cancelar");};
$("btGravar").onclick=function(){post("/api/ponto/gravar").then(lerPontos);};
/* ---------- aprendizado guiado ----------
   Os tres passos sao o mesmo caminho que ja existia -- entrar, gravar,
   sair -- so que na ordem em que acontecem e no mesmo cartao. O botao
   de gravar morava na aba Mover: ensinar um cordao obrigava a trocar de
   aba entre cada ponto, com a mao no braco. */
$("btApr").onclick=function(){post("/api/aprender?on="+(D.apr?0:1),"qApr");};
$("btAprMarcar").onclick=function(){
  post("/api/ponto/gravar","qAprMarcar").then(lerPontos);
};
$("btAprFim").onclick=function(){post("/api/aprender?on=0","qAprFim");};

/* A lista de passos: qual esta valendo agora, em palavras. Mesmo formato
   da calibracao guiada, porque e o mesmo tipo de conversa. */
function aprGuiaPintar(d){
  const el=$("aprGuia"); if(!el) return;
  const solto=!!d.aprSolto, ativo=!!d.apr, n=d.aprN||0;
  const passos=[
    {t:tr("Soltar o braco"),
     ok:ativo,
     ag:!ativo,
     q:ativo?(solto?tr("solto: leve a ponta com a mao")
                   :tr("com torque: posicione pelas setas"))
            :tr("toque em Soltar o braco")},
    {t:tr("Marcar os pontos"),
     ok:ativo&&n>=2,
     ag:ativo&&n<2,
     q:!ativo?tr("depois de soltar")
        :n===0?tr("leve a ponta ao inicio do cordao e marque")
        :n===1?tr("leve ao fim do cordao e marque")
              :(n+" "+tr("pontos marcados"))},
    {t:tr("Encerrar"),
     ok:!ativo&&n>=2,
     ag:ativo&&n>=2,
     q:ativo?tr("o torque nao volta sozinho"):tr("--")}
  ];
  el.innerHTML=passos.map(function(p,i){
    return '<div class="gp'+(p.ok?" ok":(p.ag?" agora":""))+'">'+
           '<div class="n">'+(p.ok?"\u2713":(i+1))+'</div>'+
           '<div class="tt2">'+p.t+'<small>'+p.q+'</small></div></div>';
  }).join("");
  /* Marcar so faz sentido dentro do modo. Botao apagado e mudo e a
     reclamacao mais antiga deste painel, entao vai por acao(). */
  acao("AprMarcar", ativo?"":tr("entre no modo aprendizado primeiro"));
  acao("AprFim",    ativo?"":tr("o modo nao esta ligado"));
}
$("btLimpar").onclick=function(){
  if(confirm("Apagar todos os pontos do programa?"))post("/api/prog/limpar").then(lerPontos);};
$("btEnsaio").onclick=function(){
  if(D.modo==="EXECUTANDO")post("/api/prog/parar");
  else post("/api/prog/executar?ensaio=1");};
/* Abrir o arco pede DOIS toques. O primeiro arma o botao por 4 segundos
   e ele muda de texto; o segundo executa. Nao e cerimonia: "executar com
   arco" fica a um toque de distancia de "executar ensaio", numa tela que
   se usa de luva, e os dois botoes ficam um embaixo do outro.

   A confirmacao vai TAMBEM na requisicao (conf=1). A tela pedir dois
   toques nao protege nada se a rota abrir o arco para qualquer chamada
   -- e ela e alcancavel por qualquer coisa na rede da maquina. */
let arcoArmado=0;
$("btSoldar").onclick=function(){
  if(D.modo==="EXECUTANDO"){post("/api/prog/parar");return;}
  if(Date.now()>arcoArmado){
    arcoArmado=Date.now()+4000;
    pintarSoldar();
    setTimeout(function(){if(Date.now()>arcoArmado)pintarSoldar();},4100);
    return;
  }
  arcoArmado=0;
  post("/api/prog/executar?ensaio=0&conf=1");
  pintarSoldar();
};
function pintarSoldar(){
  const b=$("btSoldar"), rodando=(D.modo==="EXECUTANDO");
  const armado=Date.now()<arcoArmado;
  b.textContent = tr(rodando ? "PARAR"
    : armado ? "Confirmar: abrir o arco" : "Executar com arco");
  b.className = "b "+(rodando?"rod":"quente")+(armado?" armado":"");
}

/* Pausar e retomar. O mesmo botao, porque e a mesma decisao. */
$("btPausa").onclick=function(){
  post("/api/prog/pausar?on="+(D.pausa?0:1));};

$("btRepetir").onclick=function(){
  if(!confirm("Repetir o programa com o ARCO ABERTO?\n\nPeca nova posicionada?"))return;
  post("/api/prog/repetir?conf=1");};

$("btDesf").onclick=function(){post("/api/prog/desfazer").then(lerPontos);};

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
    "&ppv2="+$("inPv2").value+"&red2="+$("inRd2").value+
    "&suav="+$("inSuav").value);
}
$("btSalvar").onclick=function(){salvar($("inVc2").value);};

/* =====================================================================
   AJUSTES EM LINGUAGEM DE OPERADOR
   Os numeros continuam todos aqui -- nada foi tirado da maquina. O que
   mudou e a ordem em que aparecem: primeiro tres botoes que qualquer um
   entende, e os graus por segundo ao quadrado atras de "Ajustar".
   Quem monta a maquina mexe nos numeros; quem trabalha nela escolhe
   Lento, Normal ou Rapido e vai fazer peca.
   ===================================================================== */
const PRE_VEL={
  /* velN, velP, velA, cordao */
  lento: [ 8, 1.5,  6, 3],
  normal:[20, 2.0, 12, 5],
  rapido:[35, 3.0, 25, 8]
};
const PRE_RAMPA={
  /* acel, suavidade */
  macia:[ 30, 200],
  media:[ 60, 120],
  firme:[140,  40]
};
/* Reconhece o ajuste atual como um dos prontos. Sem isto a tela abriria
   com nenhum botao aceso e o operador nao saberia em que velocidade a
   maquina esta. */
function qualPreset(tab,vals){
  const perto=function(a,b){return Math.abs(a-b)<0.051;};
  for(const k in tab){
    if(tab[k].every(function(v,i){return perto(v,vals[i]);}))return k;
  }
  return "custom";
}
function mostrarPreset(seg,cx,qual){
  document.querySelectorAll("#"+seg+" button").forEach(function(b){
    b.classList.toggle("on",b.dataset.v===qual||b.dataset.r===qual);});
  $(cx).className=(qual==="custom")?"":"oculto";
}
document.querySelectorAll("#segVel button").forEach(function(b){
  b.onclick=function(){
    const q=b.dataset.v;
    mostrarPreset("segVel","velCustom",q);
    if(q==="custom")return;
    const v=PRE_VEL[q];
    $("inVn").value=v[0];$("inVp").value=v[1];$("inVa").value=v[2];
    $("inVc2").value=v[3];$("inVc").value=v[3];
    salvar(v[3]).then(function(){carregou=false;});
  };
});
document.querySelectorAll("#segRampa button").forEach(function(b){
  b.onclick=function(){
    const q=b.dataset.r;
    mostrarPreset("segRampa","rampaCustom",q);
    if(q==="custom")return;
    const v=PRE_RAMPA[q];
    $("inA1").value=v[0];$("inA2").value=v[0];$("inSuav").value=v[1];
    salvar($("inVc2").value).then(function(){carregou=false;});
  };
});
$("hAvancado").onclick=function(){
  const abrir=$("avancado").classList.contains("oculto");
  $("avancado").className=abrir?"":"oculto";
  $("hAvancado").classList.toggle("aberto",abrir);
};
$("inVc").onchange   =function(){salvar($("inVc").value);$("inVc2").value=$("inVc").value;};
function prot(){
  return post("/api/protecoes?curso="+(D.protCurso?1:0)+
    "&dobra="+(D.protDobra?1:0)+"&envelope="+(D.protEnv?1:0));
}
$("pCur").onclick=function(){D.protCurso=!D.protCurso;prot();};
$("pDob").onclick=function(){D.protDobra=!D.protDobra;prot();};
$("pEnv").onclick=function(){D.protEnv=!D.protEnv;prot();};


/* =====================================================================
   ABA CALIBRACAO
   Junta num lugar so o que estava espalhado: resolucao medida pelo
   encoder, reducao medida contra uma referencia, curso das juntas e a
   area da mesa ensinada pelos cantos.
   ===================================================================== */
let calSeq = 0;

function calibAtualizar(){
  fetch("/api/calibracao").then(function(r){return r.json();}).then(function(j){
    const linha=function(rot,val){
      return '<div class="sl"><span>'+rot+'</span><b>'+val+'</b></div>';};
    let h="";
    for(const k of [1,2]){
      const cal=j["cal"+k], ppv=j["ppv"+k], red=j["red"+k], ppg=j["ppg"+k];
      const g0=j["g"+k+"min"], g1=j["g"+k+"max"];
      h+=linha("Junta "+k+" · passos por volta", ppv);
      h+=linha("Junta "+k+" · reducao", red.toFixed(3)+" : 1");
      h+=linha("Junta "+k+" · resolucao", ppg.toFixed(2)+" passos/°");
      h+=linha("Junta "+k+" · curso",
               cal ? (g0.toFixed(1)+"° a "+g1.toFixed(1)+"°") : "nao calibrada");
    }
    h+=linha("Area da mesa", j.mesaOn
      ? ("X "+j.mesaX0.toFixed(0)+" a "+j.mesaX1.toFixed(0)+
         " · Y "+j.mesaY0.toFixed(0)+" a "+j.mesaY1.toFixed(0)+" mm")
      : (j.mesaN>0 ? (j.mesaN+" canto(s), area ainda pequena demais")
                   : "nao ensinada"));
    h+=linha("Y minimo · raio da base",
             j.envY.toFixed(0)+" mm · "+j.envR.toFixed(0)+" mm");
    $("calResumo").innerHTML=h;

    $("sbCalib").textContent = (j.cal1&&j.cal2)
      ? ("resolucao "+j.ppg1.toFixed(1)+" e "+j.ppg2.toFixed(1)+" passos/°")
      : "juntas ainda nao calibradas";
    $("sbCurso").textContent = (j.cal1&&j.cal2)
      ? (j.g1min.toFixed(0)+"…"+j.g1max.toFixed(0)+"° · "+
         j.g2min.toFixed(0)+"…"+j.g2max.toFixed(0)+"°")
      : "nao calibrado";
    $("sbMesa").textContent = j.mesaOn
      ? (Math.round(j.mesaX1-j.mesaX0)+" x "+Math.round(j.mesaY1-j.mesaY0)+" mm")
      : (j.mesaN>0 ? (j.mesaN+" canto(s)") : "nao ensinada");
    $("mesaEstado").textContent = j.mesaOn
      ? ("mesa ensinada com "+j.mesaN+" canto(s): X de "+j.mesaX0.toFixed(0)+
         " a "+j.mesaX1.toFixed(0)+" mm, Y de "+j.mesaY0.toFixed(0)+
         " a "+j.mesaY1.toFixed(0)+" mm")
      : (j.mesaN>0
         ? ("so "+j.mesaN+" canto(s), e a area ainda e uma risca. Grave um canto "+
            "bem afastado dos outros")
         : "nenhum canto ensinado ainda");

    /* Contagem ao vivo das duas medicoes. Ver o numero subir e o que
       mostra que a medida esta acontecendo. */
    const jA=+($("afJ")||{value:1}).value, jR=+($("rdJ")||{value:1}).value;
    const conta=function(n){
      if(!j["marca"+n]) return "marque o inicio para comecar a contar";
      return j["passos"+n]+" passos comandados  ·  "+
             j["voltas"+n].toFixed(4)+" volta(s) do motor pelo encoder"+
             (j["enc"+n]?"":"  ·  SEM LEITURA CONFIAVEL");
    };
    if($("afConta")) $("afConta").textContent=conta(jA);
    if($("rdConta")) $("rdConta").textContent=conta(jR);

    guiaPintar(j);

    /* Comandado x medido: a conferencia final. */
    const cmp=function(n){
      const c=(n===1?D.t1:D.t2)||0, m=(n===1?D.m1:D.m2)||0;
      if(!(n===1?D.m1ok:D.m2ok)) return "junta "+n+": sem leitura";
      return "junta "+n+": comandado "+c.toFixed(2)+"°  medido "+m.toFixed(2)+
             "°  ("+(m-c>=0?"+":"")+(m-c).toFixed(2)+"°)";
    };
    $("calVivo").textContent=cmp(1)+"\n"+cmp(2);
  }).catch(function(){});
}

/* Enquanto a aba estiver aberta, o quadro se atualiza sozinho: quem esta
   girando o eixo precisa ver a contagem andar. */
setInterval(function(){
  if($("veuCfg")&&$("veuCfg").classList.contains("on")&&cfgAtual==="calib")
    calibAtualizar();
}, 500);

/* =====================================================================
   CALIBRACAO GUIADA
   O pedido: "necessito apenas encontrar a posicao [de referencia], poder
   se mover pela mesa, e saber a reducao mecanica de cada eixo para o
   desenho ser preciso".
   Sao quatro passos, e a ORDEM importa -- cada um usa o anterior:
     1. sentido   o eixo gira para o lado que a cinematica espera?
                  Se nao, todo o resto e medido ao contrario.
     2. reducao   quantas voltas do motor cabem num grau da junta. Sem
                  ela o desenho na tela nao bate com o braco.
     3. curso     ate onde cada junta vai. Depende da reducao: o curso e
                  medido em graus.
     4. mesa      onde a peca fica. Depende do curso: os cantos sao
                  gravados com a ponta, e a ponta so vai aonde o curso
                  deixa.
   Este cartao nao refaz nenhum deles: ele diz qual e o proximo e abre o
   cartao que faz o trabalho. Duplicar a acao aqui seria duplicar a
   regra, e regra duplicada e regra que diverge.
   ===================================================================== */
const GUIA=[
  {id:"sentido", n:"Sentido dos eixos",
   por:"o braco tem de girar para o lado que o desenho mostra",
   alvo:"sInv1"},
  {id:"reducao", n:"Reducao de cada eixo",
   por:"e ela que faz o desenho bater com o braco",
   alvo:"btRdMarcar"},
  {id:"curso",   n:"Curso das juntas",
   por:"ate onde cada junta pode ir",
   alvo:"btCalIni2"},
  {id:"mesa",    n:"Area da mesa",
   por:"onde a peca fica -- fora dela o braco nao se move",
   alvo:"btMesaCanto"}
];
/* O sentido nao tem "pronto" gravado: nao ha o que medir, so o que
   conferir. Fica marcado quando o operador diz que conferiu, e isso vive
   no navegador -- e uma nota para ele mesmo, nao configuracao da
   maquina. */
function guiaSentidoOk(){
  try{ return localStorage.getItem("guiaSentido")==="1"; }catch(e){ return false; }
}
function guiaPintar(j){
  const feito={
    sentido: guiaSentidoOk(),
    /* Reducao 1,000 e o valor de fabrica: enquanto os dois eixos
       estiverem nele, ninguem mediu nada. */
    reducao: (Math.abs(j.red1-1)>0.001)||(Math.abs(j.red2-1)>0.001),
    curso:   !!(j.cal1&&j.cal2),
    mesa:    !!j.mesaOn
  };
  let prox=null;
  let h="";
  GUIA.forEach(function(p,i){
    const ok=feito[p.id];
    if(!ok&&!prox)prox=p;
    h+='<div class="gp'+(ok?" ok":(prox===p?" agora":""))+'" data-guia="'+p.id+'">'+
       '<div class="n">'+(ok?"\u2713":(i+1))+'</div>'+
       '<div class="tt2">'+p.n+'<small>'+(ok?"pronto":p.por)+'</small></div></div>';
  });
  $("guiaLista").innerHTML=h;
  $("guiaLista").querySelectorAll("[data-guia]").forEach(function(e){
    e.onclick=function(){guiaIr(e.dataset.guia);};});

  $("guiaAgora").textContent = prox
    ? ("proximo passo: "+prox.n+" \u2014 "+prox.por)
    : "os quatro passos estao prontos. O desenho na tela representa o braco de verdade.";
  $("sbGuia").textContent = prox
    ? (GUIA.filter(function(p){return feito[p.id];}).length+" de 4 prontos")
    : "calibrada";
}
/* Abrir o cartao que faz o trabalho e levar o olho ate ele. */
function guiaIr(id){
  const p=GUIA.filter(function(x){return x.id===id;})[0];
  if(!p)return;
  const el=$(p.alvo);
  if(!el)return;
  if(id==="sentido"){
    const av=$("avancado");
    /* O sentido mora no "Avancado" da pagina Maquina: abrir a gaveta na
       pagina certa e revelar o bloco, senao o passo aponta para o nada. */
    irCfg("maquina");
    const et=el.closest(".et");
    if(et)et.classList.add("aberta");
    if(av&&av.classList.contains("oculto"))$("hAvancado").click();
  }else{
    const et=el.closest(".et");
    if(et)et.classList.add("aberta");
  }
  setTimeout(function(){
    el.scrollIntoView({behavior:"smooth",block:"center"});
  },80);
}
$("btGuiaSentidoOk").onclick=function(){
  try{localStorage.setItem("guiaSentido","1");}catch(e){}
  acao("GuiaSentidoOk","sentido conferido.");
  calibAtualizar();
};

$("btRdMarcar").onclick=function(){
  post("/api/aferir/marcar?j="+$("rdJ").value).then(calibAtualizar);};
$("btRdAplicar").onclick=function(){
  const g=parseFloat($("rdG").value);
  if(!(g>=5)){acao("RdAplicar","informe o angulo real: 90 do esquadro, ou o curso");return;}
  acao("RdAplicar","");
  post("/api/aferir/reducao?j="+$("rdJ").value+"&g="+g).then(calibAtualizar);};
document.querySelectorAll("[data-rdg]").forEach(function(b){
  b.onclick=function(){$("rdG").value=b.dataset.rdg;};});

$("btMesaCanto").onclick=function(){
  post("/api/mesa/canto").then(calibAtualizar);};
$("btMesaLimpar").onclick=function(){
  if(!confirm("Apagar a area da mesa ensinada?\n\nO braco volta a se proteger so pelo Y minimo e pelo raio da base."))return;
  post("/api/mesa/limpar").then(calibAtualizar);};

/* A calibracao guiada e o UNICO caminho. Havia um segundo bloco em
   Ajustes que abria o mesmo assistente, e ter dois lugares para a mesma
   coisa so faz o operador perguntar qual e a diferenca -- nao havia
   nenhuma. Aqui e onde ela mora, na ordem em que os passos acontecem. */
$("btCalIni2").onclick=function(){ post("/api/calib/iniciar","qCalIni2"); };
$("btCalApagar2").onclick=function(){
  if(confirm(tr("Apagar a calibracao gravada?")+"\n\n"+
             tr("O robo volta ao modo de instalacao: jog livre e sem limite de "+
                "curso, programa e trajetoria recusados ate calibrar de novo.")))
    post("/api/calib/apagar","qCalApagar2");
};

/* ---------- zerar aqui e aferir a reducao ---------- */
/* Os pulsos contados desde a marca aparecem em tempo real: sem isso o
   operador nao tem como saber se a marca pegou. E o botao de gravar so
   liga quando ha marca E graus digitados -- apertar e nao acontecer nada
   e o mesmo defeito de sempre, com outro nome. */
/* Os dois botoes de medida so ligam quando ha marca: apertar e nao
   acontecer nada e o defeito mais antigo deste painel. A contagem ao
   vivo (passos e voltas do motor) vem de /api/calibracao, em
   calibAtualizar() -- aqui fica so o estado dos botoes. */
function afEstado(){
  const aj=+$("afJ").value, ap=(aj===2?D.afer2:D.afer1)||0;
  const rj=+$("rdJ").value, rp=(rj===2?D.afer2:D.afer1)||0;
  acao("AfEnc", !D.modo ? "sem contato com o robo"
      : ap===0 ? "marque o inicio e gire o eixo primeiro"
      : porQueNaoMove(D,false));
  acao("RdAplicar", !D.modo ? "sem contato com o robo"
      : rp===0 ? "marque o inicio e gire o eixo primeiro"
      : !(parseFloat($("rdG").value)>=5) ? "informe o angulo real de referencia"
      : porQueNaoMove(D,false));
  acao("MesaCanto", !D.modo ? "sem contato com o robo"
      : (!D.cal1||!D.cal2) ? "calibre as juntas antes de ensinar a mesa"
      : D.modo!=="MANUAL" ? "ensine a mesa com o robo parado no manual"
      : D.movendo ? "espere o braco parar" : "");
}
afEstado();

$("btRefer").onclick=function(){
  if(confirm("Declarar que o braco esta AGORA na posicao de referencia?\n\n"+
             "Os limites de curso sao contados a partir dela. Se o braco nao "+
             "estiver mesmo na referencia, a area util inteira sai do lugar."))
    post("/api/referenciar");
};
$("afJ").onchange=afEstado;
$("rdJ").onchange=afEstado;
$("rdG").oninput=afEstado;
$("btAfMarcar").onclick=function(){
  post("/api/aferir/marcar?j="+$("afJ").value).then(afEstado);
};
$("btAfEnc").onclick=function(){
  post("/api/aferir/encoder?j="+$("afJ").value)
   .then(function(){carregou=false;});
};

$("btReset").onclick =function(){
  /* carregou=false faz o proximo status repreencher os campos: sem isso o
     formulario continuava mostrando os valores antigos e o "Salvar"
     seguinte reaplicava tudo por cima do padrao de fabrica. */
  if(confirm("Restaurar os PARAMETROS de fabrica?\n\n"+
             "Velocidades, aceleracao, resolucao, medidas e protecoes.\n"+
             "Calibracao, mesa ensinada e zero absoluto NAO sao tocados."))
    post("/api/config/reset").then(function(){carregou=false;});};

/* Apagar tudo. A palavra digitada e conferida aqui E na porta: a tela
   pode ter um defeito, e a porta nao pode confiar nela. */
$("btApagarTudo").onclick=function(){
  const q=$("qApagarTudo");
  if($("apgConf").value.trim().toUpperCase()!=="APAGAR"){
    q.textContent="digite APAGAR no campo acima para confirmar.";
    return;
  }
  if(!confirm("APAGAR TUDO e reiniciar a maquina?\n\n"+
              "Isto apaga a CALIBRACAO das juntas, a area da mesa ensinada, "+
              "o zero absoluto, a ligacao do encoder e os contadores.\n\n"+
              "O braco precisara ser calibrado de novo antes de trabalhar.\n"+
              "O cartao SD nao e tocado."))return;
  q.textContent="apagando e reiniciando...";
  post("/api/apagar/tudo?conf=APAGAR").then(function(){
    $("apgConf").value="";
    q.textContent=erro?erro:"apagado. A maquina esta reiniciando -- recarregue esta pagina em alguns segundos.";
  });
};

/* =====================================================================
   ABA MAQUINA: saude, registro, QR de conexao, firmware e modo operador
   ===================================================================== */

/* <<QR>> -- este bloco e extraido por testes/conferir_qr.py e conferido
   contra um decodificador de verdade. Nao mexa nele sem rodar o guarda:
   um QR desenhado errado fica bonito na tela e nenhum leitor abre. */

function qrGerar(txt,mascaraFixa){
  const dados=[];
  for(const ch of unescape(encodeURIComponent(txt))) dados.push(ch.charCodeAt(0));

  /* capacidade de dados (bytes) por versao, nivel M */
  const CAP=[0,14,26,42,62,84,106,122,152,180,213];
  /* [total de codewords, blocos grupo1, dados/bloco g1, blocos g2, dados/bloco g2] nivel M */
  const EC =[null,
    [10,1,16,0,0],[16,1,28,0,0],[26,1,44,0,0],[18,2,32,0,0],[24,2,43,0,0],
    [16,4,27,0,0],[18,4,31,0,0],[22,2,38,2,39],[22,3,36,2,37],[26,4,43,1,44]];
  let v=0;
  for(let i=1;i<=10;i++) if(dados.length<=CAP[i]){v=i;break;}
  if(!v) throw new Error("texto longo demais para o QR");

  const [ecPorBloco,b1,d1,b2,d2]=EC[v];
  const totalDados=b1*d1+b2*d2;

  /* ---- bitstream ---- */
  const bits=[];
  const push=(val,n)=>{for(let i=n-1;i>=0;i--)bits.push((val>>i)&1);};
  push(4,4);                       /* modo byte */
  push(dados.length,8);            /* v1..9: 8 bits; v10: 16 */
  if(v>=10){bits.length=4;push(dados.length,16);}
  for(const b of dados) push(b,8);
  for(let i=0;i<4&&bits.length<totalDados*8;i++) bits.push(0);
  while(bits.length%8) bits.push(0);
  const cw=[];
  for(let i=0;i<bits.length;i+=8){let b=0;for(let j=0;j<8;j++)b=(b<<1)|bits[i+j];cw.push(b);}
  const PAD=[0xEC,0x11];
  for(let i=0;cw.length<totalDados;i++) cw.push(PAD[i%2]);

  /* ---- Reed-Solomon ---- */
  const EXP=new Array(512),LOG=new Array(256);
  for(let i=0,x=1;i<255;i++){EXP[i]=x;LOG[x]=i;x<<=1;if(x&256)x^=0x11d;}
  for(let i=255;i<512;i++)EXP[i]=EXP[i-255];
  const mul=(a,b)=>(a===0||b===0)?0:EXP[LOG[a]+LOG[b]];
  function gerador(n){let g=[1];for(let i=0;i<n;i++){const ng=new Array(g.length+1).fill(0);
    for(let j=0;j<g.length;j++){ng[j]^=mul(g[j],1);ng[j+1]^=mul(g[j],EXP[i]);}g=ng;}return g;}
  function rs(bloco,n){const g=gerador(n);const r=bloco.concat(new Array(n).fill(0));
    for(let i=0;i<bloco.length;i++){const f=r[i];if(f===0)continue;
      for(let j=0;j<g.length;j++)r[i+j]^=mul(g[j],f);}
    return r.slice(bloco.length);}

  const blocos=[],ecs=[];
  let p=0;
  for(let i=0;i<b1;i++){const b=cw.slice(p,p+d1);p+=d1;blocos.push(b);ecs.push(rs(b,ecPorBloco));}
  for(let i=0;i<b2;i++){const b=cw.slice(p,p+d2);p+=d2;blocos.push(b);ecs.push(rs(b,ecPorBloco));}

  const finais=[];
  const maxD=Math.max(d1,d2);
  for(let i=0;i<maxD;i++) for(const b of blocos) if(i<b.length) finais.push(b[i]);
  for(let i=0;i<ecPorBloco;i++) for(const e of ecs) finais.push(e[i]);

  /* ---- matriz ---- */
  const n=v*4+17;
  const m=Array.from({length:n},()=>new Array(n).fill(null));
  const por=(r,c,val)=>{if(r>=0&&r<n&&c>=0&&c<n)m[r][c]=val;};

  function finder(r,c){
    for(let i=-1;i<=7;i++)for(let j=-1;j<=7;j++){
      const rr=r+i,cc=c+j;
      if(rr<0||rr>=n||cc<0||cc>=n)continue;
      const dentro=(i>=0&&i<=6&&(j===0||j===6))||(j>=0&&j<=6&&(i===0||i===6))||
                   (i>=2&&i<=4&&j>=2&&j<=4);
      m[rr][cc]=dentro?1:0;
    }
  }
  finder(0,0);finder(0,n-7);finder(n-7,0);

  /* alinhamento */
  const AL=[[],[],[6,18],[6,22],[6,26],[6,30],[6,34],[6,22,38],[6,24,42],[6,26,46],[6,28,50]];
  const cen=AL[v];
  for(const r of cen)for(const c of cen){
    if((r<=7&&c<=7)||(r<=7&&c>=n-8)||(r>=n-8&&c<=7))continue;
    for(let i=-2;i<=2;i++)for(let j=-2;j<=2;j++)
      m[r+i][c+j]=(Math.max(Math.abs(i),Math.abs(j))!==1)?1:0;
  }
  /* temporizacao */
  for(let i=8;i<n-8;i++){ if(m[6][i]===null)m[6][i]=(i%2===0)?1:0;
                          if(m[i][6]===null)m[i][6]=(i%2===0)?1:0; }
  /* modulo escuro */
  m[n-8][8]=1;

  /* reserva das areas de formato/versao */
  const reservado=Array.from({length:n},()=>new Array(n).fill(false));
  for(let i=0;i<9;i++){reservado[8][i]=true;reservado[i][8]=true;}
  for(let i=0;i<8;i++){reservado[8][n-1-i]=true;reservado[n-1-i][8]=true;}
  for(let r=0;r<n;r++)for(let c=0;c<n;c++) if(m[r][c]!==null) reservado[r][c]=true;
  if(v>=7){for(let i=0;i<6;i++)for(let j=0;j<3;j++){
    reservado[n-11+j][i]=true;reservado[i][n-11+j]=true;}}

  /* ---- dados em ziguezague ---- */
  const fluxo=[];
  for(const b of finais) for(let i=7;i>=0;i--) fluxo.push((b>>i)&1);
  let idx=0,cima=true;
  for(let col=n-1;col>0;col-=2){
    if(col===6)col--;
    for(let k=0;k<n;k++){
      const r=cima?(n-1-k):k;
      for(const c of [col,col-1]){
        if(reservado[r][c])continue;
        m[r][c]=idx<fluxo.length?fluxo[idx]:0;
        idx++;
      }
    }
    cima=!cima;
  }

  /* ---- mascaras ---- */
  const REGRA=[
    (r,c)=>(r+c)%2===0, (r,c)=>r%2===0, (r,c)=>c%3===0, (r,c)=>(r+c)%3===0,
    (r,c)=>(Math.floor(r/2)+Math.floor(c/3))%2===0,
    (r,c)=>((r*c)%2)+((r*c)%3)===0,
    (r,c)=>(((r*c)%2)+((r*c)%3))%2===0,
    (r,c)=>(((r+c)%2)+((r*c)%3))%2===0];

  function formatoBits(mask){
    const dadosF=(0<<3)|mask;            /* nivel M = 00 */
    let v2=dadosF<<10;
    for(let i=4;i>=0;i--) if(v2&(1<<(i+10))) v2^=0x537<<i;
    return ((dadosF<<10)|v2)^0x5412;
  }
  function porFormato(mm,mask){
    const f=formatoBits(mask);
    for(let i=0;i<15;i++){
      /* i=0 recebe o bit MAIS significativo. Conferido lendo os modulos
         de formato de um QR de referencia -- na ordem contraria o codigo
         fica desenhado certinho e nenhum leitor abre. */
      const b=(f>>(14-i))&1;
      /* copia 1 */
      if(i<6)      mm[8][i]=b;
      else if(i===6) mm[8][7]=b;
      else if(i===7) mm[8][8]=b;
      else if(i===8) mm[7][8]=b;
      else           mm[14-i][8]=b;
      /* copia 2: 7 modulos na coluna, 8 na linha. O oitavo da coluna
         seria (n-8,8), que e o modulo escuro fixo -- por isso o corte e
         em 7 e nao em 8. */
      if(i<7) mm[n-1-i][8]=b;
      else    mm[8][n-15+i]=b;
    }
  }
  function porVersao(mm){
    if(v<7)return;
    let d=v<<12;
    for(let i=5;i>=0;i--) if(d&(1<<(i+12))) d^=0x1f25<<i;
    const bitsV=(v<<12)|d;
    for(let i=0;i<18;i++){
      const b=(bitsV>>i)&1;
      mm[Math.floor(i/3)][n-11+(i%3)]=b;
      mm[n-11+(i%3)][Math.floor(i/3)]=b;
    }
  }
  function penalidade(mm){
    let p=0;
    /* 1: corridas */
    for(let r=0;r<n;r++)for(const eixo of [0,1]){
      let run=1;
      for(let c=1;c<n;c++){
        const a=eixo?mm[c-1][r]:mm[r][c-1], b=eixo?mm[c][r]:mm[r][c];
        if(a===b)run++;else{if(run>=5)p+=3+(run-5);run=1;}
      }
      if(run>=5)p+=3+(run-5);
    }
    /* 2: blocos 2x2 */
    for(let r=0;r<n-1;r++)for(let c=0;c<n-1;c++){
      const a=mm[r][c];
      if(a===mm[r][c+1]&&a===mm[r+1][c]&&a===mm[r+1][c+1])p+=3;
    }
    /* 3: padrao 1011101 com 4 claros */
    const A=[1,0,1,1,1,0,1,0,0,0,0],B=[0,0,0,0,1,0,1,1,1,0,1];
    for(let r=0;r<n;r++)for(let c=0;c+10<n;c++){
      let ma=true,mb=true;
      for(let i=0;i<11;i++){if(mm[r][c+i]!==A[i])ma=false;if(mm[r][c+i]!==B[i])mb=false;}
      if(ma||mb)p+=40;
      ma=true;mb=true;
      for(let i=0;i<11;i++){if(mm[c+i][r]!==A[i])ma=false;if(mm[c+i][r]!==B[i])mb=false;}
      if(ma||mb)p+=40;
    }
    /* 4: proporcao de escuros */
    let escuros=0;
    for(let r=0;r<n;r++)for(let c=0;c<n;c++)if(mm[r][c])escuros++;
    const pct=escuros*100/(n*n);
    p+=Math.floor(Math.abs(pct-50)/5)*10;
    return p;
  }

  let melhor=null,melhorP=Infinity;
  for(let mask=0;mask<8;mask++){
    if(mascaraFixa!==undefined&&mask!==mascaraFixa)continue;
    const mm=m.map(r=>r.slice());
    for(let r=0;r<n;r++)for(let c=0;c<n;c++)
      if(!reservado[r][c]&&REGRA[mask](r,c)) mm[r][c]^=1;
    porFormato(mm,mask);porVersao(mm);
    const p2=penalidade(mm);
    if(p2<melhorP){melhorP=p2;melhor=mm;}
  }
  return melhor;
}
/* <</QR>> */

function pintarQRem(id,txt){
  const cv=$(id); if(!cv)return;
  const ct=cv.getContext("2d");
  ct.fillStyle="#fff"; ct.fillRect(0,0,cv.width,cv.height);
  let m;
  try{ m=qrGerar(txt); }catch(e){
    ct.fillStyle="#900"; ct.font="11px sans-serif";
    ct.fillText("texto longo demais",8,cv.height/2); return; }
  const n=m.length, Q=2;                    /* zona de silencio */
  const e=Math.floor(cv.width/(n+2*Q));
  const off=Math.floor((cv.width-e*(n+2*Q))/2)+e*Q;
  /* Preto no branco SEMPRE, mesmo no tema escuro: leitor de QR espera
     contraste nessa ordem, e codigo invertido nao abre em boa parte dos
     celulares. Por isso o fundo branco tambem e pintado acima. */
  ct.fillStyle="#000";
  for(let r=0;r<n;r++)for(let c=0;c<n;c++)
    if(m[r][c]) ct.fillRect(off+c*e,off+r*e,e,e);
}

let qrDesenhado=false;
function pintarQR(){
  if(qrDesenhado)return;
  fetch("/api/rede").then(function(r){return r.json();}).then(function(j){
    /* Formato padrao de credencial Wi-Fi, lido por Android e iPhone. A
       rede da maquina e aberta, entao T:nopass e sem senha. */
    const ssid=String(j.ssid||"Robo2dof").replace(/([\;,:"])/g,"\\$1");
    pintarQRem("qrRede","WIFI:T:nopass;S:"+ssid+";;");
    pintarQRem("qrPainel","http://"+(j.ip||"192.168.4.1")+"/");
    qrDesenhado=true;
  }).catch(function(){});
}

function dur(s){
  s=Math.max(0,Math.floor(s));
  const d=Math.floor(s/86400), h=Math.floor(s%86400/3600), m=Math.floor(s%3600/60);
  if(d)return d+" d "+h+" h";
  if(h)return h+" h "+m+" min";
  if(m)return m+" min";
  return s+" s";
}

function saudeAtualizar(){
  fetch("/api/saude").then(function(r){return r.json();}).then(function(j){
    const enc=function(e){
      if(!e.vale&&!e.ok&&!e.falha)return "nao ligado";
      return e.graus.toFixed(2)+"° · "+e.taxa+"% de acerto"+
             (e.vale?"":" · SEM LEITURA");
    };
    const linhas=[
      ["Ligada ha",              dur(j.up)],
      ["Pecas prontas",          j.ciclos+" (nesta sessao: "+j.ciclosSes+")"],
      ["Interrompidas no meio",  String(j.abortados)],
      ["Arco aberto, no total",  dur(j.arcoS)],
      ["Desde a manutencao",     j.manut+" pecas"],
      ["Encoder junta 1",        enc(j.enc1)],
      ["Encoder junta 2",        enc(j.enc2)],
      ["Travamentos",            String(j.trav)],
      ["Avisos de desvio",       String(j.alerta)],
      ["Alarme dos drivers",     (j.alarme1||j.alarme2)?"SIM":"nenhum"],
      ["Botao de emergencia",    j.estop?"instalado":"nao instalado"],
      ["Botao da ponteira",      j.aprBotao?"instalado":"nao instalado"],
      ["Cartao",                 j.cartao?(Math.round(j.cartaoLivre/1024)+" MB livres de "+
                                           Math.round(j.cartaoTotal/1024)+" MB"):"ausente"],
      ["Memoria livre",          Math.round(j.heap/1024)+" kB (minimo "+
                                 Math.round(j.heapMin/1024)+" kB)"],
      ["Programa na particao",   Math.round(j.flashUso/1024)+" kB de "+
                                 Math.round(j.flashTot/1024)+" kB"]
    ];
    $("saudeG").innerHTML=linhas.map(function(l){
      return '<div class="sl"><span>'+tr(l[0])+'</span><b>'+tr(l[1])+'</b></div>';}).join("");

    const encRuim=(j.enc1.ok+j.enc1.falha>50&&j.enc1.taxa<90);
    const ruim=j.alarme1||j.alarme2||j.trav>0||encRuim;
    $("sbSaude").textContent = ruim ? "atencao: veja os itens abaixo"
      : (j.ciclos+" pecas · ligada ha "+dur(j.up));
    $("sbSaude").className="sb"+(ruim?" alerta":"");

    $("sbOta").textContent = tr(j.ota ? "disponivel" : "indisponivel neste firmware");
    $("otaCaixa").style.display = j.ota ? "" : "none";
    $("otaNota").innerHTML = j.ota
      ? "Envie o arquivo <b>.bin</b> gerado por <b>Sketch &rarr; Exportar binario compilado</b> na IDE do Arduino."
      : "Este firmware foi gravado com a particao de <b>3 MB sem OTA</b>, entao nao ha para onde escrever a imagem nova. Para atualizar pela rede, grave uma vez pelo USB usando <b>partitions_ota.csv</b> (veja o MANUAL). Nao ha como contornar: OTA precisa de duas particoes de programa, porque a imagem nova e escrita naquela que nao esta rodando.";
  }).catch(function(){});

  fetch("/api/registro").then(function(r){return r.json();}).then(function(j){
    if(!j.n){$("regLista").textContent=tr("nenhum evento registrado ainda");return;}
    $("regLista").innerHTML=j.linhas.map(function(l){
      return '<div><i>'+dur(l.s)+' atras</i> '+String(l.t).replace(/[<>&]/g,"")+'</div>';
    }).join("");
  }).catch(function(){});
}

$("btManut").onclick=function(){
  if(!confirm("Registrar manutencao feita e zerar o contador de pecas desde a ultima?"))return;
  post("/api/manutencao/ok").then(saudeAtualizar);
};

$("btIdioma").onclick=function(){definirIdioma(idioma==="en"?"pt":"en");};
$("btCfgRestaurar").onclick=function(){
  if(!confirm("Substituir a configuracao da maquina pela copia do cartao?\n\n"+
              "Calibracao, curso, reducao, mesa e zero voltam ao que estava "+
              "gravado no cartao."))return;
  post("/api/cfg/restaurar").then(function(){
    acao("CfgRestaurar", erro || "");
    if(!erro)$("qCfgRestaurar").textContent="lido do cartao.";
  });
};

/* Envio do firmware. XMLHttpRequest e nao fetch por causa da barra de
   progresso: um .bin de 1,3 MB por Wi-Fi leva dezenas de segundos, e sem
   progresso o operador acha que travou e desliga a maquina no meio -- que
   e a unica maneira de transformar uma atualizacao numa placa morta. */
$("btOta").onclick=function(){
  const f=$("otaArq").files[0];
  if(!f){acao("Ota","escolha o arquivo .bin primeiro");return;}
  if(!/\.bin$/i.test(f.name)){acao("Ota","o arquivo tem de ser .bin");return;}
  if(!confirm("Gravar "+f.name+" ("+Math.round(f.size/1024)+" kB)? A maquina reinicia ao terminar."))return;
  acao("Ota","");
  const fd=new FormData(); fd.append("f",f,f.name);
  const x=new XMLHttpRequest();
  x.open("POST","/api/ota");
  x.upload.onprogress=function(e){
    if(e.lengthComputable)$("otaBarra").style.width=(e.loaded*100/e.total)+"%";};
  x.onload=function(){
    acao("Ota", x.status===200 ? "" : (x.responseText||"falhou"));
    if(x.status===200)$("otaBarra").style.width="100%";
  };
  x.onerror=function(){acao("Ota","conexao perdida durante o envio");};
  x.send(fd);
};

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
$("z3D").onclick =function(){
  vista3D=!vista3D;
  $("z3D").textContent = vista3D?"2D":"3D";
  $("z3D").classList.toggle("on",vista3D);
  /* Desenhar com o dedo so existe na vista de cima: um traco desenhado
     em perspectiva nao tem onde cair na mesa. Sair da 2D desliga o modo
     em vez de deixar o operador riscando no vazio. */
  if(vista3D&&desOn)$("zDes").onclick();
  try{localStorage.setItem("vista3d",vista3D?"1":"0");}catch(e){}
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
       letra2:cor("--letra2"),letra3:cor("--letra3"),
       elo1:t==="escuro"?"#8b98a9":"#5a6675",
       elo2:t==="escuro"?"#b7c2d1":"#8794a5",
       juntaF:t==="escuro"?"#2a333e":"#ffffff",
       ponto:t==="escuro"?"#141920":"#ffffff"};
  return PAL;
}

/* =====================================================================
   VISTA 3D

   O braco e PLANAR: os dois eixos giram no mesmo plano horizontal. Uma
   vista de cima ja mostra tudo o que decide o cordao, e por isso ela
   continua sendo a vista de trabalho -- e a que tem o desenho com o
   dedo, a escolha de pontos e a mesa.

   A vista 3D existe para outra coisa: enxergar a MAQUINA. A altura dos
   elos sobre a mesa, o alcance como volume, a ferramenta descendo ate a
   peca. Serve para explicar a maquina para quem nunca a viu, e para
   conferir de relance se o braco esta na postura que se imagina.

   E uma REPRESENTACAO em escala, nao um modelo do desenho mecanico: a
   altura dos elos e uma constante escolhida para a figura ficar legivel.
   ===================================================================== */
let vista3D=false;
/* Altura dos elos sobre a mesa, em mm. Nao vem do desenho mecanico: sao
   os numeros que fazem a figura ficar legivel. Altos demais e o braco
   flutua; baixos demais e a vista vira a de cima outra vez. */
const ALT_ELO1=110, ALT_ELO2=64;

/* =====================================================================
   QUAL POSTURA O DESENHO MOSTRA

   Ate aqui o boneco era desenhado com o angulo COMANDADO -- a conta de
   pulsos do firmware. Isso desenha a intencao, nao o braco: se o eixo
   escorregou, a tela continua mostrando tudo no lugar enquanto a peca
   sai torta.

   Agora o boneco e a posicao MEDIDA pelo encoder, que e onde o braco
   esta de verdade. Quando as duas discordam de mais de meio grau, o
   comandado aparece por tras como um fantasma: da para VER o desvio, em
   vez de so ler um numero.

   Sem leitura confiavel (encoder desligado, cabo solto, leitura
   impossivel) volta a valer o comandado -- e a legenda diz isso, porque
   um boneco que muda de significado sem avisar e pior que nenhum.
   ===================================================================== */
const DESVIO_VISIVEL = 0.5;   /* graus */

function legendaPostura(z){
  if(!z.medido) return "posicao comandada (sem leitura do encoder)";
  if(z.desvio > DESVIO_VISIVEL)
    return "posicao MEDIDA pelo encoder  ·  tracejado = comandado, "+
           z.desvio.toFixed(2)+"\u00b0 de desvio";
  return z.completo ? "posicao medida pelo encoder"
                    : "posicao medida (uma junta sem leitura)";
}

/* ---------- o braco desenhado glisa entre as amostras ----------
   O /api/status chega a cada 220 ms e o desenho roda a cada quadro:
   sem isto o braco repetia a mesma pose varias vezes e SALTAVA para a
   proxima -- uns 4,5 quadros por segundo de movimento de verdade, que
   e exatamente o "parece que esta travando".
   O robo nao anda aos saltos; era o desenho que mostrava assim.

   Aproximacao amortecida, nao extrapolacao: extrapolar pela velocidade
   passa do ponto toda vez que o eixo para, e um braco que ultrapassa e
   volta mente sobre onde a ponta esteve. Isto atrasa um pouco e nunca
   inventa posicao que nao houve. */
const SUAVE_TAU_MS = 90;
let suave = null, suaveMs = 0;

function suavizar(a1, a2){
  const agora = (typeof performance !== "undefined") ? performance.now() : Date.now();
  if(!suave){ suave = {t1:a1, t2:a2}; suaveMs = agora; return suave; }
  const dt = Math.min(agora - suaveMs, 500);   /* aba oculta nao teleporta */
  suaveMs = agora;
  if(Math.abs(a1 - suave.t1) > SUAVE_PULO_GRAUS ||
     Math.abs(a2 - suave.t2) > SUAVE_PULO_GRAUS){
    suave.t1 = a1; suave.t2 = a2; return suave;
  }
  const k = 1 - Math.exp(-dt / SUAVE_TAU_MS);
  suave.t1 += (a1 - suave.t1) * k;
  suave.t2 += (a2 - suave.t2) * k;
  /* Encosta de vez quando ja chegou: senao fica uma sobra permanente de
     centesimos de grau, e o numero na tela nunca bate com o do robo. */
  if(Math.abs(a1 - suave.t1) < 0.01) suave.t1 = a1;
  if(Math.abs(a2 - suave.t2) < 0.01) suave.t2 = a2;
  return suave;
}

/* Salto que nao e movimento -- zerar a maquina, recuperar posicao pelo
   encoder -- nao deve ser glisado: ali o braco nao percorreu o caminho, e
   desenhar o percurso seria mostrar um movimento que nao aconteceu.
   Reconhecido pelo TAMANHO: nenhum eixo anda 30 graus entre duas
   amostras de 220 ms na velocidade que esta maquina usa, entao um pulo
   desse tamanho e mudanca de referencial, nao movimento. */
function suavePular(){ suave = null; }
const SUAVE_PULO_GRAUS = 30;

function postura(){
  const c1 = D.t1 || 0, c2 = D.t2 || 0;
  const tem1 = !!D.m1ok, tem2 = !!D.m2ok;
  /* Junta sem leitura usa o comandado dela: uma bancada com um driver so
     no barramento tem de desenhar o braco inteiro assim mesmo. */
  const b1 = tem1 ? (D.m1 || 0) : c1;
  const b2 = tem2 ? (D.m2 || 0) : c2;
  const sv = suavizar(b1, b2);
  const r1 = sv.t1, r2 = sv.t2;
  return {
    t1: r1, t2: r2,               /* o que se desenha */
    c1: c1, c2: c2,               /* o comandado, para o fantasma */
    medido: tem1 || tem2,
    completo: tem1 && tem2,
    desvio: Math.max(tem1 ? Math.abs(r1 - c1) : 0,
                     tem2 ? Math.abs(r2 - c2) : 0)
  };
}

function pintar3D(){
  const C=paleta();
  const L1=D.l1||200,L2=D.l2||200,dp=window.devicePixelRatio||1;
  const w=cv.width/dp,h=cv.height/dp;
  const alc=L1+L2;
  /* A isometrica achata o eixo vertical, entao a cena cabe maior que na
     vista de cima. O braco e o assunto: a mesa existe para dar chao a
     ele, nao para ocupar a tela. */
  esc=Math.min(w,h)/(vistaMm*0.64);
  ox=w/2; oy=h*0.60;

  /* Isometrica: x para a direita-baixo, y para a esquerda-baixo, z para
     cima. O achatamento de 0,52 e o que faz a mesa parecer mesa em vez
     de losango deitado. */
  const CA=Math.cos(0.5236), SA=Math.sin(0.5236)*0.52;
  const Q=function(x,y,z){
    /* O 0,80 no Z e EXAGERO DECLARADO. Um braco de 850 mm de alcance tem
       110 mm de altura: na proporcao real ele sai achatado a ponto de nao
       se ler qual elo passa por cima de qual. O exagero e so vertical e
       nao mente sobre nada que se meca na tela -- X e Y saem na escala. */
    return [ox+(x-y)*CA*esc, oy-((x+y)*SA+(z||0)*0.80)*esc];
  };
  const escuro=document.documentElement.getAttribute("data-tema")==="escuro";

  ct.clearRect(0,0,w,h);
  ct.fillStyle=C.papel;ct.fillRect(0,0,w,h);

  /* ---- a mesa: uma superficie, nao so linhas soltas no vazio ---- */
  const lado=alc*0.86;
  const cantos=[Q(-lado,-lado,0),Q(lado,-lado,0),Q(lado,lado,0),Q(-lado,lado,0)];
  const gm=ct.createLinearGradient(cantos[0][0],cantos[0][1],cantos[2][0],cantos[2][1]);
  gm.addColorStop(0, escuro?"rgba(255,255,255,.055)":"rgba(255,255,255,.85)");
  gm.addColorStop(1, escuro?"rgba(255,255,255,.015)":"rgba(0,0,0,.045)");
  ct.fillStyle=gm;
  ct.beginPath();ct.moveTo(cantos[0][0],cantos[0][1]);
  for(let k=1;k<4;k++)ct.lineTo(cantos[k][0],cantos[k][1]);
  ct.closePath();ct.fill();
  ct.strokeStyle="rgba("+C.grade+",.45)";ct.lineWidth=1.5;ct.stroke();

  /* grade sobre a mesa */
  let passo=10; const alvo=46;
  while(passo*esc<alvo)passo*=(String(passo)[0]==="1")?2.5:2;
  while(passo*esc>alvo*2.6)passo/=(String(passo)[0]==="2")?2.5:2;
  passo=Math.max(1,Math.round(passo));
  const lim=Math.floor(lado/passo)*passo;
  ct.lineWidth=1;
  for(let v=-lim;v<=lim;v+=passo){
    const eixo=(v===0);
    ct.strokeStyle="rgba("+C.grade+","+(eixo?.55:.14)+")";
    ct.lineWidth=eixo?1.6:1;
    let a=Q(v,-lado,0),b=Q(v,lado,0);
    ct.beginPath();ct.moveTo(a[0],a[1]);ct.lineTo(b[0],b[1]);ct.stroke();
    a=Q(-lado,v,0);b=Q(lado,v,0);
    ct.beginPath();ct.moveTo(a[0],a[1]);ct.lineTo(b[0],b[1]);ct.stroke();
  }

  /* alcance util, preenchido de leve */
  ct.beginPath();
  for(let g=0;g<=360;g+=3){
    const r=g*Math.PI/180, q=Q(alc*Math.cos(r),alc*Math.sin(r),0);
    if(g)ct.lineTo(q[0],q[1]);else ct.moveTo(q[0],q[1]);
  }
  ct.closePath();
  ct.fillStyle="rgba("+C.grade+",.07)";ct.fill();
  ct.strokeStyle="rgba("+C.grade+",.5)";ct.lineWidth=1.5;ct.stroke();

  /* ---- a AREA ENSINADA, se houver: o retangulo onde a ponta pode ir ----
     Nao e enfeite: dali para fora o braco nao anda, nem por programa nem
     por jog. Ver o limite e o que evita ensinar um ponto que sera
     recusado depois. */
  if(D.mesaOn){
    const q=[Q(D.mesaX0,D.mesaY0,0),Q(D.mesaX1,D.mesaY0,0),
             Q(D.mesaX1,D.mesaY1,0),Q(D.mesaX0,D.mesaY1,0)];
    ct.beginPath();ct.moveTo(q[0][0],q[0][1]);
    for(let k=1;k<4;k++)ct.lineTo(q[k][0],q[k][1]);
    ct.closePath();
    ct.fillStyle="rgba("+C.grade+",.10)";ct.fill();
    ct.strokeStyle=C.arco;ct.globalAlpha=.65;
    ct.setLineDash([7,5]);ct.lineWidth=2;ct.stroke();
    ct.setLineDash([]);ct.globalAlpha=1;
  }

  /* ---- trechos e pontos do programa, deitados na mesa ---- */
  ct.lineWidth=2.5;ct.lineCap="round";
  for(let i=0;i<pontos.length-1;i++){
    const A=pontos[i],B=pontos[i+1];
    const qa=Q(A.x,A.y,0), qb=Q(B.x,B.y,0);
    ct.strokeStyle=A.av?C.brasa:(A.s?C.quente:C.arco);
    ct.setLineDash(A.s?[]:[5,4]);
    ct.beginPath();ct.moveTo(qa[0],qa[1]);ct.lineTo(qb[0],qb[1]);ct.stroke();
  }
  ct.setLineDash([]);
  pontos.forEach(function(pt,i){
    const q=Q(pt.x,pt.y,0);
    ct.fillStyle=pt.s?C.quente:C.ponto;
    ct.strokeStyle=pt.s?C.quente:C.arco;ct.lineWidth=1.5;
    ct.beginPath();ct.ellipse(q[0],q[1],5,3.2,0,0,TAU);ct.fill();ct.stroke();
    if(i===0||i===pontos.length-1){
      ct.fillStyle=C.letra3;
      ct.font="9px ui-monospace,Menlo,monospace";ct.textAlign="center";
      ct.fillText(String(i+1),q[0],q[1]-8);
    }
  });

  /* =====================================================================
     O braco, com volume.

     Cada elo e uma CAIXA: face de cima clara, faces laterais escuras. Sao
     tres quadrilateros por elo, nao uma linha grossa -- e a diferenca
     entre parecer um tubo chapado e parecer uma peca.

     A ordem importa: primeiro a sombra na mesa, depois a base, depois o
     elo 1, depois o elo 2. Desenhar do fundo para a frente e o que faz
     uma peca tapar a outra como tapa de verdade.
     ===================================================================== */
  const PZ=postura();
  const t1=PZ.t1*Math.PI/180, t2=(PZ.t1+PZ.t2)*Math.PI/180;
  const cx=L1*Math.cos(t1), cy=L1*Math.sin(t1);
  const px=cx+L2*Math.cos(t2), py=cy+L2*Math.sin(t2);


  /* Sombra: elipse borrada sob cada junta, e uma faixa entre elas. Sem
     sombra a peca flutua e a altura nao se le. */
  const sombra=function(x,y,r){
    const q=Q(x,y,0);
    const g=ct.createRadialGradient(q[0],q[1],0,q[0],q[1],r);
    g.addColorStop(0,"rgba(0,0,0,.26)");
    g.addColorStop(1,"rgba(0,0,0,0)");
    ct.fillStyle=g;
    ct.beginPath();ct.ellipse(q[0],q[1],r,r*0.55,0,0,TAU);ct.fill();
  };
  ct.save();
  ct.strokeStyle="rgba(0,0,0,.13)";
  ct.lineWidth=Math.max(4,14*esc);ct.lineCap="round";
  let sa=Q(0,0,0), sb=Q(cx,cy,0), sc=Q(px,py,0);
  ct.beginPath();ct.moveTo(sa[0],sa[1]);ct.lineTo(sb[0],sb[1]);ct.lineTo(sc[0],sc[1]);ct.stroke();
  ct.restore();
  sombra(0,0,Math.max(16,34*esc));
  sombra(cx,cy,Math.max(12,26*esc));
  sombra(px,py,Math.max(9,18*esc));

  const mistura=function(hex,f){
    /* clareia (f>0) ou escurece (f<0) uma cor. Aceita #rrggbb e rgb(). */
    let r,g,b;
    if(hex[0]==="#"){
      const n=parseInt(hex.slice(1),16);
      r=(n>>16)&255; g=(n>>8)&255; b=n&255;
    }else{
      const m=hex.match(/(\d+)\D+(\d+)\D+(\d+)/);
      r=m?+m[1]:128; g=m?+m[2]:128; b=m?+m[3]:128;
    }
    const alvo=f>0?255:0, k=Math.abs(f);
    r=Math.round(r+(alvo-r)*k); g=Math.round(g+(alvo-g)*k); b=Math.round(b+(alvo-b)*k);
    return "rgb("+r+","+g+","+b+")";
  };

  /* Profundidade na isometrica: quanto MAIOR x+y, mais perto do
     observador. Toda peca e desenhada em ordem crescente disso, e e o
     que faz uma tapar a outra como tapa de verdade. */
  const prof=function(x,y){ return x+y; };

  const quad=function(p,cor){
    ct.fillStyle=cor;ct.beginPath();
    ct.moveTo(p[0][0],p[0][1]);
    for(let k=1;k<p.length;k++)ct.lineTo(p[k][0],p[k][1]);
    ct.closePath();ct.fill();
    ct.strokeStyle="rgba(0,0,0,.18)";ct.lineWidth=1;ct.stroke();
  };

  /* Uma caixa deitada de (x0,y0) a (x1,y1), na altura z, com largura
     'larg' e espessura 'alt'.
     As DUAS laterais sao ordenadas por profundidade, e as duas pontas
     ganham tampa: sem elas o elo parecia um tubo aberto quando visto de
     enfiada. */
  const caixa=function(x0,y0,x1,y1,z,larg,alt,corTopo,corLado){
    const dx=x1-x0, dy=y1-y0, m=Math.hypot(dx,dy)||1;
    const nx=-dy/m*larg/2, ny=dx/m*larg/2;      /* normal no plano */
    const A=[x0+nx,y0+ny], B=[x1+nx,y1+ny], Bi=[x1-nx,y1-ny], Ai=[x0-nx,y0-ny];
    const zt=z+alt/2, zb=z-alt/2;

    /* laterais, a de tras primeiro */
    const ladoA=[A,B], ladoB=[Ai,Bi];
    const dA=prof((A[0]+B[0])/2,(A[1]+B[1])/2);
    const dB=prof((Ai[0]+Bi[0])/2,(Ai[1]+Bi[1])/2);
    const ordem = (dA<dB) ? [ladoA,ladoB] : [ladoB,ladoA];
    const corFundo = mistura(corLado,-0.12);
    ordem.forEach(function(L,i){
      quad([Q(L[0][0],L[0][1],zb),Q(L[1][0],L[1][1],zb),
            Q(L[1][0],L[1][1],zt),Q(L[0][0],L[0][1],zt)],
           i===0?corFundo:corLado);
    });
    /* tampa da ponta mais proxima */
    const pA=prof(x0,y0), pB=prof(x1,y1);
    const tp = (pA>pB) ? [A,Ai] : [B,Bi];
    quad([Q(tp[0][0],tp[0][1],zb),Q(tp[1][0],tp[1][1],zb),
          Q(tp[1][0],tp[1][1],zt),Q(tp[0][0],tp[0][1],zt)], corFundo);
    /* face de cima por ultimo: e a que se ve */
    quad([Q(A[0],A[1],zt),Q(B[0],B[1],zt),Q(Bi[0],Bi[1],zt),Q(Ai[0],Ai[1],zt)],corTopo);
  };

  /* Um cilindro em pe: corpo + tampa. Serve para a base e para as juntas. */
  const cilindro=function(x,y,z0,z1,r,corLado,corTopo){
    const b=Q(x,y,z0), t=Q(x,y,z1), rx=r*esc, ry=r*esc*0.52;
    ct.fillStyle=corLado;
    ct.beginPath();
    ct.ellipse(b[0],b[1],rx,ry,0,0,Math.PI);
    ct.lineTo(t[0]-rx,t[1]);
    ct.ellipse(t[0],t[1],rx,ry,0,Math.PI,0,true);
    ct.closePath();ct.fill();
    ct.fillStyle=corTopo;
    ct.beginPath();ct.ellipse(t[0],t[1],rx,ry,0,0,TAU);ct.fill();
    ct.strokeStyle="rgba(0,0,0,.20)";ct.lineWidth=1;ct.stroke();
  };

  const topo1=mistura(C.elo1, escuro?0.22:0.30), lado1=mistura(C.elo1,-0.22);
  const topo2=mistura(C.elo2, escuro?0.22:0.30), lado2=mistura(C.elo2,-0.22);

  /* =====================================================================
     ORDEM DE DESENHO POR PROFUNDIDADE.

     Estava fixa: base, elo 1, cotovelo, elo 2 -- sempre nessa ordem. Com
     o cotovelo dobrado PARA TRAS, o elo 2 esta atras do elo 1 na cena e
     mesmo assim era pintado por cima dele. O braco aparecia recortado
     errado em metade das posturas, e era isso que fazia o desenho
     parecer quebrado.

     Agora cada peca declara a profundidade do seu ponto medio e o
     conjunto e pintado do fundo para a frente.
     ===================================================================== */
  const pecas=[];
  /* pedestal: dois degraus, para a base ter cara de base e nao de poste */
  pecas.push({d:-1e9, f:function(){
    cilindro(0,0,0,10, 44, mistura(C.elo1,-0.42), mistura(C.elo1,-0.26));
    cilindro(0,0,8,ALT_ELO1-14, 30, mistura(C.elo1,-0.34), mistura(C.elo1,-0.12));
  }});
  pecas.push({d:prof(cx/2,cy/2), f:function(){
    caixa(0,0,cx,cy, ALT_ELO1, Math.max(22,L1*0.15), 26, topo1, lado1);
  }});
  pecas.push({d:prof(cx,cy)+0.01, f:function(){
    /* carcaca do cotovelo: desce do elo 1 ate a altura do elo 2 */
    cilindro(cx,cy,ALT_ELO2-8,ALT_ELO1+12, 21, mistura(C.elo1,-0.30), topo1);
  }});
  pecas.push({d:prof((cx+px)/2,(cy+py)/2), f:function(){
    caixa(cx,cy,px,py, ALT_ELO2, Math.max(16,L2*0.115), 20, topo2, lado2);
  }});
  /* discos dos eixos, sempre por cima da propria junta */
  pecas.push({d:prof(0,0)+0.02, f:function(){
    cilindro(0,0,ALT_ELO1+12,ALT_ELO1+19, 17, mistura(C.arco,-0.2), C.arco);
  }});
  pecas.push({d:prof(cx,cy)+0.03, f:function(){
    cilindro(cx,cy,ALT_ELO1+12,ALT_ELO1+18, 13, mistura(C.elo1,-0.2), topo1);
  }});
  pecas.sort(function(a,b){return a.d-b.d;});
  pecas.forEach(function(p){p.f();});

  /* ---- a ferramenta, descendo ate a peca ---- */
  /* A tocha e um cone: grossa em cima, fina na ponta. Um risco de
     espessura constante nao lia como ferramenta. */
  const zTopo=ALT_ELO2+6;
  const k2=Q(px,py,zTopo), pMesa=Q(px,py,0);
  {
    const rTopo=Math.max(5,14*esc), rPonta=Math.max(1.5,3.5*esc);
    const g=ct.createLinearGradient(k2[0],k2[1],pMesa[0],pMesa[1]);
    g.addColorStop(0, mistura(C.elo2,-0.10));
    g.addColorStop(1, D.solda?C.quente:mistura(C.elo2,-0.45));
    ct.fillStyle=g;
    ct.beginPath();
    ct.moveTo(k2[0]-rTopo,k2[1]);
    ct.lineTo(k2[0]+rTopo,k2[1]);
    ct.lineTo(pMesa[0]+rPonta,pMesa[1]);
    ct.lineTo(pMesa[0]-rPonta,pMesa[1]);
    ct.closePath();ct.fill();
    ct.strokeStyle="rgba(0,0,0,.22)";ct.lineWidth=1;ct.stroke();
    /* colar do bico */
    ct.fillStyle=mistura(C.elo2,-0.30);
    ct.beginPath();
    ct.ellipse(k2[0],k2[1],rTopo,rTopo*0.5,0,0,TAU);ct.fill();
  }

  if(D.solda){
    /* o arco: um halo quente na peca */
    const g=ct.createRadialGradient(pMesa[0],pMesa[1],0,pMesa[0],pMesa[1],22);
    g.addColorStop(0,C.brasa);
    g.addColorStop(0.35,"rgba(255,110,40,.55)");
    g.addColorStop(1,"rgba(255,110,40,0)");
    ct.fillStyle=g;
    ct.beginPath();ct.ellipse(pMesa[0],pMesa[1],22,13,0,0,TAU);ct.fill();
  }
  ct.fillStyle=D.solda?C.brasa:C.arco;
  ct.beginPath();ct.ellipse(pMesa[0],pMesa[1],D.solda?5:4,D.solda?3.5:2.8,0,0,TAU);ct.fill();
  ct.lineCap="butt";

  /* Fantasma do comandado, POR CIMA de tudo -- ele so serve se der para
     ve-lo. Desenhado por baixo do braco, ficava escondido justamente nas
     posturas em que o desvio importa. */
  if(PZ.medido && PZ.desvio > DESVIO_VISIVEL){
    const f1=PZ.c1*Math.PI/180, f2=(PZ.c1+PZ.c2)*Math.PI/180;
    const fcx=L1*Math.cos(f1), fcy=L1*Math.sin(f1);
    const fpx=fcx+L2*Math.cos(f2), fpy=fcy+L2*Math.sin(f2);
    const a=Q(0,0,ALT_ELO1), b=Q(fcx,fcy,ALT_ELO1), c2q=Q(fpx,fpy,ALT_ELO2);
    ct.save();
    ct.strokeStyle=C.brasa;
    ct.setLineDash([7,5]); ct.lineWidth=Math.max(2,3.5*esc); ct.lineCap="round";
    ct.globalAlpha=.85;
    ct.beginPath();ct.moveTo(a[0],a[1]);ct.lineTo(b[0],b[1]);ct.lineTo(c2q[0],c2q[1]);
    ct.stroke();
    ct.fillStyle=C.brasa;
    ct.beginPath();ct.ellipse(c2q[0],c2q[1],4,2.8,0,0,TAU);ct.fill();
    ct.setLineDash([]); ct.globalAlpha=1; ct.restore();
  }

  /* Uma linha so, embaixo. Os angulos e a ponta ja estao na regua do
     rodape -- repetir aqui em cima so cobriria a legenda. */
  ct.fillStyle=C.letra3;
  ct.font="10px ui-monospace,Menlo,monospace";ct.textAlign="left";
  ct.fillText(legendaPostura(PZ) + "  ·  desenhar e escolher pontos e na vista de cima",
              10, h-10);
}

function pintar(){
  /* Na aba errada o canvas tem largura zero: desenhar ali so gasta CPU
     e divide por zero na escala. */
  if(!cv.width||!cv.height)return;
  if(vista3D){ pintar3D(); return; }
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

  /* Alcance mecanico dos elos: onde a ponta chegaria sem limite nenhum. */
  ct.beginPath();ct.arc(ox,oy,alc*esc,0,TAU);
  ct.arc(ox,oy,Math.abs(L1-L2)*esc,0,TAU,true);
  ct.fillStyle="rgba("+C.grade+",.045)";ct.fill();
  ct.strokeStyle="rgba("+C.grade+",.5)";ct.lineWidth=1;
  ct.setLineDash([4,6]);ct.stroke();ct.setLineDash([]);

  /* Area que o braco alcanca DE VERDADE com o curso calibrado.

     Nao da para desenhar isto tracando a borda do retangulo de limites
     das juntas e preenchendo: a cinematica direta e 2-para-1 (cotovelo
     para cima e para baixo dao o mesmo ponto), entao essa borda se
     cruza sozinha e o preenchimento sai com buracos -- um "yin-yang"
     que nao tem nada a ver com a area real.

     Em coordenadas polares a conta e direta. Para um braco 2R:

        raio     r(t2) = |L1 + L2.e^(i.t2)|      -- so depende de t2
        direcao  fi    = t1 + atan2(L2.sen t2, L1 + L2.cos t2)

     Ou seja: cada valor de t2 da UM raio, e t1 varre um arco nesse
     raio. Desenhando um arco por amostra de t2, com espessura igual ao
     passo radial, a regiao sai preenchida certa, sem winding nenhum. */
  if(D.cal1&&D.cal2&&D.protCurso&&D.j1max>D.j1min&&D.j2max>D.j2min){
    const m=0.5;                       /* MARGEM_LIMITE_GRAUS */
    const a0=D.j1min+m,a1=D.j1max-m,b0=D.j2min+m,b1=D.j2max-m;
    if(a1>a0&&b1>b0){
      const N=180,g=Math.PI/180;
      const raio=function(t2){return Math.hypot(L1+L2*Math.cos(t2*g),L2*Math.sin(t2*g));};
      ct.save();
      ct.strokeStyle="rgba("+C.grade+",.12)";
      /* Perto de t2 = 0 o raio quase nao muda: dezenas de arcos cairiam
         no mesmo pixel e o alfa se somaria num aro escuro. So desenha
         quando o raio andou o suficiente para valer um traco novo. */
      let ultimoR=-999;
      for(let k=0;k<N;k++){
        const t2a=b0+(b1-b0)*k/N, t2b=b0+(b1-b0)*(k+1)/N, t2=(t2a+t2b)/2;
        const Ra=raio(t2a)*esc, Rb=raio(t2b)*esc, R=(Ra+Rb)/2;
        if(R<0.5)continue;
        if(Math.abs(R-ultimoR)<0.9)continue;
        ultimoR=R;
        /* Espessura LOCAL: perto de t2=0 o raio quase nao muda e a faixa
           e fina; nas pontas ele varia rapido e a faixa engorda. Usar a
           mesma espessura em toda a varredura empilhava dezenas de arcos
           na borda externa e deixava um aro escuro grosso. */
        ct.lineWidth=Math.max(1.2,Math.abs(Rb-Ra)+1);
        const psi=Math.atan2(L2*Math.sin(t2*g),L1+L2*Math.cos(t2*g));
        ct.beginPath();ct.arc(ox,oy,R,-(a1*g+psi),-(a0*g+psi));ct.stroke();
      }
      ct.restore();

      /* Contorno: as duas bordas de t1 (raios) e as duas de t2 (arcos). */
      ct.save();
      ct.strokeStyle=C.arco;ct.lineWidth=1.5;ct.globalAlpha=.8;
      const borda=function(t1fixo){
        ct.beginPath();
        for(let k=0;k<=N;k++){
          const t2=b0+(b1-b0)*k/N,q=ponta(t1fixo,t2,L1,L2),t=P(q[0],q[1]);
          if(k)ct.lineTo(t[0],t[1]);else ct.moveTo(t[0],t[1]);
        }
        ct.stroke();
      };
      borda(a0);borda(a1);
      [b0,b1].forEach(function(t2){
        const R=raio(t2)*esc;
        if(R<0.5)return;
        const psi=Math.atan2(L2*Math.sin(t2*g),L1+L2*Math.cos(t2*g));
        ct.beginPath();ct.arc(ox,oy,R,-(a1*g+psi),-(a0*g+psi));ct.stroke();
      });
      ct.restore();
    }
  }

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

  /* Area util ensinada: o retangulo de onde a ponta nao sai. */
  if(D.mesaOn){
    const a=P(D.mesaX0,D.mesaY0), b=P(D.mesaX1,D.mesaY1);
    ct.fillStyle="rgba("+C.grade+",.10)";
    ct.fillRect(Math.min(a[0],b[0]),Math.min(a[1],b[1]),
                Math.abs(b[0]-a[0]),Math.abs(b[1]-a[1]));
    ct.strokeStyle=C.arco;ct.globalAlpha=.6;
    ct.setLineDash([7,5]);ct.lineWidth=2;
    ct.strokeRect(Math.min(a[0],b[0]),Math.min(a[1],b[1]),
                  Math.abs(b[0]-a[0]),Math.abs(b[1]-a[1]));
    ct.setLineDash([]);ct.globalAlpha=1;
  }

  /* braco: espessura proporcional ao comprimento de cada elo */
  const PZ=postura();
  const t1=PZ.t1*Math.PI/180,t2=(PZ.t1+PZ.t2)*Math.PI/180;
  const c=P(L1*Math.cos(t1),L1*Math.sin(t1));
  const p=P(L1*Math.cos(t1)+L2*Math.cos(t2),L1*Math.sin(t1)+L2*Math.sin(t2));
  const e1=Math.max(4,L1*esc*0.085), e2=Math.max(3,L2*esc*0.068);

  /* Fantasma do comandado: onde o firmware ACHA que o braco esta. */
  if(PZ.medido && PZ.desvio > DESVIO_VISIVEL){
    const f1=PZ.c1*Math.PI/180, f2=(PZ.c1+PZ.c2)*Math.PI/180;
    const fc=P(L1*Math.cos(f1),L1*Math.sin(f1));
    const fp=P(L1*Math.cos(f1)+L2*Math.cos(f2),L1*Math.sin(f1)+L2*Math.sin(f2));
    ct.save();
    ct.strokeStyle=C.letra3;ct.globalAlpha=.42;
    ct.setLineDash([6,5]);ct.lineWidth=Math.max(2,e2*.5);ct.lineCap="round";
    ct.beginPath();ct.moveTo(ox,oy);ct.lineTo(fc[0],fc[1]);ct.lineTo(fp[0],fp[1]);
    ct.stroke();
    ct.setLineDash([]);ct.globalAlpha=1;ct.restore();
  }

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

  ct.fillStyle=C.letra3;ct.globalAlpha=.8;
  ct.font="10px ui-monospace,Menlo,monospace";ct.textAlign="left";
  ct.fillText(legendaPostura(PZ),12,h-14);
  ct.globalAlpha=1;
  if(!D.protEnv){
    ct.fillStyle=C.letra2;ct.globalAlpha=.75;
    ct.textAlign="right";
    ct.fillText("protecao de mesa e base desligada",w-12,h-14);
    ct.textAlign="left";ct.globalAlpha=1;
  }

  /* desenho importado, enquanto o operador o posiciona */
  if(posOn&&dxfCaminhos){
    const r=posPontos();
    ct.save();
    r.forEach(function(c){
      /* o contorno inteiro em azul; so os pontos fora do alcance em
         vermelho, para o operador ver ONDE precisa mexer */
      ct.strokeStyle=C.arco;ct.lineWidth=2;ct.globalAlpha=.9;
      ct.beginPath();
      c.forEach(function(p,i){const a=P(p[0],p[1]);
        if(i)ct.lineTo(a[0],a[1]);else ct.moveTo(a[0],a[1]);});
      ct.stroke();ct.globalAlpha=1;
      c.forEach(function(p){
        const dentro=alcancavel(p[0],p[1]);
        const a=P(p[0],p[1]);
        ct.fillStyle=dentro?C.arco:C.brasa;
        ct.beginPath();ct.arc(a[0],a[1],dentro?2:4,0,TAU);ct.fill();
      });
    });
    ct.restore();
  }

  /* traco a mao livre em andamento e os pontos que ele vai virar */
  if(desOn&&tracado.length>1){
    ct.strokeStyle=C.arco;ct.lineWidth=2;ct.globalAlpha=.8;
    ct.beginPath();
    tracado.forEach(function(q,i){const a=P(q[0],q[1]);
      if(i)ct.lineTo(a[0],a[1]);else ct.moveTo(a[0],a[1]);});
    ct.stroke();ct.globalAlpha=1;
    ct.fillStyle=desSolda?C.quente:C.arco;
    resumo.forEach(function(q){const a=P(q[0],q[1]);
      ct.beginPath();ct.arc(a[0],a[1],3.5,0,TAU);ct.fill();});
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
function mmDe(e){
  const r=cv.getBoundingClientRect();
  return [(e.clientX-r.left-ox)/esc,(oy-(e.clientY-r.top))/esc];
}
cv.addEventListener("click",function(e){
  /* No modo desenho o toque e traco; no modo posicionar e arraste.
     Nem um nem outro manda o braco para o ponto tocado. */
  if(desOn||posOn)return;
  const q=mmDe(e);
  post("/api/mover_xy?x="+q[0].toFixed(1)+"&y="+q[1].toFixed(1));
});

/* =====================================================================
   Desenhar sobre a mesa.
   O dedo risca o caminho em cima do desenho do braco; o traco e
   simplificado aqui (Douglas-Peucker) e vira o programa de pontos no
   firmware. Dali em diante e um programa como qualquer outro: da para
   ensaiar, repetir, editar ponto a ponto e salvar no cartao.
   ===================================================================== */
/* MAX_PONTOS do firmware. Chega no /api/status: deixar o numero fixo
   aqui fazia a pagina simplificar para um limite que o robo nao tem
   mais. Ate a primeira resposta vale o valor conservador. */
let MAX_PTS=40;
const AMOSTRA_MM=2;            /* o dedo gera eventos demais para guardar todos */
let desOn=false,desenhando=false,tracado=[],resumo=[],desSolda=false;

function distReta(p,a,b){
  const dx=b[0]-a[0],dy=b[1]-a[1],L=Math.hypot(dx,dy);
  if(L<1e-6)return Math.hypot(p[0]-a[0],p[1]-a[1]);
  return Math.abs(dy*(p[0]-a[0])-dx*(p[1]-a[1]))/L;
}
function dp(p,ini,fim,tol,marca){
  let pior=0,idx=-1;
  for(let i=ini+1;i<fim;i++){
    const d=distReta(p[i],p[ini],p[fim]);
    if(d>pior){pior=d;idx=i;}}
  if(idx>0&&pior>tol){marca[idx]=1;dp(p,ini,idx,tol,marca);dp(p,idx,fim,tol,marca);}
}
function simplificar(p,tol){
  if(p.length<3)return p.slice();
  const marca=new Array(p.length).fill(0);
  marca[0]=1;marca[p.length-1]=1;
  dp(p,0,p.length-1,tol,marca);
  return p.filter(function(_,i){return marca[i];});
}
/* Aperta a tolerancia ate o traco caber nos 40 pontos do programa. Cortar
   pelo fim perderia o resto do desenho sem avisar. */
function enxugar(p){
  let tol=1.5,r=simplificar(p,tol);
  for(let k=0;k<30&&r.length>MAX_PTS;k++){tol*=1.5;r=simplificar(p,tol);}
  return r.length>MAX_PTS?r.slice(0,MAX_PTS):r;
}
function desContar(){
  resumo=tracado.length>1?enxugar(tracado):tracado.slice();
  $("dCnt").textContent = tracado.length<2
    ? "risque com o dedo sobre a mesa"
    : tracado.length+" amostras \u2192 "+resumo.length+" pontos";
  $("dEnviar").disabled = resumo.length<2;
  $("dLimpar").disabled = !tracado.length;
}
function desModo(v){
  desOn=v;desenhando=false;
  document.body.dataset.des=v?"1":"0";
  cv.classList.toggle("des",v);
  $("zDes").classList.toggle("on",v);
  if(!v){tracado=[];resumo=[];}
  desContar();
}
$("zDes").onclick=function(){desModo(!desOn);};
$("dLimpar").onclick=function(){tracado=[];desContar();};
$("dSolda").onclick=function(){
  desSolda=!desSolda;
  $("dSolda").textContent="cordao: "+(desSolda?"sim":"nao");
  $("dSolda").classList.toggle("quente",desSolda);
};
let arrastando=false,arrasteDe=null;
cv.addEventListener("pointerdown",function(e){
  if(!desOn&&!posOn)return;
  e.preventDefault();
  try{cv.setPointerCapture(e.pointerId);}catch(x){}
  if(posOn){arrastando=true;arrasteDe=mmDe(e);return;}
  desenhando=true;tracado=[mmDe(e)];desContar();
});
cv.addEventListener("pointermove",function(e){
  if(posOn){
    if(!arrastando)return;
    const q=mmDe(e);
    T.tx+=q[0]-arrasteDe[0];T.ty+=q[1]-arrasteDe[1];
    arrasteDe=q;posContar();return;
  }
  if(!desOn||!desenhando)return;
  const q=mmDe(e),u=tracado[tracado.length-1];
  if(u&&Math.hypot(q[0]-u[0],q[1]-u[1])<AMOSTRA_MM)return;
  tracado.push(q);desContar();
});
/* Sem pointerleave: sair do disco arrastando nao pode cortar o traco. */
["pointerup","pointercancel","lostpointercapture"].forEach(function(v){
  cv.addEventListener(v,function(){desenhando=false;arrastando=false;});
});
$("dEnviar").onclick=function(){
  if(resumo.length<2){erro="risque um traco maior";return;}
  const corpo=resumo.map(function(q){
    return q[0].toFixed(1)+","+q[1].toFixed(1);}).join(";");
  fetch("/api/prog/desenho?solda="+(desSolda?1:0),
        {method:"POST",headers:{"Content-Type":"text/plain"},body:corpo})
   .then(function(r){
     if(!r.ok)return r.text().then(function(t){throw new Error(t);});
     erro="";desModo(false);return lerPontos();})
   .catch(function(e){erro=e.message||"o robo nao respondeu";});
};
desModo(false);

/* =====================================================================
   Importar DXF.

   O arquivo e lido AQUI, no aparelho. O ESP32 recebe so a lista de
   pontos pronta: um DXF de 300 kB nao cabe na RAM dele, e um leitor de
   DXF em C ocuparia flash que o robo precisa para o resto.

   DXF ASCII e uma lista de pares (codigo, valor), um por linha. So a
   secao ENTITIES interessa, e dela so o que vira trajeto.
   ===================================================================== */
const DXF_SAG_MM=0.15;   /* flecha maxima ao aproximar arco por cordas */
const DXF_SOLDA_MM=0.15; /* dois pontos a menos que isto sao o mesmo ponto */

function dxfPares(txt){
  const L=txt.split(/\r\n|\r|\n/), P=[];
  for(let i=0;i+1<L.length;i+=2){
    const c=parseInt(L[i],10);
    if(!isNaN(c))P.push([c,L[i+1].trim()]);
  }
  return P;
}

/* Aproxima um arco por cordas com flecha <= DXF_SAG_MM. */
function dxfArco(cx,cy,r,a0,a1,saida){
  let d=a1-a0;
  while(d<=0)d+=Math.PI*2;
  const passo=r>DXF_SAG_MM ? 2*Math.acos(1-DXF_SAG_MM/r) : Math.PI/4;
  const n=Math.max(2,Math.ceil(d/Math.max(passo,1e-3)));
  for(let k=0;k<=n;k++){
    const a=a0+d*k/n;
    saida.push([cx+r*Math.cos(a),cy+r*Math.sin(a)]);
  }
}

/* Bulge de LWPOLYLINE: b = tan(theta/4) do arco entre dois vertices. */
function dxfBulge(p0,p1,b,saida){
  const th=4*Math.atan(b);
  const dx=p1[0]-p0[0],dy=p1[1]-p0[1],c=Math.hypot(dx,dy);
  if(c<1e-9||Math.abs(th)<1e-9){saida.push(p1);return;}
  const r=c/(2*Math.sin(Math.abs(th)/2));
  const h=Math.sqrt(Math.max(0,r*r-c*c/4))*(Math.abs(th)>Math.PI?-1:1)*(th>0?1:-1);
  const mx=(p0[0]+p1[0])/2,my=(p0[1]+p1[1])/2;
  const cx=mx-h*dy/c, cy=my+h*dx/c;
  const a0=Math.atan2(p0[1]-cy,p0[0]-cx), a1=Math.atan2(p1[1]-cy,p1[0]-cx);
  const tmp=[];
  if(th>0)dxfArco(cx,cy,r,a0,a1,tmp);
  else     {dxfArco(cx,cy,r,a1,a0,tmp);tmp.reverse();}
  for(let i=1;i<tmp.length;i++)saida.push(tmp[i]);
}

/* Devolve {caminhos:[[[x,y],...],...], contagem:{...}} em unidades do arquivo. */
function dxfEntidades(P){
  const caminhos=[], cont={LINE:0,LWPOLYLINE:0,POLYLINE:0,ARC:0,CIRCLE:0,ignorados:0};
  let i=0;
  /* pula tudo ate ENTITIES; se o arquivo nao tiver a secao, varre inteiro */
  for(let k=0;k<P.length;k++)
    if(P[k][0]===2&&P[k][1]==="ENTITIES"){i=k+1;break;}

  function juntar(){                       /* le os pares ate o proximo codigo 0 */
    const e={};
    while(i<P.length&&P[i][0]!==0){
      const c=P[i][0],v=P[i][1];
      (e[c]=e[c]||[]).push(v);
      i++;
    }
    return e;
  }
  const num=function(e,c,p){const a=e[c];return a&&a.length?parseFloat(a[0]):p;};

  while(i<P.length){
    if(P[i][0]!==0){i++;continue;}
    const tipo=P[i][1];i++;
    if(tipo==="ENDSEC"||tipo==="EOF")break;
    const e=juntar();

    if(tipo==="LINE"){
      caminhos.push([[num(e,10,0),num(e,20,0)],[num(e,11,0),num(e,21,0)]]);
      cont.LINE++;
    }else if(tipo==="CIRCLE"){
      const c=[];dxfArco(num(e,10,0),num(e,20,0),num(e,40,0),0,Math.PI*2,c);
      caminhos.push(c);cont.CIRCLE++;
    }else if(tipo==="ARC"){
      const c=[];
      dxfArco(num(e,10,0),num(e,20,0),num(e,40,0),
              num(e,50,0)*Math.PI/180,num(e,51,0)*Math.PI/180,c);
      caminhos.push(c);cont.ARC++;
    }else if(tipo==="LWPOLYLINE"){
      const xs=e[10]||[],ys=e[20]||[],bs=e[42]||[];
      /* O bulge vem intercalado; sem indice confiavel, so se aplica
         quando ha um por vertice. Sem isso, corda reta -- que e o pior
         caso aceitavel, nunca um arco no lugar errado. */
      const usarB=bs.length===xs.length;
      const v=[];
      for(let k=0;k<Math.min(xs.length,ys.length);k++)
        v.push([parseFloat(xs[k]),parseFloat(ys[k])]);
      if(v.length>1){
        const fech=(parseInt((e[70]&&e[70][0])||"0",10)&1)===1;
        const c=[v[0]];
        for(let k=1;k<v.length;k++){
          const b=usarB?parseFloat(bs[k-1]):0;
          if(b)dxfBulge(v[k-1],v[k],b,c);else c.push(v[k]);
        }
        if(fech){
          const b=usarB?parseFloat(bs[v.length-1]):0;
          if(b)dxfBulge(v[v.length-1],v[0],b,c);else c.push(v[0]);
        }
        caminhos.push(c);cont.LWPOLYLINE++;
      }
    }else if(tipo==="POLYLINE"){
      /* Estilo antigo: os vertices vem como entidades VERTEX ate SEQEND. */
      const c=[];
      while(i<P.length){
        if(P[i][0]!==0){i++;continue;}
        const t2=P[i][1];i++;
        if(t2==="SEQEND")break;
        const v=juntar();
        if(t2==="VERTEX")c.push([num(v,10,0),num(v,20,0)]);
      }
      if(c.length>1){caminhos.push(c);cont.POLYLINE++;}
    }else if(tipo!=="SEQEND"){
      cont.ignorados++;
    }
  }
  return {caminhos:caminhos,cont:cont};
}

/* Emenda caminhos cujas pontas se encostam: um contorno de CAD chega
   picado em dezenas de LINE soltas, e sem emendar cada uma viraria um
   cordao separado com deslocamento no meio. */
function dxfEmendar(cs){
  const perto=function(a,b){return Math.hypot(a[0]-b[0],a[1]-b[1])<=DXF_SOLDA_MM;};
  const rest=cs.slice(), saida=[];
  while(rest.length){
    let c=rest.shift();
    let mexeu=true;
    while(mexeu){
      mexeu=false;
      for(let k=0;k<rest.length;k++){
        const o=rest[k];
        const a=c[0],z=c[c.length-1],oa=o[0],oz=o[o.length-1];
        if(perto(z,oa)){c=c.concat(o.slice(1));}
        else if(perto(z,oz)){c=c.concat(o.slice(0,-1).reverse());}
        else if(perto(a,oz)){c=o.slice(0,-1).concat(c);}
        else if(perto(a,oa)){c=o.slice(1).reverse().concat(c);}
        else continue;
        rest.splice(k,1);mexeu=true;break;
      }
    }
    saida.push(c);
  }
  return saida;
}

/* Ordem de execucao: sempre o contorno cuja ponta esta mais perto de
   onde o anterior terminou. Reduz o deslocamento morto entre cordoes. */
function dxfOrdenar(cs){
  if(cs.length<2)return cs;
  const rest=cs.slice(), saida=[rest.shift()];
  while(rest.length){
    const fim=saida[saida.length-1][saida[saida.length-1].length-1];
    let melhor=0,dm=Infinity,inv=false;
    rest.forEach(function(c,k){
      const d0=Math.hypot(c[0][0]-fim[0],c[0][1]-fim[1]);
      const d1=Math.hypot(c[c.length-1][0]-fim[0],c[c.length-1][1]-fim[1]);
      if(d0<dm){dm=d0;melhor=k;inv=false;}
      if(d1<dm){dm=d1;melhor=k;inv=true;}
    });
    const c=rest.splice(melhor,1)[0];
    saida.push(inv?c.slice().reverse():c);
  }
  return saida;
}

/* =====================================================================
   Posicionar o desenho importado sobre a mesa.

   O CAD nao sabe onde fica a base do braco. Aqui o desenho e um objeto
   que se arrasta, gira, espelha e redimensiona em cima da area util, com
   a conta de alcance refeita a cada quadro: o operador ve os pontos que
   caem fora ficarem vermelhos e mexe ate zerar, em vez de descobrir na
   recusa.
   ===================================================================== */
let dxfCaminhos=null;       /* unidades do arquivo, como veio */
let posOn=false, posSolda=true;
const T={tx:0,ty:0,ang:0,esc:1,esp:1};   /* esp = -1 espelha em X */

function ikNav(x,y,cima){
  const L1=D.l1||200,L2=D.l2||200;
  let c2=(x*x+y*y-L1*L1-L2*L2)/(2*L1*L2);
  if(c2>1){if(c2>1.0005)return null;c2=1;}
  if(c2<-1){if(c2<-1.0005)return null;c2=-1;}
  let a=Math.acos(c2);if(!cima)a=-a;
  return [(Math.atan2(y,x)-Math.atan2(L2*Math.sin(a),L1+L2*Math.cos(a)))*180/Math.PI,
          a*180/Math.PI];
}
/* Espelha posturaValidaDet() do firmware. O robo continua sendo a
   autoridade: isto e so para o operador nao posicionar as cegas. */
function alcancavel(x,y){
  if(!(D.cal1&&D.cal2))return true;         /* modo de instalacao */
  const L1=D.l1||200,L2=D.l2||200,m=0.5;
  for(let k=0;k<2;k++){
    const q=ikNav(x,y,k===0);
    if(!q)continue;
    const t1=q[0],t2=q[1];
    if(D.protCurso&&(t1<D.j1min+m||t1>D.j1max-m||t2<D.j2min+m||t2>D.j2max-m))continue;
    if(D.protDobra&&Math.abs(t2)>180-(D.dobra||0))continue;
    if(D.protEnv){
      const g=Math.PI/180, xc=L1*Math.cos(t1*g), yc=L1*Math.sin(t1*g);
      if(Math.min(y,yc)<D.envY)continue;
      /* distancia da base ao segmento cotovelo-ponta */
      const dx=x-xc,dy=y-yc,L2q=dx*dx+dy*dy;
      let u=L2q>1e-6?(-xc*dx-yc*dy)/L2q:0;
      u=Math.max(0,Math.min(1,u));
      if(Math.hypot(xc+dx*u,yc+dy*u)<D.envR)continue;
    }
    return true;
  }
  return false;
}

function posAplicarT(p){
  const c=Math.cos(T.ang),s=Math.sin(T.ang);
  const x=p[0]*T.esc*T.esp, y=p[1]*T.esc;
  return [T.tx+x*c-y*s, T.ty+x*s+y*c];
}
function posTransformado(){
  return dxfCaminhos.map(function(c){return c.map(posAplicarT);});
}
function posCaixa(cs){
  let x0=Infinity,y0=Infinity,x1=-Infinity,y1=-Infinity;
  cs.forEach(function(c){c.forEach(function(p){
    if(p[0]<x0)x0=p[0];if(p[0]>x1)x1=p[0];
    if(p[1]<y0)y0=p[1];if(p[1]>y1)y1=p[1];});});
  return [x0,y0,x1,y1];
}

/* Simplifica cada contorno separadamente e aperta a tolerancia ate o
   total caber no programa. Cortar pelo fim perderia contorno inteiro. */
function posPontos(){
  const cs=posTransformado();
  let tol=0.3,r=cs.map(function(c){return simplificar(c,tol);});
  const total=function(a){return a.reduce(function(n,c){return n+c.length;},0);};
  for(let k=0;k<40&&total(r)>MAX_PTS;k++){
    tol*=1.4;r=cs.map(function(c){return simplificar(c,tol);});
  }
  return r;
}

function posContar(){
  if(!posOn||!dxfCaminhos)return;
  const r=posPontos();
  let n=0,fora=0;
  r.forEach(function(c){c.forEach(function(p){n++;if(!alcancavel(p[0],p[1]))fora++;});});
  const el=$("pCnt");
  const cabe=n<=MAX_PTS;
  el.className="cnt"+(fora||!cabe?" ruim":" bom");
  el.textContent = fora ? (fora+" de "+n+" pontos fora do alcance")
                 : !cabe ? (n+" pontos: o programa guarda "+MAX_PTS)
                 : (n+" pontos, todos dentro · "+Math.round(T.esc*100)+"% · "+
                    Math.round(T.ang*180/Math.PI)+"°");
  $("pAplicar").disabled = !!fora || !cabe || n<2;
  return r;
}

function posCentralizar(){
  if(!dxfCaminhos)return;
  const cs=posTransformado();
  const b=posCaixa(cs);
  /* Alvo: o centro do arco util, na frente da base. */
  const L1=D.l1||200,L2=D.l2||200;
  T.tx += (L1+L2)*0.55 - (b[0]+b[2])/2;
  T.ty += 0 - (b[1]+b[3])/2;
  posContar();
}

function posModo(v){
  posOn=v&&!!dxfCaminhos;
  document.body.dataset.pos=posOn?"1":"0";
  if(posOn){desModo(false);irAba("mesa");}
  posContar();
}

$("btDxfAbrir").onclick=function(){$("dxfArq").click();};
$("dxfArq").onchange=function(){
  const f=$("dxfArq").files&&$("dxfArq").files[0];
  if(!f)return;
  const fr=new FileReader();
  fr.onload=function(){
    let r;
    try{ r=dxfEntidades(dxfPares(String(fr.result))); }
    catch(e){ $("dxfInfo").textContent="nao consegui ler este arquivo";
              erro="DXF ilegivel";return; }
    const esc=parseFloat($("dxfEsc").value)||1;
    let cs=dxfOrdenar(dxfEmendar(r.caminhos))
            .map(function(c){return c.map(function(p){return [p[0]*esc,p[1]*esc];});})
            .filter(function(c){return c.length>1;});
    if(!cs.length){
      dxfCaminhos=null;
      $("dxfInfo").textContent="nenhuma linha, polilinha, arco ou circulo neste arquivo";
      $("sbDxf").textContent="sem geometria";
      acao("DxfPos","este arquivo nao tem geometria de trajeto");
      return;
    }
    dxfCaminhos=cs;
    const b=posCaixa(cs);
    let n=0;cs.forEach(function(c){n+=c.length;});
    $("dxfInfo").textContent=
      f.name+"\n"+cs.length+" contorno(s), "+n+" pontos brutos"+
      "\n"+Math.round(b[2]-b[0])+" x "+Math.round(b[3]-b[1])+" mm"+
      "\n"+r.cont.LINE+" LINE · "+r.cont.LWPOLYLINE+" LWPOLYLINE · "+
      r.cont.POLYLINE+" POLYLINE · "+r.cont.ARC+" ARC · "+
      r.cont.CIRCLE+" CIRCLE"+
      (r.cont.ignorados?("\n"+r.cont.ignorados+" entidade(s) ignorada(s)"):"");
    $("sbDxf").textContent=cs.length+" contorno(s)";
    /* Recomeca a transformacao e joga o desenho na frente do braco. */
    T.tx=0;T.ty=0;T.ang=0;T.esc=1;T.esp=1;
    posCentralizar();
    acao("DxfPos","");
  };
  fr.onerror=function(){erro="nao consegui abrir o arquivo";};
  fr.readAsText(f);
};
$("btDxfPos").onclick=function(){posModo(true);};
acao("DxfPos","escolha um arquivo DXF primeiro");
$("pSolda").classList.add("quente");
$("pCancel").onclick =function(){posModo(false);};

/* ---------- origem do desenho marcada COM O BRACO ----------
   Arrastar o desenho na tela pede que o operador saiba onde a peca esta
   em milimetros. Na bancada ele nao sabe: sabe onde a peca ESTA, porque
   esta olhando para ela. Entao o caminho e o contrario -- solta o braco,
   leva a ponta ate onde o desenho comeca, confirma, e o desenho vai
   para la.

   Usa o modo aprendizado para soltar: e ele que mantem o encoder
   acompanhando o braco solto, e sem isso a posicao lida seria onde o
   firmware ACHA que a ponta esta, nao onde ela esta. */
let origemEsperando=false, origemViuSolto=false;
$("pOrigem").onclick=function(){
  if(!origemEsperando){
    origemEsperando=true;
    origemViuSolto=false;
    post("/api/aprender?on=1");
    return;
  }
  /* Confirmar: o PRIMEIRO ponto do desenho vai para a ponta. E o
     primeiro, e nao o centro, porque foi ali que o operador encostou --
     "onde o desenho comeca" e uma frase sobre o comeco. */
  origemEsperando=false;
  post("/api/aprender?on=0");
  const cs=posTransformado();
  if(!cs.length||!cs[0].length)return;
  const p0=cs[0][0];
  T.tx += (D.x||0)-p0[0];
  T.ty += (D.y||0)-p0[1];
  posContar();pintar();
};

/* O botao conta o que fazer agora, e some quando nao ha desenho. */
function origemPintar(d){
  const b=$("pOrigem"); if(!b) return;
  b.textContent = origemEsperando
    ? tr("confirmar: o desenho comeca aqui")
    : tr("origem com o braco");
  b.classList.toggle("pri", origemEsperando);
  if(!origemEsperando) return;
  /* So se pode dizer que o modo CAIU depois de te-lo visto ligado. O
     status chega a cada 220 ms: cancelar por "d.apr ainda false" logo
     apos o pedido cancelaria sempre, no intervalo entre pedir e o robo
     responder. */
  if(d.apr){ origemViuSolto=true; return; }
  if(!origemViuSolto) return;
  /* Viu ligado e agora nao esta: caiu por fora -- emergencia, alarme, o
     botao da ponteira. Cancelar em silencio deixaria o botao mentindo. */
  origemEsperando=false;
  b.textContent=tr("origem com o braco");
  b.classList.remove("pri");
}
$("pGirarM").onclick =function(){T.ang-=Math.PI/12;posContar();};
$("pGirarP").onclick =function(){T.ang+=Math.PI/12;posContar();};
$("pMaior").onclick  =function(){T.esc*=1.1;posContar();};
$("pMenor").onclick  =function(){T.esc/=1.1;posContar();};
$("pEsp").onclick    =function(){T.esp=-T.esp;posContar();};
$("pSolda").onclick  =function(){
  posSolda=!posSolda;
  $("pSolda").textContent="cordao: "+(posSolda?"sim":"nao");
  $("pSolda").classList.toggle("quente",posSolda);
};
$("pCentro").onclick =posCentralizar;
$("pAplicar").onclick=function(){
  const r=posContar();
  if(!r)return;
  /* Terceiro campo por ponto: cordao ao longo de cada contorno,
     deslocamento na emenda de um para o outro. */
  const partes=[];
  r.forEach(function(c,ic){
    c.forEach(function(p,ip){
      const ultimoDoContorno=(ip===c.length-1);
      const s=(ultimoDoContorno||!posSolda)?0:1;
      partes.push(p[0].toFixed(1)+","+p[1].toFixed(1)+","+s);
    });
    void ic;
  });
  fetch("/api/prog/desenho",{method:"POST",
        headers:{"Content-Type":"text/plain"},body:partes.join(";")})
   .then(function(x){
     if(!x.ok)return x.text().then(function(t){throw new Error(t);});
     erro="";posModo(false);return lerPontos();})
   .catch(function(e){erro=e.message||"o robo nao respondeu";});
};

/* =====================================================================
   Rede: so mostrar por onde se chega no painel.
   A maquina tem Wi-Fi proprio e nada a configurar.
   ===================================================================== */
function redeAtualizar(){
  return fetch("/api/rede").then(function(r){return r.json();}).then(function(d){
    $("redeEnd").textContent=
      "rede Wi-Fi \""+d.ssid+"\"\n\n"+
      "http://"+d.ip+"\n"+
      "http://"+d.nome+".local";
    $("sbRede").textContent=d.ssid+" · "+d.ip;
  }).catch(function(){});
}
redeAtualizar();

/* =====================================================================
   Encoder.

   O grafico mostra COMANDADO MENOS MEDIDO, em graus da junta. E a
   grandeza que interessa: linha reta em zero quer dizer que o braco foi
   para onde foi mandado; degrau ou deriva quer dizer passo perdido.

   O historico vive aqui no navegador. O ESP32 nao guarda serie temporal
   -- ele publica o instante, e quem desenha e quem tem memoria de sobra.
   ===================================================================== */
/* Por que nao esta lendo. Espelha MotivoEncoder em encoder.h -- tela que
   so diz "nada" nao ensina ninguem. */
const MOTIVO=["ok","aguardando","sem resposta","quadro corrompido",
              "registrador recusado","formato inesperado"];
const ENC_AMOSTRAS=240;          /* uns 60 s a 4 Hz de consulta */
const encHist=[[],[]];
/* Amostra INTEIRA, nao so o erro: e o que a analise detalhada mostra e o
   que vai para o CSV. Guardar so o erro obrigaria a olhar duas telas
   para responder "o erro subiu porque o braco andou ou porque a leitura
   falhou?" -- que e a primeira pergunta de sempre. */
const encAmostras=[];
let encT0=0;
let encD=null, encCarregou=false;

const cvEnc=$("cvEnc"), ctEnc=cvEnc?cvEnc.getContext("2d"):null;
const cvPos=$("cvPos"), ctPos=cvPos?cvPos.getContext("2d"):null;

/* ---------------------------------------------------------------------
   As duas rodinhas.

   Cada uma e a junta vista de cima. Ponteiro GROSSO = onde o encoder diz
   que o eixo esta. Ponteiro FINO = onde o firmware mandou. A abertura
   entre os dois e o erro, e da para ver sem ler numero.

   No centro, um disco pequeno gira com a volta do MOTOR (a contagem
   modulo uma volta). Ele e a prova visual de que a leitura esta viva: se
   o braco anda e o disco nao gira, o dado morreu.
   --------------------------------------------------------------------- */
const RODAS=[$("cvR1"),$("cvR2")];
function rodaMedir(cv){
  if(!cv)return null;
  const d=window.devicePixelRatio||1,r=cv.parentElement.getBoundingClientRect();
  if(r.width<2||r.height<2)return null;
  cv.width=Math.round(r.width*d);cv.height=Math.round(r.height*d);
  const ct=cv.getContext("2d");ct.setTransform(d,0,0,d,0,0);
  return ct;
}
function rodaPintar(i,d){
  const cv=RODAS[i];if(!cv)return;
  const ct=rodaMedir(cv);if(!ct)return;
  const C=paleta(),dp=window.devicePixelRatio||1;
  const w=cv.width/dp,h=cv.height/dp;
  const cx=w/2, cy=h/2-4, R=Math.min(w,h)/2-14;
  ct.clearRect(0,0,w,h);

  const L=(d.j||[])[i]||{};
  const cmd=(i===0)?d.t1:d.t2;
  const lim=(i===0)?[d.j1min,d.j1max]:[d.j2min,d.j2max];
  const temFaixa=(typeof lim[0]==="number")&&(lim[1]>lim[0]);
  const g=Math.PI/180;
  /* Angulo cresce no anti-horario e o canvas cresce no horario: o menos
     no seno e o que faz a rodinha girar para o mesmo lado do braco. */
  const P=function(a,r){return [cx+r*Math.cos(a*g),cy-r*Math.sin(a*g)];};

  /* faixa util, quando ha calibracao */
  ct.lineWidth=7;
  ct.strokeStyle="rgba("+C.grade+",.5)";
  ct.beginPath();ct.arc(cx,cy,R,0,Math.PI*2);ct.stroke();
  if(temFaixa){
    ct.strokeStyle=C.arco;ct.globalAlpha=.25;
    ct.beginPath();ct.arc(cx,cy,R,-lim[1]*g,-lim[0]*g);ct.stroke();
    ct.globalAlpha=1;
  }

  /* marcas de 30 em 30 graus */
  ct.strokeStyle="rgba("+C.grade+",.9)";ct.lineWidth=1;
  for(let a=0;a<360;a+=30){
    const p1=P(a,R-6),p2=P(a,R-11);
    ct.beginPath();ct.moveTo(p1[0],p1[1]);ct.lineTo(p2[0],p2[1]);ct.stroke();
  }

  /* ponteiro fino: comandado */
  if(typeof cmd==="number"){
    const p=P(cmd,R-13);
    ct.strokeStyle=C.arco;ct.lineWidth=1.8;
    ct.beginPath();ct.moveTo(cx,cy);ct.lineTo(p[0],p[1]);ct.stroke();
  }

  /* ponteiro grosso: medido */
  if(L.ok){
    const p=P(L.graus,R-13);
    ct.strokeStyle=C.quente;ct.lineWidth=4;ct.lineCap="round";
    ct.beginPath();ct.moveTo(cx,cy);ct.lineTo(p[0],p[1]);ct.stroke();
    ct.lineCap="butt";
  }

  /* disco central que gira com a volta do motor */
  const rc=Math.max(12,R*0.3);
  ct.fillStyle=C.papel;ct.beginPath();ct.arc(cx,cy,rc,0,Math.PI*2);ct.fill();
  ct.strokeStyle="rgba("+C.grade+",.9)";ct.lineWidth=1;ct.stroke();
  if(L.ok){
    const cvVolta=(i===0)?d.cv1:d.cv2;
    const voltas=cvVolta>0?((L.bruto-L.ref)/cvVolta):0;
    const giro=(voltas-Math.floor(voltas))*360;
    ct.save();ct.translate(cx,cy);ct.rotate(-giro*g);
    ct.strokeStyle=C.quente;ct.lineWidth=2.5;
    ct.beginPath();ct.moveTo(0,0);ct.lineTo(rc-3,0);ct.stroke();
    ct.fillStyle=C.quente;
    ct.beginPath();ct.arc(rc-3,0,2.5,0,Math.PI*2);ct.fill();
    ct.restore();
  }

  /* numero no meio */
  ct.fillStyle=L.ok?C.letra:C.letra3;
  ct.font="600 "+Math.max(11,Math.round(R*0.22))+"px ui-monospace,Menlo,monospace";
  ct.textAlign="center";ct.textBaseline="middle";
  ct.fillText(L.ok?(L.graus.toFixed(1)+"°"):"--", cx, cy+rc+Math.max(11,R*0.18));
}


function medirTela(cv,ct){
  if(!cv||!ct)return;
  const d=window.devicePixelRatio||1,r=cv.parentElement.getBoundingClientRect();
  if(r.width<2||r.height<2)return;
  cv.width=Math.round(r.width*d);cv.height=Math.round(r.height*d);
  ct.setTransform(d,0,0,d,0,0);
}
function encMedir(){ medirTela(cvEnc,ctEnc); medirTela(cvPos,ctPos); }
addEventListener("resize",encMedir);

function encPintar(){
  if(!ctEnc||!cvEnc.width)return;
  const C=paleta();
  const dp=window.devicePixelRatio||1;
  const w=cvEnc.width/dp,h=cvEnc.height/dp;
  ctEnc.clearRect(0,0,w,h);
  ctEnc.fillStyle=C.papel;ctEnc.fillRect(0,0,w,h);

  /* Escala simetrica em torno do zero, com piso: sem piso, ruido de
     centesimo de grau viraria montanha e assustaria por nada. */
  let pico=0.5;
  encHist.forEach(function(s){s.forEach(function(v){
    const a=Math.abs(v);if(a>pico)pico=a;});});
  pico=pico*1.15;

  /* grade e rotulos */
  ctEnc.strokeStyle="rgba("+C.grade+",.45)";ctEnc.lineWidth=1;
  ctEnc.fillStyle=C.letra3;
  ctEnc.font="9px ui-monospace,Menlo,monospace";ctEnc.textAlign="left";
  [-1,-0.5,0,0.5,1].forEach(function(f){
    const y=h/2-f*(h/2-10);
    ctEnc.beginPath();ctEnc.moveTo(34,y);ctEnc.lineTo(w-6,y);
    if(f===0){ctEnc.strokeStyle=C.arco;ctEnc.globalAlpha=.5;}
    ctEnc.stroke();
    ctEnc.strokeStyle="rgba("+C.grade+",.45)";ctEnc.globalAlpha=1;
    ctEnc.fillText((f*pico).toFixed(2)+"°",4,y+3);
  });

  /* as duas juntas */
  [[0,C.arco],[1,C.quente]].forEach(function(par){
    const s=encHist[par[0]];
    if(s.length<2)return;
    ctEnc.strokeStyle=par[1];ctEnc.lineWidth=1.8;
    ctEnc.beginPath();
    s.forEach(function(v,i){
      const x=34+(w-40)*i/(ENC_AMOSTRAS-1);
      const y=h/2-(v/pico)*(h/2-10);
      if(i)ctEnc.lineTo(x,y);else ctEnc.moveTo(x,y);
    });
    ctEnc.stroke();
  });
}

/* ---------------------------------------------------------------------
   Grafico da POSICAO MEDIDA.

   O grafico de erro nao responde "o erro subiu porque o braco andou ou
   porque a leitura falhou?". Este responde: rampa limpa e movimento;
   degrau vertical sem o braco ter andado e leitura falhando; linha reta
   com o braco andando e leitura morta.
   --------------------------------------------------------------------- */
function posPintar(){
  if(!ctPos||!cvPos.width)return;
  const C=paleta();
  const dp=window.devicePixelRatio||1;
  const w=cvPos.width/dp,h=cvPos.height/dp;
  ctPos.clearRect(0,0,w,h);
  ctPos.fillStyle=C.papel;ctPos.fillRect(0,0,w,h);
  if(encAmostras.length<2)return;

  /* Escala pelos dois eixos juntos: comparar as juntas na mesma regua e
     metade do valor do grafico. */
  let lo=Infinity,hi=-Infinity;
  encAmostras.forEach(function(a){
    [a.g1,a.g2].forEach(function(v){
      if(v===null)return;
      if(v<lo)lo=v; if(v>hi)hi=v;});});
  if(!isFinite(lo)){lo=-1;hi=1;}
  if(hi-lo<0.5){const m=(hi+lo)/2;lo=m-0.25;hi=m+0.25;}
  const folga=(hi-lo)*0.1; lo-=folga; hi+=folga;

  ctPos.strokeStyle="rgba("+C.grade+",.45)";ctPos.lineWidth=1;
  ctPos.fillStyle=C.letra3;
  ctPos.font="9px ui-monospace,Menlo,monospace";ctPos.textAlign="left";
  for(let k=0;k<=4;k++){
    const y=10+(h-20)*k/4, v=hi-(hi-lo)*k/4;
    ctPos.beginPath();ctPos.moveTo(40,y);ctPos.lineTo(w-6,y);ctPos.stroke();
    ctPos.fillText(v.toFixed(1)+"°",4,y+3);
  }

  const n=encAmostras.length;
  [["g1",C.arco],["g2",C.quente]].forEach(function(par){
    ctPos.strokeStyle=par[1];ctPos.lineWidth=1.8;
    ctPos.beginPath();
    let caneta=false;
    encAmostras.forEach(function(a,i){
      const v=a[par[0]];
      if(v===null){caneta=false;return;}   /* buraco fica buraco, nao vira reta */
      const x=40+(w-46)*i/(n-1);
      const y=10+(h-20)*(hi-v)/(hi-lo);
      if(caneta)ctPos.lineTo(x,y);else{ctPos.moveTo(x,y);caneta=true;}
    });
    ctPos.stroke();
  });
}

/* ---------------------------------------------------------------------
   Estatisticas e tabela.
   --------------------------------------------------------------------- */
function anCel(id,txt){const b=$(id);if(b)b.textContent=txt;}

function analisar(d){
  const j=d.j||[];
  [0,1].forEach(function(i){
    const L=j[i]||{}, k=i+1, campo=i===0?"e1":"e2";
    const reg=(i===0)?d.reg1:d.reg2;
    const cv =(i===0)?d.cv1:d.cv2;

    if(!reg){
      ["anN","anF","anHz","anMe","anMx","anSd","anBr","anVo","anId",
       "anVe","anRp","anSe","anPa","anIv","anFx"]
        .forEach(function(x){anCel(x+k,"--");});
      return;
    }
    anCel("anN"+k,String(L.n||0));
    anCel("anF"+k,String(L.falhas||0));

    /* Leituras por segundo medidas na janela, nao o periodo pedido: o
       que importa e o que a linha ESTA dando, nao o que foi configurado. */
    const dt=encAmostras.length>1
      ?(encAmostras[encAmostras.length-1].t-encAmostras[0].t)/1000:0;
    const nJan=encAmostras.filter(function(a){return a[campo]!==null;}).length;
    anCel("anHz"+k,dt>0.5?(nJan/dt).toFixed(1)+"/s":"--");

    const vals=encAmostras.map(function(a){return a[campo];})
                          .filter(function(v){return v!==null;});
    if(vals.length){
      const soma=vals.reduce(function(a,b){return a+b;},0);
      const med=soma/vals.length;
      let pior=0; vals.forEach(function(v){if(Math.abs(v)>Math.abs(pior))pior=v;});
      const varia=vals.reduce(function(a,v){return a+(v-med)*(v-med);},0)/vals.length;
      anCel("anMe"+k,(med>=0?"+":"")+med.toFixed(3)+"°");
      anCel("anMx"+k,(pior>=0?"+":"")+pior.toFixed(3)+"°");
      anCel("anSd"+k,Math.sqrt(varia).toFixed(3)+"°");
    }else{
      anCel("anMe"+k,"--");anCel("anMx"+k,"--");anCel("anSd"+k,"--");
    }

    anCel("anBr"+k,L.ok?String(L.bruto):"--");
    anCel("anVo"+k,(L.ok&&cv>0)?((L.bruto-L.ref)/cv).toFixed(3):"--");
    anCel("anId"+k,L.ok?((L.idade||0)+" ms"):"--");

    /* Derivados: vem prontos do firmware, que tem os instantes de
       verdade. O navegador so formata. */
    anCel("anVe"+k,L.ok?Math.round(L.vel||0)+" c/s":"--");
    anCel("anRp"+k,L.ok?(L.rpm||0).toFixed(1)+" rpm":"--");
    anCel("anSe"+k,!L.ok?"--":(L.sent>0?"▲ cresce":L.sent<0?"▼ decresce":"parado"));
    anCel("anPa"+k,String(L.passos||0));
    anCel("anIv"+k,String(L.inv||0));
    /* Faixa percorrida: so faz sentido depois de ter andado. */
    const faixa=(L.bmax||0)-(L.bmin||0);
    anCel("anFx"+k,faixa>0?String(faixa):"--");
  });

  const total=encAmostras.length;
  $("sbAnal").textContent = total
    ? total+" amostras guardadas" : "tudo que foi captado";

  /* Tabela: as ultimas, mais novas em cima. Mais que isso o operador nao
     le, e o CSV leva tudo. */
  const tb=$("tabEnc").tBodies[0];
  let html="<tr><th>t</th><th>bruto 1</th><th>med 1</th><th>erro 1</th>"+
           "<th>med 2</th><th>erro 2</th></tr>";
  const ini=Math.max(0,total-40);
  for(let i=total-1;i>=ini;i--){
    const a=encAmostras[i];
    const f=function(v,casas){return v===null?"--":v.toFixed(casas||2);};
    const ruim1=a.e1!==null&&Math.abs(a.e1)>0.5?" class=\"ruim\"":"";
    const ruim2=a.e2!==null&&Math.abs(a.e2)>0.5?" class=\"ruim\"":"";
    html+="<tr><td>"+(a.t/1000).toFixed(1)+"s</td>"+
          "<td>"+(a.b1===null?"--":a.b1)+"</td>"+
          "<td>"+f(a.g1)+"</td><td"+ruim1+">"+f(a.e1)+"</td>"+
          "<td>"+f(a.g2)+"</td><td"+ruim2+">"+f(a.e2)+"</td></tr>";
  }
  tb.innerHTML=html;
}

/* ---------------------------------------------------------------------
   Assentamento pelo encoder.
   --------------------------------------------------------------------- */
const CORR=["parado","conferindo","retocando","conferido","nao fechou","recusado"];
let corrCarregou=false;

function corrAplicar(d){
  const est=CORR[d.crEst||0]||"--";
  anCel("crEst",est);
  anCel("crOk",String(d.crOk||0)+(d.crFalha?(" / "+d.crFalha+" nao"):""));
  anCel("crAlerta",String(d.crAlerta||0));
  $("crMotivo").textContent=d.crMotivo||"--";

  /* Travamento fica na tela ATE o operador dizer que resolveu. Um aviso
     que some sozinho e um aviso que ninguem leu -- e este quer dizer que
     o eixo estava forcando contra alguma coisa. */
  const trav=$("crTrav");
  if(d.trvOn){
    trav.style.display="";
    $("crTravTxt").textContent=
      "JUNTA "+d.trvJ+" TRAVOU\n"+
      "O comando andou e o eixo nao. Encostou no batente, o acoplamento "+
      "soltou, ou o driver desarmou.\nO eixo foi parado: nao ficou forcando.";
  }else{
    trav.style.display="none";
  }
  if(d.trvN)$("sbCorr").textContent="travou "+d.trvN+"x";
  $("sbCorr").textContent = !d.crOn ? "desligado"
    : (d.crEst===5||d.crEst===4) ? "atencao" : est;
  if(!corrCarregou){
    corrCarregou=true;
    $("crOnCh").className ="ch"+(d.crOn?" on":"");
    $("crVigCh").className="ch"+(d.crVig?" on":"");
    $("crTol").value=d.crTol; $("crMax").value=d.crMax;
    $("crAlr").value=d.crAlr; $("crTent").value=d.crTent;
  }
}

["crOnCh","crVigCh"].forEach(function(id){
  $(id).onclick=function(){$(id).classList.toggle("on");};
});
/* ---------------------------------------------------------------------
   Zero absoluto. A secao nasce trancada em toda visita: o tranco existe
   para nao se mexer sem querer, e "sem querer" inclui ter deixado aberto
   ontem.
   --------------------------------------------------------------------- */
const ZERO=["esperando o encoder","localizado","indo para o zero",
            "pronto","sem encoder"];
let zCarregou=false;
$("etZero").classList.add("trancado");
$("zCadeado").onclick=function(){
  $("etZero").classList.toggle("trancado");
  $("zCadeado").querySelector(".ic").textContent=
    $("etZero").classList.contains("trancado")?"\u{1F512}":"\u{1F513}";
};

function zeroAplicar(d){
  const est=ZERO[d.zEst||0]||"--";
  const ens=(d.zEn1?1:0)+(d.zEn2?1:0);
  $("sbZero").textContent = ens===0 ? "nao ensinado" : est;
  $("zEstado").textContent =
    (d.zEn1?("junta 1: zero ensinado"+(d.zEst>=1?", leu "+(d.zG1||0).toFixed(2)+"° ao ligar":""))
           :"junta 1: zero NAO ensinado")+"\n"+
    (d.zEn2?("junta 2: zero ensinado"+(d.zEst>=1?", leu "+(d.zG2||0).toFixed(2)+"° ao ligar":""))
           :"junta 2: zero NAO ensinado")+"\n"+
    (d.zMot||"");
  if(!zCarregou){
    zCarregou=true;
    $("zSinCh").className="ch"+(d.zSin?" on":"");
    $("zIrCh").className ="ch"+(d.zIr?" on":"");
    $("zTol").value=d.zTol;
  }
}

["zSinCh","zIrCh"].forEach(function(id){
  $(id).onclick=function(){$(id).classList.toggle("on");};
});
$("btZsalvar").onclick=function(){
  const on=function(id){return $(id).classList.contains("on")?1:0;};
  post("/api/zero/config?sin="+on("zSinCh")+"&ir="+on("zIrCh")+
       "&tol="+$("zTol").value).then(function(){zCarregou=false;});
};
$("btZensinar").onclick=function(){
  const g=parseFloat($("zG").value);
  if(!isFinite(g))return;
  if(!confirm("Gravar que a junta "+$("zJ").value+" esta AGORA em "+g+" graus?\n\n"+
              "O zero e a origem de onde os limites de curso sao contados. "+
              "Se o braco nao estiver mesmo nesse angulo, a area util inteira "+
              "sai do lugar."))return;
  post("/api/zero/ensinar?j="+$("zJ").value+"&g="+g)
   .then(function(){zCarregou=false;carregou=false;});
};
$("btZesquecer").onclick=function(){
  if(!confirm("Esquecer o zero absoluto das duas juntas?\n\n"+
              "A maquina volta a ligar como antes: sem se localizar e sem ir "+
              "a lugar nenhum sozinha."))return;
  post("/api/zero/esquecer?j=0").then(function(){zCarregou=false;});
};

$("btTravOk").onclick=function(){post("/api/travamento/ok");};
$("btCorrSalvar").onclick=function(){
  const on=function(id){return $(id).classList.contains("on")?1:0;};
  post("/api/correcao?on="+on("crOnCh")+"&vig="+on("crVigCh")+
       "&tol="+$("crTol").value+"&max="+$("crMax").value+
       "&alr="+$("crAlr").value+"&tent="+$("crTent").value)
   .then(function(){corrCarregou=false;});
};

function encCsv(){
  let txt="ms,bruto1,medido1,comandado1,erro1,vel1,rpm1,"+
          "bruto2,medido2,comandado2,erro2,vel2,rpm2\n";
  encAmostras.forEach(function(a){
    const c=function(v){return v===null?"":v;};
    txt+=a.t+","+c(a.b1)+","+c(a.g1)+","+c(a.c1)+","+c(a.e1)+","+
             c(a.v1)+","+c(a.r1)+","+
             c(a.b2)+","+c(a.g2)+","+c(a.c2)+","+c(a.e2)+","+
             c(a.v2)+","+c(a.r2)+"\n";
  });
  const u=URL.createObjectURL(new Blob([txt],{type:"text/csv"}));
  const a=document.createElement("a");
  a.href=u; a.download="encoder.csv";
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(function(){URL.revokeObjectURL(u);},1000);
  $("qEncCsv").textContent=encAmostras.length+" amostras baixadas";
}

function encCelula(id,texto,ruim){
  const b=$(id);if(!b)return;
  b.textContent=texto;
  const cel=b.parentElement;
  if(cel&&cel.classList.contains("err"))cel.classList.toggle("ruim",!!ruim);
}

function encAplicar(d){
  encD=d;
  const j=d.j||[];
  [0,1].forEach(function(i){
    const L=j[i]||{};
    const cmd=(i===0)?d.t1:d.t2;
    encCelula("eC"+(i+1),cmd.toFixed(2)+"°");
    if(L.ok){
      encCelula("eM"+(i+1),L.graus.toFixed(2)+"°");
      /* Meio grau ja e mais do que qualquer folga sadia num braco de
         solda: acima disso a celula fica vermelha. */
      encCelula("eE"+(i+1),(L.erro>=0?"+":"")+L.erro.toFixed(2)+"°",
                Math.abs(L.erro)>0.5);
      encHist[i].push(L.erro);
    }else{
      /* Registrador 0 quer dizer "esta junta nao foi ligada ainda".
         Nao e falha: e o estado normal de quem so tem um driver na
         bancada, e chamar isso de falha assusta a toa. */
      const reg=(i===0)?d.reg1:d.reg2;
      encCelula("eM"+(i+1), !d.ativo?"desligado"
                          : !reg?"nao ligada"
                          : MOTIVO[L.motivo||1]);
      encCelula("eE"+(i+1),"--",false);
      /* Sem leitura o historico continua andando com zero, senao o
         grafico mente dizendo que estava tudo bem no buraco. */
      if(encHist[i].length)encHist[i].push(0);
    }
    while(encHist[i].length>ENC_AMOSTRAS)encHist[i].shift();
  });

  /* Amostra inteira, para a analise detalhada e para o CSV. */
  if(!encT0)encT0=Date.now();
  const L1a=j[0]||{}, L2a=j[1]||{};
  encAmostras.push({
    t: Date.now()-encT0,
    b1: L1a.ok?L1a.bruto:null, g1: L1a.ok?L1a.graus:null,
    c1: d.t1, e1: L1a.ok?L1a.erro:null,
    v1: L1a.ok?L1a.vel:null, r1: L1a.ok?L1a.rpm:null,
    b2: L2a.ok?L2a.bruto:null, g2: L2a.ok?L2a.graus:null,
    c2: d.t2, e2: L2a.ok?L2a.erro:null,
    v2: L2a.ok?L2a.vel:null, r2: L2a.ok?L2a.rpm:null,
  });
  while(encAmostras.length>ENC_AMOSTRAS)encAmostras.shift();

  window.__encN=encHist[0].length;   /* o banco de interface confere */

  const L1=j[0]||{},L2=j[1]||{};
  $("sbEnc").textContent = !d.ativo ? "desligado"
    : (L1.ok||L2.ok) ? "lendo" : "sem resposta";
  /* Junta sem registrador nao aparece com contador de falha: ela nem
     chega a ser perguntada. */
  /* Angulo fora de qualquer escala e a assinatura de encoder mal
     configurado: contagens por volta erradas, formato de 32 bits errado,
     ou o registrador do vizinho. O firmware ja recusa a leitura -- se
     nao recusasse, o braco desenhado giraria sem parar atras dela. Mas
     recusar em silencio deixa o operador sem saber o que consertar. */
  const ABSURDO=720;
  const linhaJunta=(n,reg,L)=>
    "junta "+n+": "+(!reg ? "nao ligada"
      : (L.n||0)+" leituras, "+(L.falhas||0)+" falhas"+
        (L.ok?("   bruto "+L.bruto):"")+
        ((L.graus!==undefined&&Math.abs(L.graus)>ABSURDO)
          ? ("\n   " +Math.round(L.graus)+"\u00b0 -- fora de escala. "+
             "Confira contagens por volta, o formato de 32 bits e o registrador")
          : ""));
  $("encEstado").textContent=
    linhaJunta(1,d.reg1,L1)+"\n"+linhaJunta(2,d.reg2,L2)+
    "\n"+d.baud+" bps  ·  funcao "+d.func+"  ·  "+d.per+" ms";
  $("encQuadro").textContent=d.quadro||"--";

  if(!encCarregou){
    encCarregou=true;
    $("encAtivo").className="ch"+(d.ativo?" on":"");
    $("enc32").className  ="ch"+(d.b32?" on":"");
    $("encLo").className  ="ch"+(d.lo?" on":"");
    $("encBaud").value=d.baud;$("encPar").value=d.par;
    $("encFunc").value=d.func;$("encPer").value=d.per;
    $("encId1").value=d.id1;$("encReg1").value=d.reg1;$("encCv1").value=d.cv1;
    $("encId2").value=d.id2;$("encReg2").value=d.reg2;$("encCv2").value=d.cv2;
  }
  encMedir();encPintar();posPintar();analisar(d);corrAplicar(d);zeroAplicar(d);
  rodaPintar(0,d);rodaPintar(1,d);
}

function encAtualizar(){
  return fetch("/api/encoder").then(function(r){return r.json();})
   .then(encAplicar).catch(function(){});
}

/* As chaves sao locais ate o operador salvar: mudar o formato do valor a
   cada clique reabriria a UART no meio da leitura. */
["encAtivo","enc32","encLo"].forEach(function(id){
  $(id).onclick=function(){$(id).classList.toggle("on");};
});
$("btEncSalvar").onclick=function(){
  const on=function(id){return $(id).classList.contains("on")?1:0;};
  post("/api/encoder/config?ativo="+on("encAtivo")+
       "&baud="+$("encBaud").value+"&par="+$("encPar").value+
       "&func="+$("encFunc").value+"&per="+$("encPer").value+
       "&b32="+on("enc32")+"&lo="+on("encLo")+
       "&id1="+$("encId1").value+"&reg1="+$("encReg1").value+"&cv1="+$("encCv1").value+
       "&id2="+$("encId2").value+"&reg2="+$("encReg2").value+"&cv2="+$("encCv2").value)
   .then(function(){encCarregou=false;encHist[0]=[];encHist[1]=[];encAmostras.length=0;encT0=0;});
};
/* O habilita. Carrega uma vez do status para nao apagar o que o operador
   esta digitando a cada atualizacao de tela. */
var sonCarregou=false;
$("sonF16").onclick=function(){$("sonF16").classList.toggle("on");};
$("btSonSalvar").onclick=function(){
  post("/api/son/config?reg="+$("sonReg").value+
       "&liga="+$("sonL").value+"&desl="+$("sonD").value+
       "&f16="+($("sonF16").classList.contains("on")?1:0),"qSonSalvar")
   .then(function(){sonCarregou=false;});
};

/* Estado do ultimo pedido de habilita. SON_OCIOSO=0 PENDENTE=1 OK=2 FALHOU=3.
   O caso que importa e o 3 depois de um desabilitar: o eixo pode estar
   energizado e nao ha segundo caminho para cortar. */
/* Os botoes do motor. A cor e o texto dizem o estado REAL, nao o que foi
   pedido: enquanto o barramento nao confirma o botao fica ambar, porque
   o operador precisa saber a diferenca entre "mandei" e "tem torque". */
function motorPintar(d){
  [1,2].forEach(function(k){
    const ch=$("chSrv"+k);
    if(ch) ch.className="ch"+(((k===1)?d.srv1:d.srv2)?" on":"")+
                        (d.sonEst===1?" indo":"");
    const b=$("btMotor"+k),t=$("btMotor"+k+"T");if(!b||!t)return;
    const nome=tr("EIXO")+" "+k;
    if(d.sonReg===0){
      b.className="motor ruim";t.textContent=nome;
      b.title=tr("Habilita sem registrador: configure em Ajustes");return;
    }
    /* O pedido em curso pode ser de uma junta so ou das duas: sem saber
       de qual, ambar nos dois seria mentira sobre o eixo que ninguem
       mexeu. O firmware ja resolve isso zerando so a junta pedida, entao
       aqui basta olhar o estado dela. */
    if(d.sonEst===1){
      b.className="motor indo";t.textContent="...";
      b.title=tr("falando com os drivers");return;
    }
    const on=(k===1)?d.srv1:d.srv2;
    if(d.sonEst===3&&!on){
      b.className="motor ruim";t.textContent=nome;
      b.title=tr("o barramento nao confirmou o ultimo comando de habilita");return;
    }
    b.className="motor"+(on?" on":"");
    t.textContent=nome;
    b.title=on?tr("Toque para tirar o torque"):tr("Toque para dar torque");
  });
}

function sonPintar(d){
  if(!sonCarregou&&d.sonReg!==undefined){
    sonCarregou=true;
    $("sonReg").value=d.sonReg;$("sonL").value=d.sonL;$("sonD").value=d.sonD;
    $("sonF16").className="ch"+(d.sonF16?" on":"");
  }
  const q=$("qSon");if(!q)return;
  if(d.sonReg===0){
    q.textContent=tr("Habilita sem registrador: configure em Ajustes");
    q.className="pq2 ruim";return;
  }
  if(d.sonEst===1){q.textContent=tr("falando com os drivers...");q.className="pq2";return;}
  if(d.sonEst===3){
    q.textContent=tr("o barramento nao confirmou o ultimo comando de habilita");
    q.className="pq2 ruim";return;
  }
  q.textContent="";q.className="pq2";
}

$("btEncPadroes").onclick=function(){
  post("/api/encoder/padroes").then(function(){
    encCarregou=false;encHist[0]=[];encHist[1]=[];encAmostras.length=0;encT0=0;});
};
/* A cacada e o teste escrevem no mesmo relatorio: e sempre "o que a
   linha respondeu por ultimo". */
const encBuscarRel=function(){
  let tentativas=0;
  const buscar=function(){
    fetch("/api/encoder/teste").then(function(r){return r.text();})
      .then(function(t){
        $("encRel").textContent=t;
        if(/testando|procurando/.test(t) && ++tentativas<25) setTimeout(buscar,500);
      }).catch(function(){});
  };
  setTimeout(buscar,600);
};
$("btEncCsv").onclick=encCsv;
$("btEncTestar").onclick=function(){
  $("encRel").textContent="testando a linha...";
  post("/api/encoder/testar").then(encBuscarRel);
};
$("btEncCacar").onclick=function(){
  $("encRel").textContent="lendo a faixa toda...";
  post("/api/encoder/cacar").then(encBuscarRel);
};
$("btEncComparar").onclick=function(){
  $("encRel").textContent="comparando...";
  post("/api/encoder/cacar?comparar=1").then(encBuscarRel);
};
$("btEncZerar").onclick=function(){
  post("/api/encoder/zerar?j=0").then(function(){
    encHist[0]=[];encHist[1]=[];encAmostras.length=0;encT0=0;});
};

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
/* Motivo comum a tudo que move o braco, na ordem em que o operador
   precisa resolver. O jog NAO exige calibracao -- sem ela o robo esta em
   modo de instalacao, que existe justamente para o operador conseguir
   levar o braco ate os limites. */
function porQueNaoMove(d,exigeCalib){
  if(d.modo==="FALHA")            return "sistema em falha: rearme os servos primeiro";
  if(!d.servos)                   return "habilite os servos (aba Ajustes, etapa 1)";
  if(exigeCalib&&(!d.cal1||!d.cal2))
    return "calibre as juntas (aba Ajustes, etapa 1)";
  if(d.modo!=="MANUAL")           return "robo ocupado: "+(RM[d.modo]||d.modo);
  return "";
}

/* Curso da junta com o braco marcado dentro dele. As bordas vermelhas
   sao a margem de seguranca que a validacao desconta: dali para fora o
   movimento e recusado, entao vale ver que ela existe. */
function faixaJunta(el,calibrada,valor,min,max){
  if(!calibrada){el.textContent="sem curso";return;}
  const curso=max-min;
  if(curso<=0){el.textContent="curso invalido";return;}
  const p=Math.max(0,Math.min(100,(valor-min)/curso*100));
  const mg=Math.min(45,0.5/curso*100);          /* MARGEM_LIMITE_GRAUS */
  const perto=(p<mg*1.5||p>100-mg*1.5);
  el.innerHTML=min.toFixed(0)+"…"+max.toFixed(0)+"° · <b>"+valor.toFixed(1)+"°</b>"+
    '<span class="fxB'+(perto?" perto":"")+'">'+
    '<u style="left:0;width:'+mg.toFixed(2)+'%"></u>'+
    '<u style="right:0;width:'+mg.toFixed(2)+'%"></u>'+
    '<i style="left:'+p.toFixed(2)+'%"></i></span>';
}

function lamp(el,cls,txt){
  el.className="lp"+(cls?" "+cls:"");
  if(txt!==undefined)$("lModoT").textContent=txt;}

function aplicar(d){
  Object.assign(D,d);
  if(!jaEnquadrou){jaEnquadrou=true;autoEnquadrar();}
  sonPintar(d);
  motorPintar(d);
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

  faixaJunta($("fx1"),d.cal1,d.t1,d.j1min,d.j1max);
  faixaJunta($("fx2"),d.cal2,d.t2,d.j2min,d.j2max);

  const t=$("tira");
  t.textContent=erro?("Recusado: "+erro):d.msg;
  t.className="tira"+(erro||d.modo==="FALHA"?" er":(pronto?" ok":""));

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

  /* Aprendizado. Quando esta ligado o braco esta solto: o estado tem de
     aparecer sem o operador ter de procurar. */
  $("btApr").textContent=tr(d.apr?"Sair do modo aprendizado":"Entrar no modo aprendizado");
  $("btApr").className="b "+(d.apr?"rod":"pri");
  aprGuiaPintar(d);
  origemPintar(d);
  $("aprEst").className="aprEst"+(d.apr?" on":"");
  $("aprEst").textContent=d.apr
    ? ((d.aprSolto?"APRENDENDO · braco solto":"APRENDENDO · com torque, use as setas")
       +" · "+d.aprN+" ponto"+(d.aprN===1?"":"s")+" nesta sessao")
    : (d.aprBotao?"desligado · botao da ponteira instalado":"desligado");
  acao("Apr", d.apr ? ""
       : d.modo!=="MANUAL" ? "so a partir do modo manual: "+(RM[d.modo]||d.modo)
       : (!d.cal1||!d.cal2) ? "calibre as juntas antes de ensinar pontos"
       : d.movendo ? "espere o braco parar" : "");
  $("btPrec").textContent="Precisao: "+(d.precisao?"ligada":"desligada");

  /* Leitura de angulo: comandado em cima, medido pelo encoder embaixo.
     Divergencia acima de meio grau fica vermelha -- abaixo disso e o
     tremor normal de um encoder de 17 bits e nao quer dizer nada. */
  [[1,d.m1,d.m1ok,d.t1],[2,d.m2,d.m2ok,d.t2]].forEach(function(k){
    const el=$("hM"+k[0]);
    if(!k[2]){el.textContent="sem leitura";el.className="med sem";return;}
    const dif=k[1]-k[3];
    el.textContent=k[1].toFixed(2)+"° medido"+
      (Math.abs(dif)>=0.05?(dif>0?"  +":"  ")+dif.toFixed(2):"");
    el.className="med"+(Math.abs(dif)>0.5?" dif":"");
  });

  /* Producao */
  $("btPausa").textContent=tr(d.pausa?"Retomar":"Pausar");
  $("btPausa").className="b "+(d.pausa?"pri":"");
  acao("Pausa", rodando ? "" : "nao ha programa em execucao");
  acao("Repetir", rodando ? "programa em execucao"
       : (d.progN<2) ? "grave pelo menos 2 pontos"
       : porQueNaoMove(d,true));
  $("contPecas").textContent = d.pausa
    ? ("PAUSADO no trecho "+(d.progIdx+1)+"\u2192"+(d.progIdx+2)+", a "+d.trecho+"% dele")
    : (d.ciclos+" peca"+(d.ciclos===1?"":"s")+" no total \u00b7 "+
       d.cicSes+" nesta sessao");
  acao("Desf", d.desf ? "" : "nada para desfazer");

  $("btIdioma").textContent=(idioma==="en")?"Portugues":"English";
  $("sbIdioma").textContent=(idioma==="en")?"english":"portugues";

  pintarSoldar();
  $("btEnsaio").textContent=tr((rodando&&d.ensaio)?"Parar ensaio":"Executar ensaio");
  $("btEnsaio").className="b "+((rodando&&d.ensaio)?"rod":"pri");
  acao("Ensaio", (rodando&&d.ensaio) ? ""
       : (rodando&&!d.ensaio) ? "execucao com arco em andamento"
       : (d.progN<2) ? "grave pelo menos 2 pontos na aba Mover"
       : porQueNaoMove(d,true));
  $("e3").classList.toggle("feita",d.progN>=2&&!rodando);
  acao("Soldar", (rodando&&!d.ensaio) ? ""
       : (rodando&&d.ensaio) ? "ensaio em andamento"
       : (d.progN<2) ? "grave pelo menos 2 pontos na aba Mover"
       : porQueNaoMove(d,true));
  $("pg").style.width=(rodando?d.progPct:0)+"%";

  const passo=!pronto?1:(d.progN<2?2:(rodando&&!d.ensaio?4:3));
  document.querySelectorAll(".et").forEach(function(x){
    x.classList.toggle("agora",+x.dataset.e===passo);});

  $("sInv1").className="ch"+(d.inv1?" on":"");
  $("sInv2").className="ch"+(d.inv2?" on":"");
  $("cInv1").className="ch"+(d.inv1?" on":"");
  $("cInv2").className="ch"+(d.inv2?" on":"");

  const temCal=d.cal1&&d.cal2;
  /* Estava desabilitando direto, sem dizer por que: botao apagado e mudo
     e a reclamacao mais antiga deste painel. Vai por acao(), como todos
     os outros. */
  acao("CalApagar2",
       (d.modo!=="MANUAL"&&d.modo!=="CALIBRANDO")
         ? "apague a calibracao com o robo parado: "+(RM[d.modo]||d.modo)
       : !temCal ? "nao ha calibracao gravada para apagar" : "");

  $("pCur").className="ch"+(d.protCurso?" on":"");
  $("pDob").className="ch"+(d.protDobra?" on":"");
  $("pEnv").className="ch"+(d.protEnv?" on":"");

  $("resumoRes").textContent=
    "J1 · "+d.ppg1.toFixed(2)+" pulsos por grau"+
    "\nJ2 · "+d.ppg2.toFixed(2)+" pulsos por grau"+
    /* O que cada junta vai pedir ao driver na velocidade de jog: e aqui
       que se ve se algum eixo esta perto do teto do T3D. */
    "\nno jog o driver recebe "+Math.round(d.velN*d.ppg1)+" Hz e "+
    Math.round(d.velN*d.ppg2)+" Hz";

  /* Em palavras, nao em campos: o resumo e o que o operador le antes de
     decidir se precisa mexer em alguma coisa. */
  const qv=qualPreset(PRE_VEL,[d.velN,d.velP,d.velA,d.velCordao]);
  const qr=qualPreset(PRE_RAMPA,[d.acel1,d.suav]);
  $("resumoVel").textContent=
    (qv==="custom"?"ajuste proprio":qv)+
    " · jog "+d.velN.toFixed(0)+" °/s · precisao "+d.velP.toFixed(1)+
    " °/s · deslocamento "+d.velA.toFixed(0)+" °/s · cordao "+
    d.velCordao.toFixed(1)+" mm/s";
  $("resumoRampa").textContent=
    (qr==="custom"?"ajuste proprio":qr)+
    " · aceleracao "+d.acel1.toFixed(0)+" °/s² · suavidade "+d.suav;
  $("sbAjustes").textContent=
    (qv==="custom"?"velocidade propria":"velocidade "+qv)+
    " · partida "+(qr==="custom"?"propria":qr);
  const nProt=(d.protCurso?1:0)+(d.protDobra?1:0)+(d.protEnv?1:0);
  $("resumoArea").textContent = nProt===3
    ? "as tres protecoes ligadas"
    : nProt===0 ? "NENHUMA protecao ligada: o braco aceita qualquer postura"
    : nProt+" de 3 protecoes ligadas -- "+
      (!d.protCurso?"fim de curso ":"")+(!d.protDobra?"cotovelo ":"")+
      (!d.protEnv?"mesa e base ":"")+"desligada(s)";
  if(!carregou){
    mostrarPreset("segVel","velCustom",qv);
    mostrarPreset("segRampa","rampaCustom",qr);
  }

  if(!carregou){
    carregou=true;
    $("inVn").value=d.velN;$("inVp").value=d.velP;$("inVa").value=d.velA;
    $("inVc").value=d.velCordao;$("inVc2").value=d.velCordao;
    $("inA1").value=d.acel1;$("inA2").value=d.acel2;
    $("inSuav").value=d.suav;
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

    const med=$("cMed");
    /* A conferencia de sentido so na etapa de referencia: dali em diante
       ja ha limite medido, e trocar o sinal do eixo inverteria o
       significado do que foi medido. */
    $("cSent").style.display=(d.calib==="HOME")?"block":"none";
    if(d.calib==="HOME"){
      med.style.display="block";
      $("cMedL1").textContent="Junta 1 esta em";
      $("cMedL2").textContent="Junta 2 esta em";
      if(ultCal!=="HOME"){$("cG1").value=0;$("cG2").value=0;}
      $("cMedNota").innerHTML=
        "Deixe <b>0 e 0</b> se o braco estiver na postura que a cinematica "+
        "chama de zero: <b>elo 1 apontando para a direita, na horizontal</b>, "+
        "e <b>elo 2 alinhado com o elo 1</b> (braco esticado). Se a sua "+
        "referencia for outra postura, informe aqui os angulos reais dela "+
        "&mdash; sem isso o desenho na tela sai girado em relacao a maquina.";
    }else if(d.calib==="CONCLUIDO"){
      med.style.display="block";
      $("cMedL1").textContent="Curso real da junta 1";
      $("cMedL2").textContent="Curso real da junta 2";
      const c1=Math.abs(d.j1max-d.j1min), c2=Math.abs(d.j2max-d.j2min);
      if(ultCal!=="CONCLUIDO"){
        $("cG1").value=c1.toFixed(1);$("cG2").value=c2.toFixed(1);}
      $("cMedNota").innerHTML=
        "Pelos <b>pulsos contados</b> e pela resolucao que esta nos ajustes, "+
        "o curso deu <b>"+c1.toFixed(1)+"°</b> na junta 1 e <b>"+c2.toFixed(1)+
        "°</b> na junta 2.<br><br>Meca o curso de verdade com transferidor ou "+
        "inclinometro e corrija aqui. O firmware recalcula a resolucao pelos "+
        "pulsos que acabou de contar &mdash; e a maior base de medida que a "+
        "maquina tem, entao sai preciso. Batendo com o medido, e so confirmar.";
    }else{
      med.style.display="none";
    }
  }
  ultCal=d.calib;

  $("sbTraj").textContent=d.trajN<2?"nenhuma gravada":
    (d.trajN+" pontos · "+(d.trajMs/1000).toFixed(1)+" s");
  /* O cartao diz em que pe a gravacao esta. Sem isto o operador aperta
     "Iniciar gravacao", nada muda na tela em que ele esta, e ele conclui
     que o botao nao faz nada -- foi exatamente a queixa. */
  const gv=$("gravBox");
  if(d.modo==="GRAVANDO"){
    gv.className="gravBox on";
    $("gravTit").textContent=tr("GRAVANDO")+" · "+d.trajN+" "+tr("amostras");
    $("gravMsg").textContent=tr("Mova o braco: pelo joystick da aba Mover, pelos botoes de passo, ou com a mao se o modo aprendizado estiver ligado.");
  }else if(d.trajN>=2){
    gv.className="gravBox tem";
    $("gravTit").textContent=tr("Trajetoria na memoria");
    $("gravMsg").textContent=d.trajN+" "+tr("amostras")+" · "+
      (d.trajMs/1000).toFixed(1)+" s · "+tr("reproduza abaixo ou salve na aba Arquivos");
  }else{
    gv.className="gravBox";
    $("gravTit").textContent=tr("Parado");
    $("gravMsg").textContent=tr("Nada gravado ainda. Aperte Iniciar gravacao e mova o braco.");
  }
  acao("GravIni", d.modo==="GRAVANDO" ? "ja esta gravando" : porQueNaoMove(d,false));
  acao("GravFim", d.modo==="GRAVANDO" ? "" : "nao ha gravacao em andamento");
  acao("Repro", (d.trajN<2) ? "nenhuma trajetoria gravada" : porQueNaoMove(d,true));
  $("btArco").textContent=d.solda?"FECHAR ARCO":"Abrir arco";
  $("btArco").className="b "+(d.solda?"rod":"quente");
  acao("Arco", d.solda ? ""
       : (d.modo!=="MANUAL"&&d.modo!=="GRAVANDO") ? "arco manual so no modo manual ou gravando"
       : !d.servos ? "habilite os servos (aba Ajustes, etapa 1)" : "");
  acao("Mover", porQueNaoMove(d,true));
  acao("TrajLimpar", (d.trajN<2) ? "nao ha trajetoria para apagar"
       : (d.modo!=="MANUAL") ? "so com o robo parado no modo manual" : "");
  const bloqJog=porQueNaoMove(d);
  const instalacao=d.servos&&!(d.cal1&&d.cal2)&&d.modo!=="FALHA";
  joy.classList.toggle("bloq",!!bloqJog);
  $("joyMotivo").textContent=bloqJog;
  $("sbMover").textContent=bloqJog||(instalacao?"modo de instalacao · jog livre":
    (d.precisao?"precisao · joystick":"joystick das duas juntas"));
  if(!bloqJog&&instalacao){
    $("joyMotivo").textContent=
      "Modo de instalacao: sem calibracao nao ha limite de curso. "+
      "Quem protege sao os batentes da maquina.";
    $("joyMotivo").style.color="var(--letra2)";
  }else{
    $("joyMotivo").style.color="";
  }
  if(d.maxPts>1&&d.maxPts!==MAX_PTS){MAX_PTS=d.maxPts;posContar();}
  if(abaAtual==="arq")sdEstadoSalvar();
  acao("Home", porQueNaoMove(d,true));
  acao("EncZerar", (encD&&encD.ativo) ? "" : "ligue a leitura do encoder");
  acao("EncSalvar", (d.modo==="MANUAL") ? ""
     : "configure o encoder com o robo parado no modo manual");
  /* Zerar reescreve a contagem de pulsos: so com o robo parado. Nao exige
     calibracao -- e justamente o que se usa no modo de instalacao. */
  acao("Refer", porQueNaoMove(d,false));

  acao("AfMarcar", porQueNaoMove(d,false));
  afEstado();

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
  /* Consulta quando o painel do encoder esta NA TELA, nao quando a aba
     esta escolhida. No computador ele virou coluna fixa e nao ha mais
     aba "enc" para escolher -- amarrar a consulta a aba deixava a coluna
     sempre aberta mostrando dado do momento em que a pagina carregou. */
  /* Enquanto o operador esta movendo, o painel do encoder cede a vez. O
     WebServer do ESP32 atende UMA conexao por vez: status + encoder a
     cada 220 ms disputando com o heartbeat do jog de 100 ms atrasava o
     heartbeat, e jog sem heartbeat por 350 ms PARA o eixo -- travada de
     verdade, no motor, nao no desenho. */
  if(!jogando() && $("pnEnc") && $("pnEnc").offsetParent) encAtualizar();
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
/* =====================================================================
   IDIOMAS
   O que e traduzido: TUDO que o operador toca -- abas, botoes, rotulos,
   titulos de secao, mensagens da tira e a tela de saude. O que NAO e
   traduzido: as notas longas de explicacao (os blocos cinza) e os
   paineis de instalacao. Elas sao o manual embutido desta maquina, foram
   escritas para quem a monta, e traduzir mal um texto que explica por que
   o arco fecha na pausa e pior do que deixa-lo em portugues.

   A troca e por dicionario de FRASE INTEIRA, casando o texto exato. Sem
   chave nenhuma espalhada pelo HTML: chave errada quebra em silencio e
   ninguem descobre ate o cliente estrangeiro abrir a tela.
   ===================================================================== */
const EN={
 /* abas */
 "Mesa":"Table","Mover":"Jog","Programa":"Program","Arquivos":"Files",
 "Ajustes":"Setup","Encoder":"Encoder","Maquina":"Machine",
 /* regua */
 "junta 1":"joint 1","junta 2":"joint 2","X mm":"X mm","Y mm":"Y mm",
 "ponta mm/s":"tip mm/s",
 /* botoes principais */
 "Habilitar servos":"Enable servos","Desabilitar servos":"Disable servos",
 "EIXO":"AXIS",
 "Toque para dar torque":"Tap to apply torque",
 "Toque para tirar o torque":"Tap to remove torque",
 "falando com os drivers":"talking to the drives",
 "PARAR":"STOP","Parar":"Stop","Executar ensaio":"Run dry run",
 "Parar ensaio":"Stop dry run","Executar com arco":"Run with arc",
 "Confirmar: abrir o arco":"Confirm: strike the arc",
 "Pausar":"Pause","Retomar":"Resume","Mais uma peca":"One more part",
 "Desfazer":"Undo","Apagar programa":"Clear program",
 "Gravar ponto na posicao atual":"Teach point at current position",
 "Ir para o zero da maquina":"Go to machine zero",
 "Zerar a maquina aqui":"Set machine zero here",
 "Ir para esses angulos":"Go to these angles",
 "Entrar no modo aprendizado":"Enter teach mode",
 "Sair do modo aprendizado":"Leave teach mode",
 "Entrar no modo operador":"Enter operator mode",
 "Sair do modo operador":"Leave operator mode",
 "Registrar manutencao feita":"Log maintenance done",
 "Trocar a senha":"Change password","Enviar firmware":"Upload firmware",
 "Fechar":"Close","Iniciar gravacao":"Start recording",
 "Encerrar gravacao":"Stop recording","Abrir arco":"Strike arc",
 "Restaurar padroes":"Restore defaults",
 "Carregar esta peca na maquina":"Load this part into the machine",
 "carregar":"load","apagar":"delete","ver":"view",
 /* titulos de secao */
 "Ensinar o caminho":"Teach the path","Ensaiar sem arco":"Dry run, no arc",
 "Soldar":"Weld","Modo aprendizado":"Teach mode",
 "Saude da maquina":"Machine health","Registro de eventos":"Event log",
 "Conectar no painel":"Connect to the panel",
 "Atualizar o firmware":"Update firmware",
 "Ir para um angulo":"Go to an angle","Atalhos":"Shortcuts",
 "Velocidade do cordao":"Bead speed","Senha do tecnico":"Technician password",
 "Nova senha":"New password",
 "1. entrar na rede":"1. join the network","2. abrir o painel":"2. open the panel",
 /* tela de saude */
 "Ligada ha":"Powered on for","Pecas prontas":"Parts finished",
 "Interrompidas no meio":"Interrupted midway","Arco aberto, no total":"Total arc time",
 "Desde a manutencao":"Since last maintenance",
 "Encoder junta 1":"Joint 1 encoder","Encoder junta 2":"Joint 2 encoder",
 "Travamentos":"Stalls","Avisos de desvio":"Deviation warnings",
 "Alarme dos drivers":"Drive alarms","Botao de emergencia":"Emergency stop",
 "Botao da ponteira":"Torch button","Cartao":"Card","Memoria livre":"Free memory",
 "Programa na particao":"Program in partition",
 "nenhum":"none","ausente":"absent","instalado":"installed",
 "nao instalado":"not installed","disponivel":"available",
 "indisponivel neste firmware":"unavailable in this firmware",
 "nenhum evento registrado ainda":"no events logged yet",
 "atencao: veja os itens abaixo":"attention: see the items below",
 "ligado: ajustes escondidos":"on: setup hidden",
 "desligado: tudo visivel":"off: everything visible",
 "desligado":"off","sem leitura":"no reading",
 "previa do cartao":"preview from card",
};

let idioma="pt";
function tr(s){
  if(idioma==="pt")return s;
  const k=String(s).trim();
  return (k in EN)?EN[k]:s;
}

/* Percorre o documento trocando NOS DE TEXTO que casem por inteiro. Nao
   toca em nada dentro de .nt (as notas longas) nem em .res, que e saida
   crua da maquina. */
function traduzirDom(raiz){
  if(idioma==="pt")return;
  const it=document.createTreeWalker(raiz||document.body,NodeFilter.SHOW_TEXT,{
    acceptNode:function(n){
      if(!n.nodeValue||!n.nodeValue.trim())return NodeFilter.FILTER_REJECT;
      let p=n.parentElement;
      while(p&&p!==document.body){
        if(p.classList&&(p.classList.contains("nt")||p.classList.contains("res")||
                         p.classList.contains("perigo")))return NodeFilter.FILTER_REJECT;
        p=p.parentElement;
      }
      return NodeFilter.FILTER_ACCEPT;
    }});
  const alvos=[];
  let n;
  while((n=it.nextNode()))alvos.push(n);
  alvos.forEach(function(x){
    const k=x.nodeValue.trim();
    if(k in EN)x.nodeValue=x.nodeValue.replace(k,EN[k]);
  });
}

function definirIdioma(novo){
  idioma=(novo==="en")?"en":"pt";
  try{localStorage.setItem("idioma",idioma);}catch(e){}
  /* Recarregar e mais honesto do que tentar desfazer a traducao no
     lugar: o PT original volta do proprio HTML, sem dicionario reverso
     e sem risco de sobrar meia tela traduzida. */
  location.reload();
}
try{ if(localStorage.getItem("idioma")==="en")idioma="en"; }catch(e){}

const ABAS=[
 ["mesa","Mesa","M4 19h16M6 19V9l6-5 6 5v10"],
 ["mover","Mover","M12 4v16M4 12h16M12 4l-3 3M12 4l3 3M12 20l-3-3M12 20l3-3M4 12l3-3M4 12l3 3M20 12l-3-3M20 12l-3 3"],
 ["prog","Programa","M5 6h14M5 12h9M5 18h5M17 15l2 2 3-4"],
 ["arq","Arquivos","M4 7a2 2 0 012-2h4l2 2h6a2 2 0 012 2v8a2 2 0 01-2 2H6a2 2 0 01-2-2z"],
 ["enc","Encoder","M12 3a9 9 0 100 18 9 9 0 000-18zM12 12l5-3M12 12v-4"],
];
/* Paineis da tela de TRABALHO. Ajustes, configuracao do encoder e
   sistema sairam daqui: moram na gaveta da engrenagem, porque sao coisa
   de instalar uma vez, nao de usar no turno. */
const PANES={mover:"pnMover",prog:"pnProg",arq:"pnArq",enc:"pnEnc"};

(function montarAbas(){
  let h="",t="";
  ABAS.forEach(function(a){
    /* Ajustes e Encoder sao instalacao, nao operacao: somem no modo
       operador. A Mesa, o Mover, o Programa e os Arquivos ficam --
       e com eles a maquina continua fazendo peca o dia inteiro. */
    /* Nao ha mais aba de instalacao na barra: o que sobrou aqui e tudo
       tela de trabalho, e o modo operador nao precisa esconder nada. */
    const cl='';
    h+='<button'+cl+' data-aba="'+a[0]+'"><svg viewBox="0 0 24 24"><path d="'+a[2]+'"/></svg>'+
       '<span>'+a[1]+'</span></button>';
    t+='<button'+cl+' data-aba="'+a[0]+'">'+a[1]+'</button>';
  });
  $("abas").innerHTML=h;
  $("abasTopo").innerHTML=t;
  traduzirDom($("abas")); traduzirDom($("abasTopo"));
  /* SO os botoes das duas barras. O seletor generico "[data-aba]" tambem
     casava com o proprio <body>, que carrega data-aba para o CSS -- e o
     resultado era um ouvinte de clique no body inteiro: TODO clique da
     pagina (arrastar na mesa, digitar num campo, apertar qualquer botao)
     chamava irAba() de novo, regravava o localStorage e, na aba Mesa,
     remedia o canvas. Passava despercebido porque trocar para a aba em
     que ja se esta nao muda nada visivel. */
  document.querySelectorAll("#abas [data-aba], #abasTopo [data-aba]")
    .forEach(function(b){
      b.addEventListener("click",function(){irAba(b.dataset.aba);});
    });
})();


/* =====================================================================
   GAVETA DE CONFIGURACAO
   Abre pela engrenagem do cabecalho. Tudo que se ajusta uma vez mora
   aqui; a tela de trabalho fica so com o que se usa no turno.
   ===================================================================== */
let cfgAtual = "maquina";

function irCfg(qual){
  const validas = {maquina:"cfgMaquina", calib:"cfgCalib",
                   encoder:"cfgEncoder", sistema:"cfgSistema"};
  if(!validas[qual]) qual = "maquina";
  cfgAtual = qual;
  for(const k in validas) $(validas[k]).classList.toggle("on", k === qual);
  document.querySelectorAll("#cfgAbas button").forEach(function(b){
    b.classList.toggle("on", b.dataset.cfg === qual);});
  /* A tela de saude e o registro so sao buscados quando aparecem: sao
     duas requisicoes que nao fazem falta enquanto ninguem olha. */
  if(qual === "sistema"){ saudeAtualizar(); pintarQR(); }
  if(qual === "calib"){ calibAtualizar(); }
  try{localStorage.setItem("cfg", qual);}catch(e){}
}

/* Mede o cabecalho e guarda a altura numa variavel de CSS. Sem isto a
   gaveta ou cobriria o botao PARAR ou deixaria um buraco: a altura muda
   com a largura da tela. */
function medirCabecalho(){
  const h = document.querySelector("header.placa");
  if(h) document.documentElement.style.setProperty("--altCab", h.offsetHeight + "px");
  const a = document.getElementById("abas");
  // No computador a barra de abas nao existe (display:none) e offsetHeight
  // e zero -- que e exatamente o valor certo para a conta.
  document.documentElement.style.setProperty("--altAbas",
    (a ? a.offsetHeight : 0) + "px");
}
addEventListener("resize", medirCabecalho);
medirCabecalho();

function abrirCfg(){
  medirCabecalho();
  irCfg(cfgAtual);
  $("veuCfg").classList.add("on");
  $("btCfg").classList.add("on");
}
function fecharCfg(){
  $("veuCfg").classList.remove("on");
  $("btCfg").classList.remove("on");
}

$("btCfg").onclick = function(){
  if($("veuCfg").classList.contains("on")) fecharCfg(); else abrirCfg();
};
$("cfgFechar").onclick = fecharCfg;
document.querySelectorAll("#cfgAbas button").forEach(function(b){
  b.onclick = function(){ irCfg(b.dataset.cfg); };
});
/* Clicar no veu, fora da caixa, fecha. Tocar fora para sair e o gesto
   que todo mundo ja tenta. */
$("veuCfg").addEventListener("click", function(e){
  if(e.target === $("veuCfg")) fecharCfg();
});
/* Esc fecha, e a gaveta nao pode engolir a parada de emergencia: o
   botao PARAR fica no cabecalho, fora dela, e continua alcancavel. */
addEventListener("keydown", function(e){
  if(e.key === "Escape" && $("veuCfg").classList.contains("on")) fecharCfg();
});
try{ const g = localStorage.getItem("cfg"); if(g) cfgAtual = g; }catch(e){}
irCfg(cfgAtual);

let abaAtual="";
function irAba(nome){
  /* Escolher uma aba de trabalho e dizer "voltei ao trabalho": a gaveta
     de configuracao sai da frente sozinha. */
  if(typeof fecharCfg === "function") fecharCfg();
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
/* Duas bibliotecas independentes, uma por tipo: programas e trajetorias.
   Antes era UMA lista com um seletor de tres posicoes (programas,
   trajetorias, ajustes) -- e o operador nunca sabia qual estava vendo.
   Os ajustes sairam da biblioteca: agora eles se copiam sozinhos para o
   cartao, num arquivo reservado, e voltam por um botao na gaveta.
   O mesmo codigo serve as duas: o que muda e o tipo e os ids. */
let sdSeq=-1,sdEstado="";
const BIB={
  prog:{arqs:[],nome:"sdNomeProg",oque:"sdOqueProg",lista:"sdListaProg",
        bt:"SdSalvarProg",ver:true},
  traj:{arqs:[],nome:"sdNomeTraj",oque:"sdOqueTraj",lista:"sdListaTraj",
        bt:"SdSalvarTraj",ver:false}
};
$("btSdMontar").onclick=function(){post("/api/sd/montar").then(function(){sdSeq=-1;});};

Object.keys(BIB).forEach(function(tipo){
  const b=BIB[tipo];
  $("bt"+b.bt).onclick=function(){
    const n=$(b.nome).value.trim();
    if(!n){acao(b.bt,"informe um nome para o arquivo");return;}
    post("/api/sd/salvar?tipo="+tipo+"&nome="+encodeURIComponent(n))
     .then(function(){sdSeq=-1;});
  };
  $(b.nome).oninput=function(){sdEstadoSalvar();};
});

/* O que "Salvar" vai gravar, e por que ele nao pode agora.
   Antes o botao respondia 200 sempre: o firmware enfileirava o pedido e
   a recusa ("nada para salvar", "cartao ausente") aparecia so na tira de
   mensagem, que rola. O operador apertava e concluia que nao funcionava. */
function sdEstadoSalvar(){
  Object.keys(BIB).forEach(function(tipo){
    const b=BIB[tipo];
    const nome=$(b.nome).value.trim();
    const quanto=(tipo==="prog")?(D.progN||0):(D.trajN||0);
    $(b.oque).textContent = (tipo==="prog")
      ? (quanto>=2 ? "vai gravar o programa que esta na maquina: "+quanto+" pontos"
                   : "nao ha programa na maquina. Desenhe na mesa, importe um DXF ou grave pontos na aba Mover")
      : (quanto>=2 ? "vai gravar a trajetoria na memoria: "+quanto+" amostras"
                   : "nao ha trajetoria gravada. Use \"Trajetoria a mao livre\" na aba Programa");
    acao(b.bt,
        sdEstado==="DESLIGADO" ? "o cartao nao foi iniciado"
      : sdEstado==="SEM_CARTAO" ? "nenhum cartao no slot"
      : sdEstado==="OCUPADO" ? "o cartao esta ocupado, aguarde"
      : (D.modo&&D.modo!=="MANUAL") ? "salve com o robo parado no modo manual"
      : (quanto<2) ? (tipo==="prog" ? "nao ha programa na maquina para salvar"
                                    : "nao ha trajetoria gravada para salvar")
      : !nome ? "de um nome ao arquivo"
      : /[^A-Za-z0-9 _-]/.test(nome) ? "use so letras, numeros, espaco, hifen e sublinhado"
      : "");
  });
  $("sbSdTraj").textContent=(BIB.traj.arqs.length||0)+" no cartao";
}

function sdPintar(tipo){
  const b=BIB[tipo], cx=$(b.lista);
  if(!b.arqs.length){
    cx.innerHTML='<div class="nulo">Nenhum arquivo salvo ainda.</div>';return;}
  let h='<div class="lista arqs">';
  b.arqs.forEach(function(a){
    h+='<div class="arq"><div class="nm">'+a.n+'</div>'+
       '<div class="kb">'+(a.b<1024?a.b+" B":(a.b/1024).toFixed(1)+" kB")+'</div>'+
       (b.ver?'<button class="mb" data-ver="'+a.n+'">ver</button>':'')+
       '<button class="mb" data-car="'+a.n+'">carregar</button>'+
       '<button class="mb x" data-apg="'+a.n+'">apagar</button></div>';
  });
  cx.innerHTML=h+'</div>';
  traduzirDom(cx);
  cx.querySelectorAll("[data-ver]").forEach(function(e){e.onclick=function(){
    verPeca(e.dataset.ver);};});
  cx.querySelectorAll("[data-car]").forEach(function(e){e.onclick=function(){
    post("/api/sd/carregar?tipo="+tipo+"&nome="+encodeURIComponent(e.dataset.car))
     .then(function(){sdSeq=-1;});};});
  cx.querySelectorAll("[data-apg]").forEach(function(e){e.onclick=function(){
    if(!confirm('Apagar "'+e.dataset.apg+'" do cartao?'))return;
    post("/api/sd/apagar?tipo="+tipo+"&nome="+encodeURIComponent(e.dataset.apg))
     .then(function(){sdSeq=-1;});};});
}

/* =====================================================================
   MINIATURA DA PECA
   Ver a peca errada e barato; carregar a peca errada custa uma chapa. O
   "ver" le o arquivo para uma area de troca no firmware e desenha -- o
   programa que esta na maquina nao e tocado.
   ===================================================================== */
function verPeca(nome){
  $("pvNome").textContent=nome;
  $("pvAviso").textContent="";
  $("pvAviso").style.display="none";
  $("pvInfo").textContent="lendo do cartao...";
  $("veuPeca").classList.add("on");
  const ct=$("pvTela").getContext("2d");
  ct.clearRect(0,0,$("pvTela").width,$("pvTela").height);

  post("/api/sd/prever?nome="+encodeURIComponent(nome)).then(function(){
    /* A leitura e assincrona no firmware (tarefa propria do cartao).
       Tenta algumas vezes ate a area de troca trazer esta peca. */
    let tentativas=0;
    (function puxar(){
      fetch("/api/sd/previa").then(function(r){return r.json();}).then(function(j){
        if(!j.n&&tentativas++<12){setTimeout(puxar,180);return;}
        pintarPeca(j);
      }).catch(function(){$("pvInfo").textContent="nao consegui ler o arquivo";});
    })();
  }).catch(function(){$("pvInfo").textContent="cartao ocupado ou ausente";});
}

function pintarPeca(j){
  const cv=$("pvTela"), ct=cv.getContext("2d");
  const L=cv.width, A=cv.height;
  ct.clearRect(0,0,L,A);
  if(!j.n){$("pvInfo").textContent="arquivo sem pontos";return;}

  /* Enquadra a peca inteira com folga. */
  let x0=1e9,x1=-1e9,y0=1e9,y1=-1e9;
  j.pts.forEach(function(p){x0=Math.min(x0,p.x);x1=Math.max(x1,p.x);
                            y0=Math.min(y0,p.y);y1=Math.max(y1,p.y);});
  const lx=Math.max(1,x1-x0), ly=Math.max(1,y1-y0);
  const k=Math.min((L-30)/lx,(A-30)/ly);
  const cx=function(x){return 15+(x-x0)*k;};
  /* Y do desenho cresce para cima, o do canvas para baixo. */
  const cy=function(y){return A-15-(y-y0)*k;};

  const est=getComputedStyle(document.body);
  const corSolda=est.getPropertyValue("--quente").trim()||"#c0392b";
  const corDesl =est.getPropertyValue("--fraca").trim()||"#888";

  for(let i=0;i+1<j.n;i++){
    ct.beginPath();
    ct.moveTo(cx(j.pts[i].x),cy(j.pts[i].y));
    ct.lineTo(cx(j.pts[i+1].x),cy(j.pts[i+1].y));
    ct.strokeStyle=j.pts[i].s?corSolda:corDesl;
    ct.lineWidth=j.pts[i].s?3:1;
    if(!j.pts[i].s)ct.setLineDash([3,3]); else ct.setLineDash([]);
    ct.stroke();
  }
  ct.setLineDash([]);
  j.pts.forEach(function(p,i){
    ct.beginPath();
    ct.arc(cx(p.x),cy(p.y),i===0?4:3,0,7);
    ct.fillStyle=i===0?corSolda:corDesl;
    ct.fill();
  });

  const cordoes=j.pts.filter(function(p,i){return i<j.n-1&&p.s;}).length;
  $("pvInfo").textContent=j.n+" pontos · "+cordoes+" cordao(oes) · "+
    Math.round(lx)+" x "+Math.round(ly)+" mm";

  /* Peca errada: feita com outros elos, ela nao vai cair onde deveria. */
  if(j.l1>0&&j.l2>0&&(Math.abs(j.l1-j.l1Maq)>0.5||Math.abs(j.l2-j.l2Maq)>0.5)){
    const av=$("pvAviso");
    av.style.display="block";
    av.innerHTML="Esta peca foi feita com elos <b>"+j.l1.toFixed(0)+"+"+
      j.l2.toFixed(0)+" mm</b> e esta maquina tem <b>"+j.l1Maq.toFixed(0)+"+"+
      j.l2Maq.toFixed(0)+" mm</b>. Os mesmos angulos apontam para outro lugar da chapa "+
      "&mdash; ensaie antes de soldar.";
  }
}

$("pvFechar").onclick=function(){$("veuPeca").classList.remove("on");};
$("pvCarregar").onclick=function(){
  const nome=$("pvNome").textContent;
  post("/api/sd/carregar?tipo=prog&nome="+encodeURIComponent(nome)).then(function(){
    sdSeq=-1; $("veuPeca").classList.remove("on");});
};

function sdAtualizar(forcar){
  return fetch("/api/sd").then(function(r){return r.json();}).then(function(d){
    sdEstado=d.estado;
    sdEstadoSalvar();
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
    $("sbCfgCartao").textContent = d.estado==="PRONTO"
      ? "espelhada no cartao" : "sem cartao: so a memoria interna";
    acao("CfgRestaurar", d.estado!=="PRONTO" ? "nenhum cartao pronto"
       : (D.modo&&D.modo!=="MANUAL") ? "restaure com o robo parado no modo manual" : "");
    if(forcar||d.seq!==sdSeq){sdSeq=d.seq;sdLer();}
  }).catch(function(){});
}
function sdLer(){
  /* As duas pastas de uma vez: o operador ve as duas listas na tela e
     nao ha seletor para dizer qual esta valendo. */
  return Promise.all(Object.keys(BIB).map(function(tipo){
    return fetch("/api/sd/lista?tipo="+tipo).then(function(r){return r.json();})
     .then(function(j){
       BIB[tipo].arqs=j.arq||[];
       sdPintar(tipo);
       /* A tarefa do cartao pode ainda estar montando outra pasta: nesse
          caso o firmware avisa "pronto:false" e a gente volta no proximo
          ciclo, sem ficar martelando o SPI. Sem cartao nao adianta
          insistir -- o firmware recusa e ficariamos batendo a cada 400 ms. */
       if(!j.pronto&&sdEstado==="PRONTO")setTimeout(function(){sdSeq=-1;},400);
     }).catch(function(){});
  })).then(sdEstadoSalvar);
}

/* =====================================================================
   TRAJETORIA A MAO LIVRE
   ===================================================================== */
/* Nao troca de aba sozinho: quem aperta aqui esta olhando para o cartao,
   e a tela pular embaixo do dedo assusta mais do que ajuda. Quem diz o
   que fazer em seguida e a tarja de estado logo acima do botao. */
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
/* Levar UMA junta e mandar a outra para onde ela ja esta: o firmware
   recebe um destino completo e nao precisa de rota nova, e a junta que
   nao se quer mexer nao anda um pulso. Serve para o caso de um motor so
   no barramento -- da para levar o eixo que tem torque sem que o outro
   entre na conta. */
function moverJuntas(t1,t2,onde){
  post("/api/mover?t1="+t1+"&t2="+t2,onde);
}
$("btMover1").onclick=function(){
  moverJuntas($("inMt1").value||0, (D.t2!==undefined?D.t2:0), "qMover1");
};
$("btMover2").onclick=function(){
  moverJuntas((D.t1!==undefined?D.t1:0), $("inMt2").value||0, "qMover2");
};
$("btMover").onclick=function(){
  moverJuntas($("inMt1").value||0, $("inMt2").value||0, "qMover");
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
/* Desenho no ritmo do monitor. O setInterval de 45 ms nao se alinhava
   com os quadros da tela, entao um a cada tres saia repetido ou pulado
   -- tremia mesmo com dado novo. O rAF tambem para sozinho quando a aba
   sai da frente, em vez de gastar bateria desenhando o que ninguem ve. */
function quadro(){ pintar(); requestAnimationFrame(quadro); }
requestAnimationFrame(quadro);
tick();
lerPontos();
sdAtualizar(true);
/* A vista escolhida sobrevive ao recarregar. Fica AQUI, no fim: mais
   acima, 'desOn' e o proprio botao de desenho ainda nao existem, e
   restaurar cedo demais quebraria a pagina inteira. */
try{ if(localStorage.getItem("vista3d")==="1")$("z3D").onclick(); }catch(e){}

/* PUXAR PARA ATUALIZAR: a trava de reserva.
   O CSS (overscroll-behavior) resolve no Chrome do Android e no Safari
   16 para cima. Em WebView antigo e no iPhone velho ele e ignorado, e
   ali o gesto so para na mao.
   A regra e estreita de proposito, para nao atrapalhar rolagem de
   verdade: so barra quando o dedo DESCE e o container debaixo dele ja
   esta no topo. Qualquer rolagem que ainda tenha para onde ir passa
   direto. */
(function(){
  let y0=0,x0=0;
  document.addEventListener("touchstart",function(e){
    if(e.touches.length===1){y0=e.touches[0].clientY;x0=e.touches[0].clientX;}
  },{passive:true});
  document.addEventListener("touchmove",function(e){
    if(e.touches.length!==1)return;
    const dy=e.touches[0].clientY-y0;
    const dx=e.touches[0].clientX-x0;
    if(dy<=0||Math.abs(dx)>Math.abs(dy))return;   /* subindo ou de lado */
    let el=e.target;
    while(el&&el!==document.body){
      if(el.scrollHeight>el.clientHeight+1){
        const ov=getComputedStyle(el).overflowY;
        if(ov==="auto"||ov==="scroll"){
          if(el.scrollTop>0)return;               /* ainda tem para onde rolar */
          break;
        }
      }
      el=el.parentElement;
    }
    if(e.cancelable)e.preventDefault();
  },{passive:false});
})();

/* As explicacoes da GAVETA tem interruptor proprio, e ele nasce
   desligado. Na tela de trabalho as notas sao poucas e curtas e ficam
   sempre visiveis -- por isso o "?" do cabecalho saiu: um interruptor
   para o que nunca se esconde e so mais um botao. */
(function(){
  const b=$("cfgAjuda");
  if(!b)return;
  const por=function(esconder){
    document.body.classList.toggle("semNotasCfg",esconder);
    b.classList.toggle("on",!esconder);
    b.title=esconder?"Mostrar as explicacoes":"Esconder as explicacoes";
    try{localStorage.setItem("notasCfg",esconder?"0":"1");}catch(e){}
  };
  b.onclick=function(){por(!document.body.classList.contains("semNotasCfg"));};
  let guardado="0";
  try{guardado=localStorage.getItem("notasCfg")||"0";}catch(e){}
  por(guardado==="0");
})();

</script>
</body>
</html>
)rawliteral";
