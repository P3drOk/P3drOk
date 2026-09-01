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
   Em tela larga ela pode ser uma TERCEIRA coluna, ao lado do resto: a
   leitura do encoder existe para ser acompanhada enquanto se mexe no
   resto, e trocar de aba para olhar o erro e perder o momento em que ele
   acontece.

   MAS ELA NASCE FECHADA. Aquilo e diagnostico -- duas rodinhas, um
   grafico e quinze numeros que ninguem opera -- e estava ocupando um
   terco da tela mais nobre da maquina, na frente de quem nunca a viu. Em
   HMI industrial isso e nivel 3 no lugar do nivel 1. Quem precisa dela
   abre num toque, e a escolha fica gravada no navegador.

   Abaixo de 1300px nao ha largura honesta para tres colunas, e ela volta
   a ser uma aba como as outras. */
.dockEnc{display:none}
/* O interruptor da coluna so existe onde ela cabe -- e a regra base vem
   ANTES da media query: mesma especificidade, ganha a ultima, e ja
   houve um caso neste arquivo em que a ultima era a errada. */
#btEnc{display:none;align-items:center;gap:6px}
@media(min-width:1301px){
  #btEnc{display:inline-flex}
  body.comEnc .corpo{grid-template-columns:380px minmax(0,1fr) 400px}
  body.comEnc .dockEnc{display:block;overflow-y:auto;overscroll-behavior:contain;
   min-width:0;padding-right:2px}
  body.comEnc .dockEnc #pnEnc{display:block}
  /* Fica aberta em qualquer aba, inclusive na "mesa". */
  .dockEnc .et{margin-bottom:9px}
  /* Com a coluna na tela o botao de aba nao faz falta. */
  body.comEnc .abas button[data-aba="enc"],
  body.comEnc .abasTopo button[data-aba="enc"]{display:none}
}
#btEnc.on{background:var(--face);color:var(--letra);border-color:var(--letra2)}
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
  .regua b{font-size:20px}
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
/* ORCAMENTO DE COR: CINZA E O NORMAL, COR E A EXCECAO.
   A tela gastava cor no que esta em ordem -- eixo energizado verde,
   lampada acesa verde, aba escolhida azul saturado -- e quando algo dava
   errado nao sobrava contraste para chamar. Aqui o normal e cinza; verde,
   azul, laranja e vermelho ficam reservados para o que pede atencao.
   Um ponto vermelho entre cinco cinzas se le em duzentos milissegundos;
   entre cinco verdes, nao.

   OS STATUS SAO SECUNDARIOS.
   Eles estavam com caixa, borda e halo aceso, competindo com os
   controles pela atencao -- e controle do robo tem de ganhar de
   diagnostico. Aqui viraram texto apagado com um ponto pequeno: legivel
   quando se procura, invisivel quando nao se procura.
   O que continua chamando e o que PRECISA chamar: emergencia,
   que piscam em vermelho. */
.lamps{display:flex;margin-left:auto;min-width:0;opacity:.55}
.lp{display:flex;flex-direction:row;align-items:center;gap:5px;min-width:0;
 padding:5px 8px}
.olho{flex:0 0 auto;width:6px;height:6px;border-radius:50%;background:var(--linha2)}
.lp.on .olho{background:var(--letra3)}   /* normal = cinza */
.lp.at .olho{background:var(--arco)}
.lp.hot .olho{background:var(--quente)}
/* A falha e a excecao: ela TEM de furar a discricao. */
.lp.er{opacity:1}
.lp.er .olho{background:var(--brasa);box-shadow:0 0 10px var(--brasa);animation:pi .45s infinite}
@keyframes pi{50%{opacity:.25;box-shadow:none}}
.lp span{font-family:var(--mono);font-size:8px;letter-spacing:.08em;color:var(--letra3);
 text-transform:uppercase;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
/* Aceso, o rotulo acende junto: ponto colorido com legenda apagada ao
   lado obriga a olhar duas vezes para ler o mesmo estado. */
.lp.er span{color:var(--brasa);font-weight:700}
/* O campo do modo e o unico que muda de palavra: reservar largura evita
   a tira inteira pular de tamanho a cada troca de estado. */
#lModo{min-width:96px}
#lModo span{font-weight:700}
.estop{flex:0 0 auto;background:var(--brasa);border:none;color:#fff;font-family:var(--mono);font-size:12px;
 font-weight:700;letter-spacing:.15em;padding:12px 20px;border-radius:4px;cursor:pointer;
 box-shadow:0 3px 0 rgba(0,0,0,.35);margin-left:8px}
.estop:active{box-shadow:0 1px 0 rgba(0,0,0,.35);transform:translateY(2px)}

/* O motor tem botao proprio por eixo, hoje na linha de comando do
   painel de jog, ao lado do PARAR: ligar e desligar torque e a coisa que
   mais se aperta na maquina, e estava enterrada numa gaveta de Ajustes.
   Passou pelo cabecalho e desceu para o painel a pedido de quem opera --
   perto das setas que a mao ja esta usando. A cor diz o estado --
   vermelho sem torque, cinza com, ambar esperando o barramento
   responder. Nao e o PARAR e nao pode ser confundido com ele. */
.ch.indo{opacity:.55;animation:pi .7s infinite}
/* Posicao atual, grande o bastante para se ler de longe: e o numero que
   o operador confere antes de mandar o proximo comando. */
.agora{font-family:var(--mono);font-size:15px;letter-spacing:.02em;
 padding:8px 10px;margin-bottom:9px;border-radius:4px;
 background:var(--painel);border:1px solid var(--linha);color:var(--letra);
 /* Uma linha, sempre: "(medido)" e "(comandado)" tem larguras diferentes
    e a troca quebrava a linha em duas, mexendo em tudo abaixo. */
 white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
/* Ir para um angulo numa linha so: de onde esta, para onde vai. */
/* Velocidade numa barra, ao lado do braco: e olhando ele andar que se
   acerta velocidade, nao numa gaveta de ajustes. */
.velLinha{display:flex;align-items:center;gap:10px;margin:10px 0 8px}
.velLinha label{font-size:12px;color:var(--letra2);flex:0 0 auto}
.velLinha input[type=range]{flex:1;min-width:0;accent-color:var(--arco)}
.velLinha input[type=number]{width:74px;flex:0 0 auto;background:var(--fundo);
 border:1px solid var(--linha);border-radius:2px;padding:6px 8px;text-align:right;
 font-family:var(--mono);font-size:12px;color:var(--letra)}
.velLinha input[type=number]:focus{outline:none;border-color:var(--arco)}
.velLinha .un{font-family:var(--mono);font-size:9px;color:var(--letra3);
 width:34px;flex:0 0 auto}
/* A linha de baixo diz a mesma velocidade na outra unidade, e traz os
   tres degraus que cobrem o dia inteiro. Digitar milimetro por segundo e
   o que se pensa na bancada; grau por segundo e o que a maquina faz. */
.velEq{display:flex;align-items:center;gap:8px;margin:-4px 0 8px;
 font-family:var(--mono);font-size:10px;color:var(--letra3)}
.velEq b{font-weight:600;color:var(--letra2);flex:1}
.atalhosVel{display:flex;gap:5px}
/* Botoes lado a lado quando sao do mesmo assunto: dois botoes largos
   empilhados ocupavam duas linhas para dizer duas palavras. */
.linhaBt{display:flex;gap:7px;align-items:stretch;margin-bottom:6px}
.linhaBt .b{margin:0;flex:1}
/* Botao quadrado, so simbolo: "gravar ponto na posicao atual" e uma
   frase para uma acao que se faz dezenas de vezes seguidas. */
.bq{flex:0 0 auto;width:44px;display:grid;place-items:center;border:none;
 border-radius:4px;cursor:pointer;background:var(--face);color:var(--letra2)}
.bq.ok{background:var(--pronto);color:#fff}
.bq:disabled{opacity:.45;cursor:default}
.bq .ic{width:19px;height:19px}
.irAng{display:flex;align-items:center;gap:7px;margin-bottom:6px}
.irAng #irDe{font-family:var(--mono);font-size:12px;color:var(--letra2);
 min-width:58px;text-align:right}
.irAng .seta{color:var(--letra3)}
.irAng input{flex:1;min-width:70px;max-width:none;width:auto;text-align:right}
/* A LINHA DE COMANDO DO PAINEL DE JOG.
   Os tres botoes que mexem na maquina -- torque de cada eixo e PARAR --
   ficam juntos no alto do painel, do tamanho da mao. PARAR ocupa a
   largura que sobra: e o unico deles que se aperta sem olhar. */
.comandos{display:flex;gap:6px;margin-bottom:8px}
.comandos .motor{flex:1 1 0;min-width:0;padding:12px 6px;text-align:center}
.comandos .estop{flex:1 1 0;min-width:0;margin-left:0;padding:12px 6px}
/* SEM TORQUE E VERMELHO; COM TORQUE E CINZA.
   Cinza dizia "nao sei" -- e a maquina sabe: ela releu o registrador do
   driver antes de responder. O que mudou e o outro lado: eixo energizado
   e o estado NORMAL, e normal nao gasta cor. Verde ali competia com o
   vermelho do eixo que falta ligar, que e o que precisa ser visto. O
   ambar continua sendo "o barramento ainda nao confirmou". */
.motor{flex:0 0 auto;border:1px solid var(--linha2);font-family:var(--mono);
 font-size:12px;font-weight:700;
 letter-spacing:.08em;padding:12px 14px;border-radius:4px;cursor:pointer;color:#fff;
 background:var(--brasa);box-shadow:0 3px 0 rgba(0,0,0,.25)}
.motor.on{background:var(--face);color:var(--letra);border-color:var(--linha)}
.motor.indo{background:var(--quente)}
.motor.ruim{background:var(--brasa)}
.motor:active{box-shadow:0 1px 0 rgba(0,0,0,.25);transform:translateY(2px)}
@media(max-width:760px){
  .comandos{gap:4px}
  .comandos .motor,.comandos .estop{padding:10px 4px;font-size:10px;
   letter-spacing:.02em}
}

/* CINCO DEGRAUS DE VELOCIDADE.
   Alvos do tamanho do dedo, com o escolhido preenchido. A barra continua
   que estava aqui pedia mira fina para acertar um numero que ninguem
   sabe de cor. */
.velNiveis{display:flex;gap:4px;flex:1 1 auto;min-width:0}
.velNiveis button{flex:1 1 0;min-width:0;background:var(--painel);
 border:1px solid var(--linha);color:var(--letra2);border-radius:3px;
 font-family:var(--mono);font-size:13px;font-weight:600;padding:8px 0;
 cursor:pointer}
.velNiveis button:hover{border-color:var(--arco2);color:var(--letra)}
.velNiveis button.on{background:var(--arco);border-color:var(--arco);
 color:#fff}
/* O mm/s deixou de ser o que se escolhe e passou a ser o que se confere:
   entra pequeno, do lado, sem disputar com os degraus. */
.velLinha #inVelMm{flex:0 0 62px;width:62px;font-size:11px;padding:5px 6px}

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
.barraDes{position:absolute;left:12px;right:12px;bottom:12px;display:none;
 align-items:center;gap:8px;flex-wrap:wrap;background:var(--painel);opacity:.97;
 border:1px solid var(--linha);border-radius:5px;padding:8px 10px}
.barraDes .cnt{flex:1;min-width:120px;font-family:var(--mono);font-size:9.5px;
 letter-spacing:.06em;color:var(--letra2);text-transform:uppercase}
.barraDes .b{margin:0;width:auto;flex:0 0 auto;white-space:nowrap}
body[data-pos="1"] #barraPos{display:flex}
body[data-pos="1"] .tela canvas{cursor:move;touch-action:none}
.barraDes .cnt.ruim{color:var(--brasa)}
.barraDes .cnt.bom{color:var(--pronto)}

.regua{display:grid;grid-template-columns:repeat(5,1fr);border-top:1px solid var(--linha);
 background:var(--painel)}
.regua div{padding:7px 4px 8px;text-align:center;border-right:1px solid var(--linha)}
.regua div:last-child{border:none}
/* Corpo 28: e o numero que o operador le de pe, a um metro da bancada, sem
   se abaixar. Tudo o mais nesta faixa (rotulo em cima, medido embaixo) e
   apoio e fica pequeno de proposito -- a hierarquia tem que ser obvia de
   relance, senao a faixa vira um bloco cinza uniforme que ninguem le. */
.regua b{display:block;font-family:var(--mono);font-size:28px;font-weight:500;
 line-height:1.05;margin-top:2px;font-variant-numeric:tabular-nums}
.regua b.mv{color:var(--arco)}
.regua b.hot{color:var(--quente)}

/* A ABA PROGRAMA TAMBEM E UM QUADRO FIXO -- por dentro, nao por corte.
   Aqui nao dava para fazer como no jog e enxugar ate caber: a lista de
   pontos E o programa, e ela cresce com a peca. Cortar seria esconder
   ponto, e esconder ponto e pior que rolar.
   Entao quem rola e o MIOLO do cartao aberto, e so ele. Os cabecalhos
   dos cartoes, a barra de abas e as bordas do painel ficam parados: o
   dedo procura "Ensaiar sem arco" sempre no mesmo lugar, com um
   programa de tres pontos ou de quarenta.
   O .rol segue com overflow auto de reserva: se a tela for tao baixa que
   nem o miolo minimo caiba, e melhor rolar do que cortar um botao. */
body[data-aba="prog"] .coluna .rol{display:flex;flex-direction:column}
body[data-aba="prog"] #pnProg{display:flex;flex-direction:column;
 flex:1 1 auto;min-height:0}
body[data-aba="prog"] #pnProg > .et{flex:0 0 auto}
body[data-aba="prog"] #pnProg > .et.aberta{flex:1 1 auto;min-height:0;
 display:flex;flex-direction:column}
body[data-aba="prog"] #pnProg > .et.aberta > .dentro{flex:1 1 auto;
 min-height:170px;overflow-y:auto;overscroll-behavior:contain}

/* A MAO LIVRE E QUADRO FIXO PELO MESMO MOTIVO, e com um a mais: aqui as
   duas maos do operador estao no BRACO, nao na tela. Ele olha de relance
   para achar "marcar ponto" -- se a lista crescer e empurrar o botao para
   baixo, o relance nao acha, e ele tira a mao do braco que acabou de
   posicionar. */
body[data-aba="mao"] .coluna .rol{display:flex;flex-direction:column}
body[data-aba="mao"] #pnMao{display:flex;flex-direction:column;
 flex:1 1 auto;min-height:0}
body[data-aba="mao"] #pnMao > .et{flex:0 0 auto}
body[data-aba="mao"] #pnMao > .et.aberta{flex:1 1 auto;min-height:0;
 display:flex;flex-direction:column}
body[data-aba="mao"] #pnMao > .et.aberta > .dentro{flex:1 1 auto;
 min-height:170px;overflow-y:auto;overscroll-behavior:contain}

/* "Segure para gravar" acesa enquanto a mao esta em cima. */
#btGravSeg.on{background:var(--arco);border-color:var(--arco);color:#fff}

/* O PAINEL DE JOG E UM QUADRO FIXO.
   Aqui o dedo procura o mesmo botao no mesmo lugar, toda vez. Rolar
   significa que a seta que estava sob o dedo saiu dali -- entao o
   conteudo tem de CABER, e as folgas deste painel sao menores que as do
   resto da pagina de proposito. Cada pixel poupado aqui e um pixel a
   menos de rolagem. */
#pnMover .agora{margin-bottom:6px}
#pnMover .eixo{margin-bottom:4px}
#pnMover .velLinha{margin-top:7px;margin-bottom:6px}
#pnMover .velEq{margin-bottom:6px}
#pnMover .linhaBt{margin-bottom:3px}
#pnMover h4{margin-top:10px;margin-bottom:6px}
#pnMover .cab{padding:8px 12px}
#pnMover .dentro{padding-bottom:6px}
#pnMover .cp{margin-bottom:5px}
#pnMover .irAng{margin-bottom:4px}

/* ---------- coluna ---------- */
.coluna{background:var(--mesa);border:1px solid var(--linha);border-radius:5px;
 display:flex;flex-direction:column;min-height:0;overflow:hidden}
.rol{overflow-y:auto;overflow-x:hidden;padding:10px;flex:1;scrollbar-width:thin;min-width:0;
 overscroll-behavior:contain}
/* Grudada no topo da coluna: a resposta de cada acao ("Ponto 3 gravado",
   "Movimento recusado: ...") tem que estar visivel sem rolar de volta. */
/* A TARJA DE ESTADO SAI DAS ABAS DE COMANDO.
   Em Mover e Programa o painel e so comando: os proprios botoes dizem o
   que da e o que nao da (cada um com o seu motivo embaixo), as lampadas
   do cabecalho dizem o estado, e os botoes de torque agora estao ali
   dentro -- que era o que a tarja mandava fazer. Nas outras abas ela
   fica, porque ali nao ha botao de torque a mao. */
body[data-aba="mover"] #tira,
body[data-aba="mao"]   #tira,
body[data-aba="prog"]  #tira{display:none}
.tira{position:sticky;top:0;z-index:6;
 padding:10px 12px;background:var(--painel);border:1px solid var(--linha);
 border-left:4px solid var(--linha2);border-radius:3px;margin-bottom:9px;
 min-height:40px}
.teTopo{display:flex;align-items:baseline;gap:9px;flex-wrap:wrap}
/* O ESTADO em uma palavra, do tamanho de quem le de pe, a um metro. */
.teEst{font-family:var(--mono);font-size:17px;letter-spacing:.09em;
 color:var(--letra);font-weight:700}
.teSub{font-family:var(--mono);font-size:10px;letter-spacing:.09em;
 color:var(--letra3);text-transform:uppercase}
/* Altura de DUAS linhas reservada. A mensagem oscila entre uma e duas
   linhas a cada estado, e cada oscilacao empurrava para baixo tudo o que
   vem depois -- inclusive as setas de jog, que o dedo ja estava
   apertando. Reservar o espaco custa dezoito pixels e devolve uma coluna
   que fica parada. */
.teMsg{font-size:12.5px;color:var(--letra2);line-height:1.45;margin-top:3px;
 min-height:36px}
/* O PROXIMO PASSO, quando ha um obvio. Botao, nao frase: quem esta
   comecando nao devia ter de procurar onde se faz o que a tela pediu. */
.teAcao{margin:8px 0 0;width:100%}
/* Cor so no anormal. Em ordem normal a barra e cinza como o resto. */
.tira.er{border-left-color:var(--brasa)}
.tira.er .teEst{color:var(--brasa)}
.tira.at{border-left-color:var(--arco)}
.tira.at .teEst{color:var(--arco)}
.tira.hot{border-left-color:var(--quente)}
.tira.hot .teEst{color:var(--quente)}

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
.eixo .fx{font-family:var(--mono);font-size:9px;color:var(--letra3);margin-top:2px;
 /* "sem curso" e uma linha de texto; com curso medido vem numero MAIS a
    barrinha. As duas alturas tem de ser a mesma, senao calibrar uma
    junta empurra as setas da outra. */
 min-height:21px}
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
/* O TRILHO do programa. Ele nasce embaixo do numero do ponto de cima e
   morre embaixo do de baixo, entao a sequencia se le sem contar linha:
   laranja = cordao, cinza = so deslocamento. */
.lista.prog .tr{position:relative}
.lista.prog .tr span{flex:1}
.lista.prog .tr::before{content:"";position:absolute;left:18px;top:-1px;bottom:-1px;
 width:4px;border-radius:2px;background:var(--linha2)}
.lista.prog .tr.q::before{background:var(--quente)}
.lista.prog .p .c{display:flex;align-items:baseline;gap:8px}
.lista.prog .p .c .ang{color:var(--letra3);font-size:10px}
.lista.prog .ch{width:34px;height:19px}
.lista.prog .ch i{width:13px;height:13px}
.lista.prog .ch.on i{left:18px}
/* Rodape com as duas contas do ciclo. */
.somaProg{display:flex;gap:14px;justify-content:flex-end;padding:7px 10px;
 background:var(--face);font-family:var(--mono);font-size:10px;
 color:var(--letra2);letter-spacing:.04em}
.somaProg b{color:var(--letra);font-weight:600}
.somaProg .q b{color:var(--quente)}
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
.ajudaAba{margin:0 0 8px;padding:9px 11px;border:1px solid var(--linha);
 border-left:3px solid var(--arco2);border-radius:4px;background:var(--face);
 font-size:12px;line-height:1.55;color:var(--letra2)}
.ajudaAba b{display:block;color:var(--letra);font-size:12.5px;margin-bottom:2px}
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
/* "Robo 2DOF | Configuracao": a configuracao logo depois do titulo, do
   tamanho de um link e nao de um botao -- alcancavel sem disputar
   espaco com o que controla o robo. O subtitulo do modelo saiu: ele
   nao dizia nada que o operador precisasse na hora de operar. */
.divisor{color:var(--linha2);font-size:15px;margin:0 -4px}
.cfgLink{background:none;border:none;color:var(--letra2);cursor:pointer;
 font-family:var(--mono);font-size:10px;letter-spacing:.14em;
 text-transform:uppercase;padding:6px 8px;border-radius:3px}
.cfgLink:hover{color:var(--arco)}
.cfgLink.on{color:var(--arco);background:var(--face)}
.ajd{background:var(--face);border:1px solid var(--linha);border-radius:3px;
 width:30px;height:30px;flex:0 0 auto;cursor:pointer;color:var(--letra3);
 font-family:var(--mono);font-size:13px;font-weight:600;margin-left:10px}
.ajd:hover{color:var(--arco);border-color:var(--arco2)}
.ajd.on{background:var(--arco);border-color:var(--arco);color:#fff}
@media(max-width:560px){.ajd{width:26px;height:26px;font-size:11px;margin-left:6px}}
/* Motivo de um botao estar fora de acao. Nada de botao morto e mudo. */
.pq2{display:none;font-size:11px;color:var(--quente);margin:-5px 0 10px;
 line-height:1.5;padding-left:2px}
/* Pre-requisito: "falta fazer isto antes" nao e a mesma coisa que "deu
   errado". Mesmo lugar, peso diferente. */
.pq2.pre{color:var(--letra2)}
.pq2.pre::before{content:"\2022\00a0\00a0";color:var(--letra3)}
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
/* A previa da peca abre DE DENTRO da gaveta de Arquivos, entao tem de
   ficar por cima dela. Com o mesmo z-index vencia quem viesse depois no
   documento -- e a gaveta vem depois. */
#veuPeca{z-index:80}
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
/* A CONFIGURACAO E UMA SECAO, NAO UM POPUP.
   Ela ocupava 760 px no meio de um veu escurecido -- lia como uma
   caixa de dialogo, e caixa de dialogo pede para ser fechada logo. Como
   secao ela ocupa a tela inteira abaixo do cabecalho e pode respirar:
   cabem linhas de configuracao de ponta a ponta, no lugar de uma coluna
   estreita rolando sem fim. */
.cfgVeu{align-items:flex-start;padding:0;background:var(--fundo);
 backdrop-filter:none;
 padding-top:var(--altCab, 82px);
 padding-bottom:var(--altAbas, 0px)}
.cfgVeu .cfgCx{max-width:none;width:100%;border:none;border-radius:0;
 height:calc(100vh - var(--altCab, 82px) - var(--altAbas, 0px));
 display:flex;flex-direction:column;padding:0;overflow:hidden}
/* As linhas de configuracao acompanham a largura nova em vez de
   esticarem um campo so por toda a tela.

   E, acima de 1000 px, os CARTOES entram em COLUNAS. Em tela cheia com
   uma coluna so sobrava metade do monitor em branco e o resto ia parar
   embaixo da dobra: quem procurava um ajuste tinha de rolar para
   descobrir se ele existia. Cada cartao e fechado em si -- titulo,
   linhas, botoes --, entao ele flui para a coluna seguinte sem se
   partir. O `inline-block` e o que segura isso nos motores webkit, onde
   `break-inside` sozinho ainda deixa o cartao rachar no meio. */
@media(min-width:900px){
  .cfgRol{padding:14px 22px 20px}
  .cfgRol .dentro{max-width:none}
}
@media(min-width:1000px){
  .cfgRol .pane{column-width:410px;column-gap:20px}
  .cfgRol .pane>.et{break-inside:avoid;-webkit-column-break-inside:avoid;
   display:inline-block;width:100%;margin:0 0 14px}
}
@media(min-width:1500px){
  .cfgRol .pane{column-width:440px}
}

/* AS CONFIGURACOES EM LINHAS, COMO A SAUDE DA MAQUINA.
   La cada linha responde uma pergunta -- nome a esquerda, valor a
   direita, alternadas para o olho nao se perder. Aqui e a mesma
   pergunta com uma diferenca: o valor da direita se EDITA. Mesma
   gramatica visual, entao quem sabe ler uma sabe ler a outra. */
.cfgRol .cp{display:flex;align-items:center;gap:10px;margin:0;
 padding:7px 12px;border-bottom:1px solid var(--linha);
 background:var(--face)}
.cfgRol .cp:nth-child(even){background:var(--painel)}
.cfgRol .cp label{flex:1;font-size:12.5px;color:var(--letra)}
.cfgRol .cp input{width:110px;max-width:34%}
.cfgRol .cp .un{width:38px}
/* A chave tambem vira linha: um interruptor e uma configuracao como
   qualquer outra, e sair da grade fazia parecer outra coisa. */
.cfgRol .tr{display:flex;align-items:center;gap:10px;margin:0;
 padding:7px 12px;border-bottom:1px solid var(--linha);
 background:var(--face);flex-direction:row-reverse;justify-content:flex-end}
.cfgRol .tr span{flex:1;font-size:12.5px;color:var(--letra)}
.cfgRol .tr:nth-child(even){background:var(--painel)}
/* Titulo de grupo separa as linhas em assuntos, como as faixas da
   Saude. */
.cfgRol h4{margin:13px 0 0;padding:6px 12px;font-size:10px;
 letter-spacing:.14em;text-transform:uppercase;color:var(--letra3);
 font-family:var(--mono);border-bottom:1px solid var(--linha2);
 background:none}
.cfgRol h4:first-child{margin-top:0}
/* Botoes e notas continuam soltos: nao sao valores, sao acoes. */
.cfgRol .b{margin-top:10px}
.cfgRol .nt{padding:8px 12px 0}
.cfgTopo{display:flex;align-items:center;gap:10px;padding:14px 16px 10px;
 border-bottom:1px solid var(--linha)}
.cfgTopo h2{flex:1;margin:0}
.cfgTopo .ajd{flex:0 0 auto}
.cfgTopo .b{margin:0;width:auto;flex:0 0 auto}
.cfgBusca{display:flex;gap:7px;padding:9px 16px 0}
.cfgBusca input{flex:1;min-width:0;background:var(--fundo);color:var(--letra);
 border:1px solid var(--linha);border-radius:3px;padding:8px 10px;font-size:12.5px}
.cfgBusca input:focus{outline:none;border-color:var(--arco)}
/* Procurando, as abas param de valer: o que a busca mostra vem de todas
   elas ao mesmo tempo, e deixar uma marcada seria mentir sobre isso. */
body.cfgProcurando .cfgAbas{opacity:.35;pointer-events:none}
body.cfgProcurando .cfgRol .pane{display:block}
body.cfgProcurando .et.foraDaBusca{display:none}

/* O roteiro: uma linha por passo, numero, nome, estado e o atalho. */
.roteiro{border:1px solid var(--linha);border-radius:3px;overflow:hidden;
 margin-bottom:9px}
.rtItem{display:flex;align-items:center;gap:9px;padding:9px 10px;
 background:var(--painel);border-bottom:1px solid var(--linha)}
.rtItem:last-child{border-bottom:none}
.rtItem .n{font-family:var(--mono);font-size:11px;color:var(--letra3);
 width:14px;flex:0 0 auto;text-align:center}
.rtItem .tx{flex:1;min-width:0}
.rtItem .tt2{font-size:12.5px;color:var(--letra)}
.rtItem .st{display:block;font-size:10.5px;color:var(--letra3);margin-top:1px}
.rtItem.ok .n{color:var(--pronto)}
.rtItem.ok .st{color:var(--pronto)}
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
/* O joystick sai no COMPUTADOR: as setas de passo fazem o mesmo com
   mais precisao, e ele so ocupava o espaco dos controles que importam.
   No celular ele fica -- ali nao ha setas confortaveis, e arrastar o
   polegar continua sendo o jeito natural de levar o braco.
   Esta regra vem DEPOIS da que abre o joystick: com a mesma
   especificidade, quem ganha e a ultima -- e antes ela nao ganhava. */

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
/* O joystick sai no COMPUTADOR: as setas de passo fazem o mesmo com
   mais precisao, e ele so ocupava o espaco dos controles que importam.
   No celular ele fica -- ali nao ha setas confortaveis, e arrastar o
   polegar continua sendo o jeito natural de levar o braco.
   Esta regra vem DEPOIS das que abrem o joystick: com a mesma
   especificidade quem ganha e a ultima, e antes ela nao ganhava. */
@media(min-width:761px){ .joyCx,.joyLe{display:none} }

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
/* A etiqueta de tipo vem DEPOIS do nome, em cinza: o tipo importa para
   conferir, nao para escolher -- na hora de escolher, o que se procura
   e o nome. */
.arq .tag{font-family:var(--mono);font-size:8.5px;letter-spacing:.1em;
 text-transform:uppercase;color:var(--letra3);border:1px solid var(--linha2);
 border-radius:2px;padding:2px 5px;flex:0 0 auto}
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
  /* Aba escolhida marcada por CONTRASTE, nao por cor: um bloco azul
     saturado no topo competia com a barra de estado, que e quem precisa
     ser vista. */
  .abasTopo button.on{background:var(--face);color:var(--letra);
   font-weight:700;box-shadow:inset 0 -3px 0 var(--letra2)}
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
    <span class="divisor">|</span>
    <button class="cfgLink" id="btCfg">Configuracao</button>
    <button class="cfgLink" id="btArq" title="Abrir a aba de arquivos do cartao">Arquivos</button>
    <button class="cfgLink" id="btEnc" title="Mostrar ou esconder a coluna de diagnostico do encoder">Diagnostico</button>
    <button class="ajd" id="btAjuda" title="O que faco nesta aba?">?</button>
    <div class="lamps">
      <div class="lp" id="lModo"><i class="olho"></i><span id="lModoT">--</span></div>
      <div class="lp" id="lServo"><i class="olho"></i><span>servo</span></div>
      <div class="lp" id="lArco"><i class="olho"></i><span>arco</span></div>
      <div class="lp" id="lRede"><i class="olho"></i><span>rede</span></div>
      <div class="lp" id="lSd"><i class="olho"></i><span>cartao</span></div>
    </div>
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
            
            <!-- O ERRO SAIU DA TELA.
                 Ele era a diferenca entre o angulo COMANDADO (a contagem
                 de pulsos do firmware) e o MEDIDO pelo encoder. Numa
                 maquina em montagem essa contagem anda sozinha: o painel
                 chegou a mostrar "comandado 1986,79 graus, medido
                 -230,05, erro +2216,85". Nenhum desses tres numeros
                 ajudava a operar, e o do meio -- o unico que descreve o
                 braco de verdade -- ficava perdido entre dois que nao
                 descrevem nada.
                 O que sobra e o que importa: onde a junta ESTA. -->
            <div class="encGrade">
              <div class="encCel"><span class="rot">junta 1</span><b id="eM1">--</b></div>
              <div class="encCel"><span class="rot">junta 2</span><b id="eM2">--</b></div>
            </div>
            
            <h4>Analise detalhada</h4>
            <div class="grafico"><canvas id="cvPos"></canvas>
              <div class="legenda">
                <div class="lg g1"><i></i>junta 1 medida</div>
                <div class="lg g2"><i></i>junta 2 medida</div>
              </div>
            </div>

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

            <h4>Ultimas amostras <span class="pq2" id="sbAnal"></span></h4>
            <div class="tabAmostras"><table id="tabEnc"><tbody></tbody></table></div>
            <button class="b mini" id="btEncCsv">Baixar tudo em CSV</button>
            <div class="pq2" id="qEncCsv"></div>

            <h4>Diagnostico da linha</h4>
            <button class="b mini" id="btEncTestar">Testar a linha agora</button>
            <div class="pq2" id="qEncTestar"></div>
            <div class="res" id="encRel">--</div>
            
            <button class="b mini" id="btEncCacar">Procurar o registrador</button>
            <button class="b mini" id="btEncComparar">Comparar agora</button>
            <div class="pq2" id="qEncCacar"></div>
            
            <button class="b mini" id="btEncZerar">Zerar a contagem aqui</button>
            <div class="pq2" id="qEncZerar"></div>
            <div class="res" id="encEstado">--</div>
            
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
          <button class="zb pq" id="zTema" title="Alternar tema">TEMA</button>
          <button class="zb pq" id="z3D" title="Alternar vista 2D / 3D">3D</button>
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
      <!-- A BARRA DE ESTADO.
           Ela responde as tres perguntas que quem chega faz, nesta ordem:
           em que pe a maquina esta, por que ela nao anda, e o que fazer
           agora. Antes so a segunda existia, em cinza claro, corpo 12 --
           a informacao mais valiosa da tela era a menos visivel dela. -->
      <div class="tira" id="tira">
        <div class="teTopo">
          <b class="teEst" id="teEst">INICIANDO</b>
          <span class="teSub" id="teSub"></span>
        </div>
        <div class="teMsg" id="teMsg">Falando com a maquina…</div>
        <button class="b teAcao" id="teAcao" style="display:none"></button>
      </div>

      <!-- Ajuda no lugar da duvida. Nao e um manual em outra tela: e uma
           frase sobre a aba em que a pessoa ESTA, com o primeiro passo,
           que abre e fecha no "?" do cabecalho e lembra a escolha. Quem
           ja sabe operar desliga uma vez e nunca mais ve. -->
      <div class="ajudaAba" id="ajudaAba" hidden></div>

      <!-- ============================ MOVER ============================ -->
      <section class="pane" id="pnMover">
        <div class="et aberta">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-cruz"/></svg></div>
            <div class="tx"><div class="tt">Comando manual</div>
            <span class="sb" id="sbMover">passo, angulo e referencia</span></div></div>
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

            <!-- OS COMANDOS DE MAQUINA MORAM AQUI, no painel de jog.
                 Ligar torque e parar sao o que mais se aperta, e estavam
                 no cabecalho, longe das setas que a mao ja esta usando.
                 Agora estao no alto do proprio painel de comando.
                 O seletor "Junta" e a linha "Eixo 1: x graus (medido)"
                 sairam: o eixo se escolhe tocando no elo do desenho ou
                 na propria seta, e o angulo ja esta na regua do rodape,
                 em corpo 28, comandado e medido lado a lado. -->
            <div class="comandos">
              <button class="motor" id="btMotor1"><span id="btMotor1T">EIXO 1</span></button>
              <button class="motor" id="btMotor2"><span id="btMotor2T">EIXO 2</span></button>
              <button class="estop" id="btParar">PARAR</button>
            </div>

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
            <!-- VELOCIDADE EM CINCO DEGRAUS.
                 Era uma barra continua: mira fina para escolher um numero
                 que ninguem sabe de cor. Cinco degraus repartem a faixa
                 configurada da maquina, e o de baixo e o de cima sao
                 exatamente o minimo e o maximo dela.
                 O mm/s continua na tela -- e a unidade em que se pensa o
                 cordao -- mas pequeno, e ao lado: deixou de ser o que se
                 escolhe para ser o que se confere. -->
            <div class="velLinha">
              <label>Velocidade</label>
              <div class="velNiveis" id="velNiveis">
                <button data-niv="1">1</button>
                <button data-niv="2">2</button>
                <button data-niv="3">3</button>
                <button data-niv="4">4</button>
                <button data-niv="5">5</button>
              </div>
              <input type="number" id="inVelMm" min="1" step="10">
              <span class="un">mm/s</span>
            </div>
            <div class="velEq"><b id="velMovTx">--</b>
              <span class="atalhosVel">
                <button class="mb" data-vel="1">lento</button>
                <button class="mb" data-vel="3">normal</button>
                <button class="mb" data-vel="5">rapido</button>
                <button class="mb" id="btPrec">precisao</button>
              </span></div>
            <div class="linhaBt">
              <button class="b mini" id="btTesteMov">Testar rele</button>
            </div>
            <h4>Ir para um angulo</h4>
            <div class="irAng">
              <span id="irDe">--</span>
              <span class="seta">&rarr;</span>
              <input type="number" id="inMtSel" step="0.5" placeholder="alvo">
              <span class="un">&deg;</span>
              <button class="b mini" id="btMoverSel">Levar</button>
            </div>
            <div class="pq2" id="qMoverSel"></div>

            <h4>Atalhos</h4>
            <div class="linhaBt">
              <button class="bq ok" id="btGravar" title="Gravar ponto na posicao atual">
                <svg class="ic" aria-hidden="true"><use href="#i-alvo"/></svg></button>
              <button class="b mini" id="btHome">Ir para o zero da maquina</button>
            </div>
            <div class="pq2" id="qGravar"></div>
            <div class="pq2" id="qHome"></div>
            <!-- "Mudar a origem" morava aqui e foi para a gaveta, no
                 cartao "Zero absoluto", que ja e sobre origem e ja tem o
                 cadeado. Este painel e o de MEXER no braco: o que se
                 ajusta uma vez nao disputa espaco com ele, e o que sai
                 daqui e o que fazia a coluna crescer e rolar. -->

          </div>
        </div>
      </section>

      <!-- ========================== PROGRAMA ========================== -->
      <!-- ====================== TRAJETORIA MAO LIVRE ======================
           A funcao de destaque da maquina, e a unica em que o operador
           ENSINA em vez de digitar: solta o braco, leva a ponta com a mao
           e a maquina guarda o caminho.

           Dois modos, e a diferenca entre eles nao e de conforto:

             POR PONTOS  - o operador marca os vertices, e a maquina anda em
                           RETA de um para o outro. E o caminho que se edita
                           depois, ponto a ponto, na aba Programa.
             CONTINUO    - a maquina amostra o percurso enquanto o botao
                           estiver pressionado, e reproduz a curva como ela
                           foi feita. E o caminho que nao cabe em vertices.

           Quem EXECUTA continua sendo o Programa: aqui se grava. -->
      <section class="pane" id="pnMao">

        <div class="et aberta" id="eMaoPt">
          <div class="cab"><div class="mk">1</div>
            <div class="tx"><div class="tt">Por pontos, em reta</div>
            <span class="sb" id="sbMaoPt">nenhum ponto</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="perigo">Solto, o braco desce pelo proprio peso. Apoie a
            ponta <b>antes</b> de soltar, e nada embaixo da ponteira.</div>
            <div class="guia" id="aprGuia"></div>
            <button class="b pri" id="btApr">1 &middot; Soltar o braco</button>
            <div class="pq2" id="qApr"></div>
            <button class="b ok" id="btAprMarcar">2 &middot; Marcar ponto aqui</button>
            <div class="pq2" id="qAprMarcar"></div>
            <div class="nt">Com esta aba aberta, a tecla <b>G</b> marca o ponto
            sem tirar a mao do braco.</div>
            <button class="b mini" id="btAprFim">3 &middot; Encerrar</button>
            <div class="pq2" id="qAprFim"></div>
            <div class="aprEst" id="aprEst">desligado</div>
            <div class="nt">Entre dois pontos a ponta anda em <b>linha reta</b>.
            Um trecho cujo meio saia da area alcancavel e recusado na partida,
            em vez de o braco dar a volta por fora.</div>
          </div>
        </div>

        <div class="et" id="eTraj">
          <div class="cab"><div class="mk">2</div>
            <div class="tx"><div class="tt">Continuo, segurando</div>
            <span class="sb" id="sbTraj">nenhuma gravada</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt">Para curva, contorno e forma livre -- o que nao cabe
            em vertices. A maquina amostra o percurso inteiro e reproduz a curva
            como ela foi feita.</div>
            <div class="gravBox" id="gravBox">
              <div class="pt"></div>
              <div class="tx"><b id="gravTit">--</b><br><span id="gravMsg">--</span></div>
            </div>
            <!-- SEGURAR E SOLTAR, e nao dois botoes.
                 Com um botao para comecar e outro para terminar, o operador
                 tem de largar o braco para encerrar -- e o ultimo pedaco do
                 caminho sai a mao dele voltando para a tela. Segurando, a
                 gravacao acaba onde a mao acaba. -->
            <button class="b pri" id="btGravSeg" data-segurar="1">Segure para gravar</button>
            <div class="pq2" id="qGravSeg"></div>
            <button class="b mini" id="btGravIni">Iniciar gravacao</button>
            <div class="pq2" id="qGravIni"></div>
            <button class="b mini" id="btGravFim">Encerrar gravacao</button>
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

      <section class="pane" id="pnProg">
        <div class="et aberta" id="e2" data-e="2">
          <div class="cab"><div class="mk">2</div>
            <div class="tx"><div class="tt">Ensinar o caminho</div>
            <span class="sb" id="sb2">nenhum ponto</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="nt">Os pontos se gravam na aba <b>Mao livre</b>, levando
            o braco com a mao. Aqui eles se editam e se executam.</div>
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
            
          </div>
        </div>

        <div class="et" id="eDxf">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-arquivo"/></svg></div>
            <div class="tx"><div class="tt">Importar desenho DXF</div>
            <span class="sb" id="sbDxf">nenhum arquivo</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">

            <input type="file" id="dxfArq" accept=".dxf,text/plain" hidden>
            <button class="b pri" id="btDxfAbrir">Escolher arquivo DXF</button>
            <div class="res" id="dxfInfo">--</div>
            <div class="cp"><label>1 unidade do arquivo vale</label>
              <input type="number" id="dxfEsc" value="1" min="0.001" step="0.1"><span class="un">mm</span></div>
            <div class="nt">Deixe em 1 se o CAD estava em milimetros. Use 25,4
            para arquivo em polegadas.</div>
            <button class="b" id="btDxfPos">Posicionar na mesa</button>
            <div class="pq2" id="qDxfPos"></div>
            
          </div>
        </div>

      </section>

      <!-- ARQUIVOS SAIU DA COLUNA.
           Guardar e abrir trabalho nao e coisa que se faz olhando o
           braco: e uma biblioteca, e biblioteca quer largura. Virou
           gaveta de tela cheia, como a Configuracao, e se abre pelo
           atalho do cabecalho. -->

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
<!-- =====================================================================
     GAVETA DE ARQUIVOS (o atalho "Arquivos" do cabecalho)
     Mesmo molde da Configuracao: tela cheia, abaixo do cabecalho, com o
     fechar no mesmo canto. Duas gavetas com a mesma forma sao uma coisa
     so de aprender.
     ===================================================================== -->
<div class="veu cfgVeu" id="veuArq"><div class="cx cfgCx">
  <div class="cfgTopo">
    <h2>Arquivos</h2>
    <button class="b mini" id="arqFechar">Fechar</button>
  </div>
  <div class="cfgRol">
    <div class="pane on" id="pnArq">
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

            <!-- UMA biblioteca, nao duas.
                 Havia dois cartoes lado a lado -- "Programas salvos" e
                 "Trajetorias salvas" -- cada um com o seu campo de nome,
                 o seu botao Salvar e a sua lista. Para usar, era preciso
                 saber ANTES em qual das duas palavras o que voce acabou
                 de fazer se encaixa. Quem nunca operou nao sabe, e o
                 segundo cartao ainda nascia fechado.
                 Agora e um so: um campo de nome, um Salvar, uma lista.
                 O tipo aparece como etiqueta em cada linha -- depois de
                 salvo, quando ja nao e uma decisao. E a escolha de tipo
                 so aparece na tela quando a maquina tem MESMO as duas
                 coisas para guardar. -->
            <h4>Guardar o que esta na maquina</h4>
            <div class="seg" id="segGuardar" style="display:none">
              <button data-t="prog">programa</button>
              <button data-t="traj">trajetoria</button>
            </div>
            <div class="res" id="sdOque">--</div>
            <div class="linhaNome">
              <input id="sdNome" maxlength="24" placeholder="nome do arquivo" autocomplete="off">
              <button class="b mini" id="btSdSalvar" style="width:auto;margin:0">Salvar</button>
            </div>
            <div class="pq2" id="qSdSalvar"></div>

            <h4>No cartao</h4>
            <div id="sdLista"></div>
            <div class="nt">Os desenhos, os programas de ponto e as gravacoes
            a mao livre que voce fez na maquina. <b>Apagar tudo nao mexe
            neles</b> &mdash; so na memoria interna.</div>
          </div>
        </div>
    </div>
  </div>
</div></div>

<div class="veu cfgVeu" id="veuCfg"><div class="cx cfgCx">
  <div class="cfgTopo">
    <h2>Configuracao</h2>
    <button class="ajd" id="cfgAjuda" title="Mostrar ou esconder as explicacoes">?</button>
    <button class="b mini" id="cfgFechar">Fechar</button>
  </div>
  <!-- Procurar um ajuste pelo nome. Sao quatro paginas e uns quinze
       cartoes: lembrar em qual deles mora "aceleracao" e trabalho que a
       maquina pode fazer. Digitou, ela mostra os cartoes que casam --
       de TODAS as paginas -- e abre cada um. Vazio, tudo volta ao
       normal. -->
  <div class="cfgBusca">
    <input id="cfgProcurar" placeholder="procurar um ajuste pelo nome" autocomplete="off">
    <button class="b mini" id="cfgProcurarX" style="display:none">limpar</button>
  </div>
  <nav class="cfgAbas" id="cfgAbas">
    <button data-cfg="maquina" class="on">Maquina</button>
    <button data-cfg="calib">Calibracao</button>
    <button data-cfg="encoder">Encoder</button>
    <button data-cfg="sistema">Sistema</button>
  </nav>
  <div class="cfgRol">
    <div class="pane on" id="cfgMaquina">
      <!-- O ROTEIRO.
           A gaveta tinha quinze cartoes e nenhuma ordem. Quem monta a
           maquina pela primeira vez nao sabe o que vem antes do que, e
           nada na tela dizia -- descobria-se abrindo cartao por cartao.
           Aqui estao os cinco passos, na ordem, cada um dizendo se ja
           esta feito e levando ao lugar onde se faz. Quem ja instalou
           fecha e nunca mais abre. -->
      <div class="et aberta" id="etRoteiro">
        <div class="cab"><div class="mk"><svg class="ic"><use href="#i-lista"/></svg></div>
          <div class="tx"><div class="tt">Por onde comecar</div>
          <span class="sb" id="sbRoteiro">--</span></div><div class="chv">&#9654;</div></div>
        <div class="dentro">
          <div class="roteiro" id="roteiro"></div>
          <div class="nt">Nenhum destes passos e obrigatorio para mover o
          braco na mao. Eles sao o que faz a maquina saber ONDE ela esta
          &mdash; e sem isso um programa nao cai no mesmo lugar duas vezes.</div>
        </div>
      </div>

        <div class="et aberta" id="e1" data-e="1">
          <div class="cab"><div class="mk">1</div>
            <div class="tx"><div class="tt">Preparar a maquina</div>
            <span class="sb" id="sb1">servos e calibracao</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="tr"><div class="ch" id="chSrv1"><i></i></div>
              <span>torque no eixo 1</span></div>
            <div class="tr"><div class="ch" id="chSrv2"><i></i></div>
              <span>torque no eixo 2</span></div>
            <div class="nt">Uma chave por eixo. Programa e cordao precisam dos dois.</div>
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
            <!-- A FAIXA DA BARRA. Maquina nenhuma usa de 1 a 120 graus/s:
                 uma com redutor grande nunca passa de vinte, outra com
                 redutor curto so comeca a ser util acima de cinquenta.
                 Configurada aqui, a barra inteira passa a ser util nesta
                 maquina -- e o teto vira tambem um limite de seguranca,
                 guardado na maquina em vez de no navegador. -->
            <div class="cp"><label>Barra da aba Mover &middot; minimo</label>
              <input type="number" id="inVmn" min="0.1" step="0.5"><span class="un">°/s</span></div>
            <div class="cp"><label>Barra da aba Mover &middot; maximo</label>
              <input type="number" id="inVmx" min="1" max="720" step="1"><span class="un">°/s</span></div>
            <button class="b pri mini" id="btFaixaSalvar">Salvar a faixa</button>
            <div class="pq2" id="qFaixaSalvar"></div>
            <div class="seg" id="segVel">
              <button data-v="lento">Lento</button>
              <button data-v="normal">Normal</button>
              <button data-v="rapido">Rapido</button>
              <button data-v="custom">Ajustar</button>
            </div>
            <div class="res" id="resumoVel">--</div>
            
            <div id="velCustom" class="oculto">
              <div class="cp"><label>Jog normal</label><input type="number" id="inVn" min="0.1" step="0.5"><span class="un">°/s</span></div>
              <div class="cp"><label>Jog precisao</label><input type="number" id="inVp" min="0.1" step="0.1"><span class="un">°/s</span></div>
              <div class="cp"><label>Deslocamento</label><input type="number" id="inVa" min="0.1" step="0.5"><span class="un">°/s</span></div>
              <div class="cp"><label>Cordao</label><input type="number" id="inVc2" min="0.5" step="0.5"><span class="un">mm/s</span></div>
              <div class="cp"><label>Fator do motor 1</label><input type="number" id="inFv1" min="0.05" max="3" step="0.05"><span class="un">x</span></div>
              <div class="cp"><label>Fator do motor 2</label><input type="number" id="inFv2" min="0.05" max="3" step="0.05"><span class="un">x</span></div>
              <div class="nt">Multiplica a velocidade acima em cada motor.
              <b>1</b> = igual nos dois. Serve para a junta que carrega mais e
              nao aguenta o ritmo da outra.</div>
              
            </div>

            <h4>Partida e parada</h4>
            <div class="seg" id="segRampa">
              <button data-r="macia">Macia</button>
              <button data-r="media">Media</button>
              <button data-r="firme">Firme</button>
              <button data-r="custom">Ajustar</button>
            </div>
            <div class="res" id="resumoRampa">--</div>
            
            <div id="rampaCustom" class="oculto">
              <div class="cp"><label>Aceleracao da junta 1</label><input type="number" id="inA1" min="1" step="5"><span class="un">°/s²</span></div>
              <div class="cp"><label>Aceleracao da junta 2</label><input type="number" id="inA2" min="1" step="5"><span class="un">°/s²</span></div>
              <div class="cp"><label>Suavidade da partida</label><input type="number" id="inSuav" min="0" max="255" step="10"></div>
              
            </div>

            <button class="b pri" id="btSalvar">Salvar velocidade e partida</button>
            <div class="pq2" id="qSalvar"></div>

            <h4>Ate onde o braco pode ir</h4>
            <div class="res" id="resumoArea">--</div>
            
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
              <div class="nt av">Daqui para baixo sao numeros de montagem.
              Mexer neles sem medir muda a escala de tudo: o desenho na tela
              para de bater com o braco. O <b>redutor</b> mora na pagina
              Calibracao, junto do resto da escala.</div>

              <h4>Pulsos por volta do motor</h4>
              <div class="cp"><label>Junta 1</label><input type="number" id="inPv1" min="1"></div>
              <div class="cp"><label>Junta 2</label><input type="number" id="inPv2" min="1"></div>
              <div class="res" id="resumoRes">--</div>

              <h4>Sentido dos eixos</h4>
              <div class="tr"><div class="ch" id="sInv1"><i></i></div>
                <span>inverter a junta 1</span></div>
              <div class="tr"><div class="ch" id="sInv2"><i></i></div>
                <span>inverter a junta 2</span></div>

              <h4>Margens de seguranca</h4>
              <div class="cp"><label>Folga de dobra</label><input type="number" id="inDb" min="0" max="90"><span class="un">°</span></div>
              <div class="cp"><label>Y minimo</label><input type="number" id="inEy"><span class="un">mm</span></div>
              <div class="cp"><label>Raio morto</label><input type="number" id="inEr" min="0"><span class="un">mm</span></div>
              
              <button class="b pri" id="btSalvarGeo">Salvar margens</button>
              <div class="pq2" id="qSalvarGeo"></div>
            </div>
          </div>
        </div>

    </div>
    <div class="pane" id="cfgCalib">
      <div class="et aberta">
        <div class="cab"><div class="mk"><svg class="ic"><use href="#i-mira"/></svg></div>
          <div class="tx"><div class="tt">Calibrar o braco</div>
          <span class="sb" id="sbCurso">--</span></div><div class="chv">&#9654;</div></div>
        <div class="dentro">
          <div class="nt"><b>Dois gestos.</b> Toque em Calibrar: a maquina
          leva o braco ao zero e solta os motores. Empurre com a mao ate o
          extremo de um lado &mdash; os dois eixos de uma vez &mdash; e
          toque de novo. Ela volta ao zero sozinha e solta outra vez; voce
          vai ao outro extremo e toca. Acabou.</div>
          <div class="nt">Dali sai o curso de cada junta, a escala do encoder
          e os pulsos por volta de cada driver. Nada a digitar.</div>
          <button class="b pri" id="btCalIni2">Calibrar agora</button>
          <div class="pq2" id="qCalIni2"></div>
          <div class="res" id="calVivo">--</div>
          <button class="b mini x" id="btCalApagar2">Apagar os limites</button>
          <div class="pq2" id="qCalApagar2"></div>
          <div class="nt">Calibrar e <b>opcional</b>: sem curso medido a
          maquina opera igual. E medir o curso nao liga o limite &mdash; quem
          liga e voce, em Ajustes.</div>
        </div>
      </div>

      <div class="et aberta">
        <div class="cab"><div class="mk"><svg class="ic"><use href="#i-alvo"/></svg></div>
          <div class="tx"><div class="tt">Como a maquina esta agora</div>
          <span class="sb" id="sbCalib">--</span></div><div class="chv">&#9654;</div></div>
        <div class="dentro">
          <div class="grelha" id="calResumo"></div>
        </div>
      </div>

      <div class="et">
        <div class="cab"><div class="mk"><svg class="ic"><use href="#i-alvo"/></svg></div>
          <div class="tx"><div class="tt">Area da mesa</div>
          <span class="sb" id="sbMesa">--</span></div><div class="chv">&#9654;</div></div>
        <div class="dentro">
          <div class="perigo">Ensine os cantos com a <b>ponta</b> da tocha, nao com
          o cotovelo. A area e da ferramenta &mdash; o cotovelo passa por cima da
          mesa o tempo todo e nao solda nada.</div>
          <button class="b pri" id="btMesaCanto">Gravar canto na posicao atual</button>
          <div class="pq2" id="qMesaCanto"></div>
          <div class="res" id="mesaEstado">--</div>
          <button class="b mini x" id="btMesaLimpar">Apagar a area ensinada</button>
          <div class="pq2" id="qMesaLimpar"></div>
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
            
          </div>
        </div>

        <div class="et" id="etZero">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-casa"/></svg></div>
            <div class="tx"><div class="tt">Zero absoluto da maquina</div>
            <span class="sb" id="sbZero">avancado</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            <div class="res" id="zEstado">--</div>

            <div class="cadeado" id="zCadeado">
              <div class="ic">&#128274;</div>
              <div><b>Ajustes de origem</b>
              <span>errar aqui desloca a area util inteira &mdash; toque para abrir</span></div>
            </div>

            <div class="trancavel">
              <h4>Declarar a posicao de agora</h4>
              <button class="b mini x" id="btRefer">Declarar esta posicao como referencia</button>
              <div class="pq2" id="qRefer"></div>
              <div class="nt">O curso e contado a partir da referencia:
              declara-la fora do lugar desloca a area util inteira.</div>

              <h4>Ensinar o zero</h4>
              
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
            <!-- O REDUTOR MORA AQUI, embaixo da medicao daquela junta.
                 O encoder conta no eixo do MOTOR, antes do redutor: a
                 contagem so vira grau da junta passando por ele. Os dois
                 numeros pertencem a mesma conta, e estavam em telas
                 diferentes. -->
            <div class="cp"><label>Redutor</label>
              <input type="number" id="inRd1" min="0.1" step="0.1"><span class="un">: 1</span></div>
            <div class="nt">O unico numero que a calibracao nao mede: com o
            sensor antes do redutor, nenhuma medida revela a relacao dele.</div>
            <h4>Junta 2</h4>
            <div class="cp"><label>Endereco do driver</label><input type="number" id="encId2" min="1" max="247"></div>
            <div class="cp"><label>Registrador da posicao</label><input type="number" id="encReg2" min="0" max="65535"></div>
            <div class="cp"><label>Contagens por volta</label><input type="number" id="encCv2" min="1"></div>
            <div class="cp"><label>Redutor</label>
              <input type="number" id="inRd2" min="0.1" step="0.1"><span class="un">: 1</span></div>
            <button class="b pri mini" id="btRedSalvar">Salvar os redutores</button>
            <div class="pq2" id="qRedSalvar"></div>
            <h4 class="dobra">Escala do angulo</h4>
            <div class="sub">
            <div class="nt">Quantas contagens o encoder da por grau da junta.
            <b>A calibracao mede isto sozinha</b> &mdash; entre o limite
            positivo e o negativo ha um tanto de contagens e um tanto de graus.
            Aqui so se le o que ela achou.</div>
            <div class="res" id="escAtual">--</div>
            <button class="b mini x" id="btEscLimpar">Voltar ao calculo antigo</button>
            <div class="pq2" id="qEscLimpar"></div>
            </div>

            <h4 class="dobra">Habilita (SON)</h4>
            <div class="sub">
            <div class="nt">Registrador errado estraga o driver. Ache com <b>ferramentas/teste_rs485</b> antes de gravar.</div>
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
            
            <button class="b pri" id="btEncSalvar">Salvar ligacao</button>
            <div class="pq2" id="qEncSalvar"></div>
            <button class="b mini" id="btEncPadroes">Voltar aos padroes medidos</button>
            <div class="pq2" id="qEncPadroes"></div>

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
            
          </div>
        </div>

        <div class="et">
          <div class="cab"><div class="mk"><svg class="ic"><use href="#i-disco"/></svg></div>
            <div class="tx"><div class="tt">Copia da configuracao no cartao</div>
            <span class="sb" id="sbCfgCartao">--</span></div><div class="chv">&#9654;</div></div>
          <div class="dentro">
            
            <button class="b mini" id="btCfgRestaurar">Restaurar do cartao</button>
            <div class="pq2" id="qCfgRestaurar"></div>
            <div class="nt">Programas e trajetorias sao outra coisa: ficam na
            gaveta Arquivos e <b>nao</b> sao tocados nem por isto nem por "apagar
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

            <h4>Apagar tudo</h4>
            <div class="perigo"><b>Isto apaga a instalacao inteira</b> e
            reinicia a maquina: calibracao das juntas, area da mesa ensinada,
            zero absoluto, ligacao do encoder, contadores de producao e
            manutencao &mdash; tudo que esta gravado na memoria interna. Depois
            disso o braco precisa ser calibrado de novo antes de trabalhar.
            <br><b>O cartao SD nao e tocado.</b> As pecas salvas sao trabalho
            seu, nao configuracao da maquina; apagar programa continua sendo na
            gaveta Arquivos, um a um.</div>
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
  <h2>Calibrar o braco</h2>
  <div class="pp" id="cPasso">--</div>
  <div class="pgr"><i id="cBarra"></i></div>
  <div class="ins" id="cInstr"></div>

  <!-- Onde as DUAS juntas estao agora. Sao os unicos numeros da tela, e
       nenhum se digita: se le. -->
  <div class="res" id="cOnde">--</div>

  <div class="nt">Com os motores soltos voce empurra o braco com a mao e
  <b>sente</b> o batente. Os dois eixos de uma vez &mdash; com o braco
  solto, os dois estao soltos.</div>

  <!-- So na PRIMEIRA etapa: e aqui que o operador descobre que o braco
       gira ao contrario, e mandar cancelar para consertar era pedir para
       ele desistir. Da segunda em diante ja ha marca feita, e trocar o
       sinal inverteria o que foi medido. -->
  <div id="cSent" style="display:none">
    <div class="nt"><b>Confira o sentido antes de medir.</b> Aperte
    &#8635; (horario) em cada junta e veja para que lado ela vai de
    verdade. Se for ao contrario, marque aqui.</div>
    <div class="tr"><div class="ch" id="cInv1"><i></i></div>
      <span>inverter a junta 1</span></div>
    <div class="tr"><div class="ch" id="cInv2"><i></i></div>
      <span>inverter a junta 2</span></div>
  </div>
  <button class="b pri" id="cOk">Guardar este extremo</button>
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
  const estava=!!tm[j];
  if(tm[j]){clearInterval(tm[j]);delete tm[j];}
  if(el)el.classList.remove("press");
  /* So manda o zero se havia jog DESTA pagina. A captura de ponteiro faz
     pointerup e lostpointercapture chegarem em sequencia pelo mesmo
     gesto, e mandar o zero duas vezes so ocuparia a unica conexao do
     servidor. Se ainda assim ele se perder, o firmware corta o jog
     sozinho em 350 ms sem heartbeat (TIMEOUT_JOG_MS). */
  if(estava)ping("/api/jog?j="+j+"&d=0");
}
/* ---------- as setas ----------
   Voltaram a ser jog puro: segura e anda, solta e para. O modo de passo
   com incremento fixo saiu -- ele obrigava a escolher um numero antes de
   mexer no braco, e mexer no braco tem de ser direto. Quem quer chegar a
   um angulo exato usa "ir para um angulo", logo abaixo, que e onde essa
   frase ja existe.

   Havia aqui um anguloAtual() que preferia o encoder e caia no
   comandado. Ele era usado para preencher o destino do eixo NAO
   selecionado em "Levar", e era exatamente por onde as duas contas se
   misturavam. Saiu junto com o defeito: cada lugar agora diz qual conta
   esta usando, em vez de uma funcao escolher por todos. */

/* O GESTO FICA PRESO AO BOTAO, como ja acontecia no joystick.
   A coluna da direita rola e o conteudo dela muda de altura sozinho: a
   barra de estado ganha uma linha, o botao de proximo passo aparece ou
   some, uma dica surge sob outro botao. Qualquer uma dessas coisas tira
   a seta de baixo do dedo -- e sem captura isso disparava
   "pointerleave", o jog morria no meio do movimento e o eixo parava
   sozinho. Era o "faco um ajuste na aba e depois as setas nao movem o
   motor direito".
   Com setPointerCapture o botao continua recebendo o fluxo do ponteiro
   onde quer que ele va, e o pointerup chega sempre -- entao "pointerleave"
   sai da lista de parada: com captura ele dispara justamente quando o
   botao se move, nao quando o dedo levanta. */
document.querySelectorAll(".jb").forEach(function(b){
  let idAtivo=null;
  b.addEventListener("pointerdown",function(e){
    e.preventDefault();
    idAtivo=e.pointerId;
    try{b.setPointerCapture(e.pointerId);}catch(x){}
    const j=+b.dataset.j;
    juntaSel=j;
    jogOn(b.dataset.j,b.dataset.d,b);
  });
  ["pointerup","pointercancel","lostpointercapture"].forEach(function(v){
    b.addEventListener(v,function(e){
      if(idAtivo!==null&&e.pointerId!==undefined&&e.pointerId!==idAtivo)return;
      idAtivo=null;
      jogOff(b.dataset.j,b);
    });});
});


/* Velocidade do jog numa barra, em POR CENTO do que a maquina aceita.
   Antes so existia em Ajustes, em graus por segundo, longe do braco --
   e velocidade e coisa que se acerta olhando o braco andar. */
/* ---------------------------------------------------------------------
   VELOCIDADE, NA UNIDADE DE QUEM OPERA

   A maquina pensa em GRAUS POR SEGUNDO -- e o que vira frequencia de
   pulso, e nao ha outro jeito. Quem esta na bancada pensa em MILIMETRO
   POR SEGUNDO: e a velocidade da ponta, a mesma unidade do cordao, e a
   unica que da para comparar com a solda que se fez a mao.

   A conversao e a do braco esticado: a ponta a R = elo1 + elo2 do eixo
   anda R x omega. Usar o alcance CHEIO e a escolha conservadora -- em
   qualquer outra postura a ponta anda mais devagar que o numero pedido,
   nunca mais rapido. Um raio que mudasse com a postura faria o mesmo
   ajuste significar coisas diferentes a cada movimento.

   O ajuste vale para o jog E para "ir para um angulo": os dois sao a mao
   do operador movendo o braco, e ate aqui cada um obedecia a um numero
   diferente (velN e velA), guardados em telas diferentes. Era essa a
   parte "complexa" -- subir a barra do jog e o posicionamento continuar
   lerdo, porque quem mandava nele era outro campo.

   O modo Precisao continua com o valor dele: e o proposito daquele
   botao.
   --------------------------------------------------------------------- */
/* A faixa da barra vem da maquina (velMn/velMx no estado), configurada
   em Ajustes. Estes sao so os valores enquanto o primeiro estado nao
   chegou -- e o teto de seguranca do firmware. */
let VEL_GRAUS_MAX = 60;
let VEL_GRAUS_MIN = 2;
/* Tempo de subida ate a velocidade escolhida. E ele que se mantem
   constante -- nao a aceleracao. */
const VEL_RAMPA_S = 0.35;

function alcanceMm(){ return (D.l1||200)+(D.l2||200); }
function grausParaMm(g){ return g*Math.PI*alcanceMm()/180; }
function mmParaGraus(v){ const R=alcanceMm(); return R>1 ? v*180/(Math.PI*R) : v; }

function velFaixa(){
  const mn=(typeof D.velMn==="number"&&D.velMn>0)?D.velMn:VEL_GRAUS_MIN;
  const mx=(typeof D.velMx==="number"&&D.velMx>mn)?D.velMx:VEL_GRAUS_MAX;
  VEL_GRAUS_MIN=mn; VEL_GRAUS_MAX=mx;
  return [mn,mx];
}

/* Os cinco degraus repartem a FAIXA configurada da maquina, nao o teto
   absoluto: numa maquina cujo maximo util e vinte graus por segundo, o
   degrau 5 tem de ser vinte, e nao um numero que ela nunca alcanca. O 1
   e o minimo e o 5 e o maximo, entao nenhum degrau e inalcancavel. */
const VEL_NIVEIS=5;
function velDoNivel(n){
  const f=velFaixa();
  const k=Math.min(VEL_NIVEIS,Math.max(1,n));
  return f[0]+(f[1]-f[0])*((k-1)/(VEL_NIVEIS-1));
}
function nivelDaVel(g){
  const f=velFaixa();
  if(f[1]<=f[0])return 1;
  const bruto=1+(g-f[0])/(f[1]-f[0])*(VEL_NIVEIS-1);
  return Math.min(VEL_NIVEIS,Math.max(1,Math.round(bruto)));
}

function velMostrar(g){
  velFaixa();
  const gg=Math.min(VEL_GRAUS_MAX,Math.max(VEL_GRAUS_MIN,g));
  const niv=nivelDaVel(gg);
  $("velNiveis").querySelectorAll("[data-niv]").forEach(function(b){
    b.classList.toggle("on",+b.dataset.niv===niv);});
  if(document.activeElement!==$("inVelMm"))
    $("inVelMm").value=Math.round(grausParaMm(gg));
  $("velMovTx").textContent=gg.toFixed(0)+" \u00b0/s "+tr("na junta");
  /* O alcance usado na conta fica no titulo: explica o numero para quem
     for procurar, sem quebrar a linha para quem so quer operar. */
  $("velMovTx").title=tr("medido na ponta com o braco esticado")+
    " ("+Math.round(alcanceMm())+" mm)";
}

let velEnviando=false, velUltimoEnviado=-1;
function velEnviar(g){
  velFaixa();
  const gg=Math.min(VEL_GRAUS_MAX,Math.max(VEL_GRAUS_MIN,g));
  velMostrar(gg);
  if(velEnviando) return;
  velEnviando=true;
  velUltimoEnviado=gg;
  /* Os dois campos de uma vez: o jog e o posicionamento sao o mesmo
     gesto para quem opera, e separa-los so fazia a barra parecer que
     nao funcionava.

     E a RAMPA vai junto. A aceleracao era um numero fixo, entao subir a
     velocidade so alongava a subida: a 120 graus/s com rampa de 60
     graus/s a segunda demorava dois segundos para chegar na velocidade
     pedida, e o movimento inteiro virava rampa -- o braco parecia
     engasgar em vez de andar. Amarrando a rampa a velocidade, o TEMPO
     de subida fica constante e o movimento tem a mesma cara em qualquer
     velocidade. */
  const v=gg.toFixed(1);
  const a=Math.min(5000,Math.max(10,gg/VEL_RAMPA_S)).toFixed(0);
  post("/api/config?velN="+v+"&velA="+v+"&acel1="+a+"&acel2="+a)
    .then(function(){velEnviando=false;})
    .catch(function(){velEnviando=false;});
}

$("velNiveis").querySelectorAll("[data-niv]").forEach(function(b){
  b.onclick=function(){ velEnviar(velDoNivel(+b.dataset.niv)); };
});
$("inVelMm").oninput=function(){
  const v=parseFloat($("inVelMm").value);
  if(!isNaN(v))$("velMovTx").textContent=mmParaGraus(v).toFixed(0)+" \u00b0/s";
};
$("inVelMm").onchange=function(){
  const v=parseFloat($("inVelMm").value);
  if(isNaN(v)){velMostrar(velUltimoEnviado>0?velUltimoEnviado:20);return;}
  velEnviar(mmParaGraus(v));
};
/* Lento, normal e rapido sao apelidos dos degraus 1, 3 e 5 -- os mesmos
   degraus dos numeros, nao uma segunda escala. Quem prefere a palavra
   aperta a palavra, quem prefere o numero aperta o numero, e o degrau
   aceso e o mesmo nos dois. */
document.querySelectorAll("[data-vel]").forEach(function(b){
  b.onclick=function(){ velEnviar(velDoNivel(+b.dataset.vel)); };
});
$("btTesteMov").onclick=function(){post("/api/teste/rele","qMoverSel");};

$("btMoverSel").onclick=function(){
  const j=juntaSel;
  const alvo=parseFloat($("inMtSel").value);
  /* Sem alvo, DIZER que falta o alvo. Sair calado deixava o botao
     parecendo quebrado. */
  if(isNaN(alvo)){
    const q=$("qMoverSel");
    if(q){q.textContent=tr("digite o angulo de destino");q.style.display="block";}
    return;
  }
  /* O OUTRO EIXO TEM DE FICAR PARADO -- e "parado" se escreve na conta
     do FIRMWARE, nao na do encoder.
     /api/mover recebe um destino ABSOLUTO para as duas juntas e o
     converte em pulsos pela contagem que ele mesmo mantem (D.t1/D.t2).
     Mandar aqui o angulo MEDIDO (anguloAtual, que prefere o encoder)
     misturava as duas contas: onde elas divergem -- perda de passo,
     folga, escala recem-medida -- o firmware via um destino diferente da
     posicao atual e mexia num eixo que ninguem pediu para mexer. Levar a
     junta 1 a zero sacudia a junta 2 junto.
     Mandando a propria contagem do firmware, a diferenca e exatamente
     zero e o eixo nao recebe pulso nenhum. */
  const t1=(j===1)?alvo:(D.t1||0);
  const t2=(j===2)?alvo:(D.t2||0);
  post("/api/mover?t1="+t1.toFixed(2)+"&t2="+t2.toFixed(2),"qMoverSel");
};
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
/* A gaveta exclusiva -- abrir uma fecha as outras -- existe por causa do
   CELULAR: numa coluna estreita duas gavetas abertas ja empurram a
   terceira para fora da tela. Na Configuracao em tela de computador o
   efeito e o contrario: os cartoes entram em colunas, e manter so um
   aberto deixava dois tercos da tela em branco com o resto escondido
   atras de um clique. Larga, la a gaveta so ALTERNA. */
function cfgEmColunas(){
  try{ return matchMedia("(min-width:1000px)").matches; }catch(e){ return false; }
}
document.querySelectorAll(".cab").forEach(function(c){
  if(!c.querySelector(".chv"))return;
  c.addEventListener("click",function(){
    const et=c.parentElement,ja=et.classList.contains("aberta");
    if(et.closest("#veuCfg")&&cfgEmColunas()){
      et.classList.toggle("aberta",!ja);return;}
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
  /* A calibracao nao pergunta mais nada: os dois numeros da rota ficaram
     por compatibilidade e vao sempre em zero. */
  post("/api/calib/confirmar");
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
    "&fvel1="+($("inFv1").value||1)+"&fvel2="+($("inFv2").value||1)+
    "&suav="+$("inSuav").value);
}
$("btSalvar").onclick=function(){salvar($("inVc2").value);};

/* A faixa da barra e salva sozinha: ela nao pertence ao mesmo formulario
   que as tres velocidades, e mandar tudo junto faria salvar a faixa
   reaplicar valores que o operador nao tocou. */
$("btFaixaSalvar").onclick=function(){
  const mn=parseFloat($("inVmn").value), mx=parseFloat($("inVmx").value);
  if(!(mn>0)||!(mx>mn)){
    acao("FaixaSalvar","o minimo tem de ser menor que o maximo");return;}
  acao("FaixaSalvar","");
  post("/api/config?velMin="+mn+"&velMax="+mx,"qFaixaSalvar");
};

/* ---------- escala do angulo ----------
   Ela era ensinada aqui, em dois toques: marca onde esta, leva a junta
   ate um angulo conhecido, diz quantos graus foram. Virou consequencia
   de calibrar -- a calibracao ja anda de um limite ao outro, e ali estao
   as contagens e os graus de que a divisao precisa. O que sobra aqui e
   a leitura, e o botao de voltar ao calculo antigo. */
$("btEscLimpar").onclick=function(){
  /* Zerar volta ao caminho antigo (contagens por volta + reducao) sem
     apagar mais nada. Vale para as duas juntas: nao ha mais seletor. */
  post("/api/encoder/config?cg1=0&cg2=0","qEscLimpar");
};

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

    if($("inRd1")!==document.activeElement) $("inRd1").value=j.red1;
    if($("inRd2")!==document.activeElement) $("inRd2").value=j.red2;

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
  if(!$("veuCfg")||!$("veuCfg").classList.contains("on"))return;
  if(cfgAtual==="calib")calibAtualizar();
  /* O roteiro le o estado real: calibrar ou ensinar a mesa noutra aba
     tem que riscar o passo aqui sem ninguem reabrir a gaveta. */
  if(cfgAtual==="maquina"&&typeof roteiroPintar==="function")roteiroPintar();
}, 500);

$("btMesaCanto").onclick=function(){
  post("/api/mesa/canto").then(calibAtualizar);};
$("btMesaLimpar").onclick=function(){
  if(!confirm("Apagar a area da mesa ensinada?\n\nO braco volta a se proteger so pelo Y minimo e pelo raio da base."))return;
  post("/api/mesa/limpar").then(calibAtualizar);};

$("btCalIni2").onclick=function(){ post("/api/calib/iniciar","qCalIni2"); };

/* O botao da barra de estado faz o que o passo diz. Ele nao tem acao
   propria: e um atalho para a acao que ja existe em algum lugar da tela,
   posto onde o operador esta olhando. */
$("teAcao").onclick=function(){ if(proximoAtual)proximoAtual.faz(); };
$("btCalApagar2").onclick=function(){
  if(confirm(tr("Apagar os limites gravados?")+"\n\n"+
             tr("A maquina continua operando -- ela so fica sem protecao de curso.")))
    post("/api/calib/apagar","qCalApagar2");
};
/* O redutor e o unico numero que a calibracao nao mede: com o encoder no
   eixo do motor, antes do redutor, nenhuma medida revela a relacao dele.
   Salvar aqui manda so ele; o resto do formulario de ajustes segue
   valendo o que ja estava. */
$("btRedSalvar").onclick=function(){
  const r1=parseFloat($("inRd1").value), r2=parseFloat($("inRd2").value);
  if(!(r1>0)||!(r2>0)){acao("RedSalvar","o redutor tem de ser maior que zero");return;}
  acao("RedSalvar","");
  post("/api/config?red1="+r1+"&red2="+r2,"qRedSalvar").then(calibAtualizar);
};

/* Gravar canto so liga quando ha o que gravar: apertar e nao acontecer
   nada e o defeito mais antigo deste painel. Calibracao deixou de ser
   exigencia -- a mesa e ensinada em coordenada, e a coordenada existe
   com ou sem limites medidos. */
function afEstado(){
  acao("MesaCanto", !D.modo ? "sem contato com o robo"
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
      /* PRIMEIRA LINHA, de proposito: QUAL firmware esta rodando.
         Faltava, e a falta custou caro -- um defeito ja corrigido no
         fonte continuou aparecendo na bancada e nao havia nada na tela
         que dissesse se aquela placa tinha ou nao a correcao. */
      ["Firmware nesta placa",   j.fw ? String(j.fw).slice(0,12) : "--"],
      ["Ligada ha",              dur(j.up)],
      ["Pecas prontas",          j.ciclos+" (nesta sessao: "+j.ciclosSes+")"],
      ["Interrompidas no meio",  String(j.abortados)],
      ["Arco aberto, no total",  dur(j.arcoS)],
      ["Desde a manutencao",     j.manut+" pecas"],
      ["Encoder junta 1",        enc(j.enc1)],
      ["Encoder junta 2",        enc(j.enc2)],
      ["Travamentos",            String(j.trav)],
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
    const ruim=j.trav>0||encRuim;
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
  /* O programa E uma sequencia, e agora ele PARECE uma: os pontos ficam
     presos num trilho, e o pedaco de trilho entre dois pontos e o
     trecho -- laranja onde ha cordao, cinza onde so desloca. Antes eram
     duas linhas soltas e a pergunta "esse trecho solda?" pedia leitura
     de texto, em vez de um olhar.

     A chave do cordao foi para a DIREITA, onde estao todos os controles
     do resto da tela; a esquerda ficou com o trilho, que e leitura. */
  let h='<div class="lista prog">';
  let percurso=0, cordao=0;
  pontos.forEach(function(p,i){
    const ag=(D.modo==="EXECUTANDO"&&D.progIdx===i)?" agora":"";
    h+='<div class="p'+ag+'"><div class="n">'+(i+1)+'</div>'+
       '<div class="c"><em>X'+p.x+' Y'+p.y+'</em>'+
       '<span class="ang">'+p.t1.toFixed(0)+'° / '+p.t2.toFixed(0)+'°</span></div>'+
       '<button class="mb" data-ir="'+i+'">ir</button>'+
       '<button class="mb x" data-del="'+i+'">apagar</button></div>';
    if(i<pontos.length-1){
      const d=Math.round(Math.hypot(pontos[i+1].x-p.x,pontos[i+1].y-p.y));
      percurso+=d; if(p.s)cordao+=d;
      h+='<div class="tr'+(p.s?" q":"")+(p.av?" ruim":"")+'">'+
         '<span>'+(i+1)+'&rarr;'+(i+2)+' · '+d+' mm · '+
         (p.s?tr("cordao"):tr("so desloca"))+'</span>'+
         '<div class="ch'+(p.s?" on":"")+'" data-sw="'+i+'" '+
         'title="ligar ou desligar o cordao neste trecho"><i></i></div></div>';
      /* O trecho e conferido enquanto o operador ensina: descobrir que o
         cordao nao passa so na hora de apertar Executar e tarde. */
      if(p.av)h+='<div class="avTr">'+p.av+'</div>';}
  });
  /* Quanto o braco anda e quanto disso e cordao: sao as duas contas que
     o operador faz de cabeca antes de mandar executar -- tempo de ciclo
     e consumo de arame. */
  if(pontos.length>1)
    h+='<div class="somaProg"><span>'+tr("percurso")+' <b>'+percurso+' mm</b></span>'+
       '<span class="q">'+tr("cordao")+' <b>'+cordao+' mm</b></span></div>';
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
/* Clareia (f>0) ou escurece (f<0) uma cor. Aceita #rrggbb e rgb().
   Nasceu dentro da vista 3D, para sombrear as faces do cilindro; a
   vista de cima passou a precisar dela pelo mesmo motivo -- dar VOLUME
   a uma figura chapada -- entao subiu para o nivel do arquivo em vez de
   ser copiada. */
function tom(hex,f){
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
}

function paleta(){
  const t=document.documentElement.getAttribute("data-tema")||"claro";
  if(t===palQuando)return PAL;
  palQuando=t;
  PAL={papel:cor("--papel"),grade:cor("--grade"),arco:cor("--arco"),
       quente:cor("--quente"),brasa:cor("--brasa"),letra:cor("--letra"),
       letra2:cor("--letra2"),letra3:cor("--letra3"),pronto:cor("--pronto"),
       elo1:t==="escuro"?"#8b98a9":"#5a6675",
       elo2:t==="escuro"?"#b7c2d1":"#8794a5",
       juntaF:t==="escuro"?"#2a333e":"#ffffff",
       /* carcaca do mancal e a base parafusada: cinza de aluminio, mais
          claro que o elo para o eixo se destacar do braco */
       mancal:t==="escuro"?"#9fadbd":"#c4ccd6",
       base:t==="escuro"?"#3a434f":"#9aa4b0",
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
   esta de verdade -- e SO ela. O comandado chegou a ser desenhado por
   tras, tracejado, para dar a ver a diferenca; saiu porque durante todo
   movimento a conta vai a frente do braco pela rampa e o tracejado
   piscava a cada viagem. Alarme que toca em condicao normal ensina a
   ignorar alarme.

   Sem leitura confiavel (encoder desligado, cabo solto, leitura
   impossivel) o boneco CONGELA na ultima postura medida -- e a legenda
   diz isso, porque um boneco que muda de significado sem avisar e pior
   que nenhum.
   ===================================================================== */

function legendaPostura(z){
  /* A frase do desvio saiu junto com o tracejado: ela so descrevia
     aquele tracejado. O que sobra e de onde vem a posicao desenhada,
     que continua importando. */
  if(z.medido) return z.completo ? "posicao medida pelo encoder"
                                 : "posicao medida (uma junta sem leitura)";
  if(z.travado) return "ultima posicao medida (sem leitura agora)";
  return "sem leitura do encoder ainda";
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

/* O que se DESENHA vem so do encoder, ja passado pela reducao medida
   (contagensPorGrau) -- nunca da contagem de pulsos comandada. Passo
   perdido, folga do redutor ou um driver mal calibrado nao aparecem no
   comandado; so aparecem no braco de verdade, que e o que o encoder ve.
   Misturar os dois deixaria o desenho mentindo justo quando ele mais
   precisa avisar que algo saiu do lugar.

   Sem leitura confiavel, a junta CONGELA no ultimo angulo medido -- nao
   volta a seguir o comandado. Um driver sem encoder no barramento
   continua desenhado (na ultima postura que se sabia real), em vez de
   passar a desenhar uma posicao que ninguem mediu. */
let ultimoMedido = {t1:0, t2:0, tem1:false, tem2:false};

function postura(){
  const tem1 = !!D.m1ok, tem2 = !!D.m2ok;
  if(tem1){ ultimoMedido.t1 = D.m1 || 0; ultimoMedido.tem1 = true; }
  if(tem2){ ultimoMedido.t2 = D.m2 || 0; ultimoMedido.tem2 = true; }
  const sv = suavizar(ultimoMedido.t1, ultimoMedido.t2);
  /* Nao ha mais c1/c2 nem desvio aqui: eram do fantasma tracejado, e ele
     saiu. O desenho depende SO do encoder, e quem compara comandado com
     medido e a regua do rodape, que mostra os dois numeros. */
  return {
    t1: sv.t1, t2: sv.t2,         /* o que se desenha */
    medido: tem1 || tem2,
    completo: tem1 && tem2,
    travado: ultimoMedido.tem1 || ultimoMedido.tem2
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

  const mistura=tom;

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

  /* O FANTASMA TRACEJADO DO COMANDADO SAIU TAMBEM DA VISTA 3D.
     Ele desenhava o braco onde a contagem de pulsos achava que ele
     estava, para dar a VER a diferenca entre a conta e o ferro. So que
     durante todo movimento a conta vai a frente do braco pela rampa, e
     o tracejado piscava a cada viagem -- alarme que toca em condicao
     normal ensina a ignorar alarme. E depois que o braco para, quem
     fecha essa diferenca e o assentamento, sem ninguem precisar olhar.
     Quem quiser o numero tem os dois angulos na regua do rodape,
     comandado e medido lado a lado, e o aviso de desvio na saude. */

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

      /* Contorno: as duas bordas de t1 (raios) e as duas de t2 (arcos).
         Em azul cheio ele era a coisa mais forte da tela -- e o alcance
         nao muda nunca: e cenario, nao informacao. Passou a ser um risco
         cinza fino, do mesmo peso da grade. O azul volta a significar
         uma coisa so: o braco e a ponta que ele carrega. */
      ct.save();
      ct.strokeStyle="rgba("+C.grade+",.55)";
      ct.lineWidth=1;ct.globalAlpha=1;
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

  /* ---------------------------------------------------------------
     O BRACO

     Ate aqui cada elo era um TRACO: uma linha com lineWidth grande. De
     longe passava por braco; de perto era uma barra chapada, sem comeco
     nem fim, e no cotovelo as duas se cruzavam sem que desse para dizer
     qual passa por cima da outra.

     Agora cada elo e um CORPO. Uma capsula -- pontas arredondadas no
     raio do proprio mancal -- sombreada ao longo da ESPESSURA, como um
     tubo redondo iluminado de cima. E o cotovelo ganhou ordem: o elo 2
     e pintado depois, entao encobre o 1, que e o que acontece na
     maquina.

     As juntas deixaram de ser bolinha soltas e viraram MANCAL: carcaca
     com aro e parafusos de flange, e no centro o disco que diz se
     aquele eixo tem torque. Assim a pergunta "onde fica o eixo" e a
     pergunta "esse eixo esta ligado" tem cada uma o seu sinal, no mesmo
     lugar, sem uma apagar a outra.
     --------------------------------------------------------------- */
  const PZ=postura();
  const t1=PZ.t1*Math.PI/180,t2=(PZ.t1+PZ.t2)*Math.PI/180;
  const c=P(L1*Math.cos(t1),L1*Math.sin(t1));
  const p=P(L1*Math.cos(t1)+L2*Math.cos(t2),L1*Math.sin(t1)+L2*Math.sin(t2));
  const e1=Math.max(4,L1*esc*0.085), e2=Math.max(3,L2*esc*0.068);

  /* Um elo: capsula com sombreado cilindrico no sentido da espessura.
     A luz vem de cima-esquerda, entao a faixa clara nao fica no meio --
     fica a um terco da borda, que e o que a vista faz parecer redondo. */
  const capsula=function(a,b,espA,espB,base){
    const ang=Math.atan2(b[1]-a[1],b[0]-a[0]);
    const rA=espA/2, rB=(espB||espA)/2, r=Math.max(rA,rB);
    const px=-Math.sin(ang), py=Math.cos(ang);
    const g=ct.createLinearGradient(a[0]-px*r,a[1]-py*r,a[0]+px*r,a[1]+py*r);
    g.addColorStop(0.00,tom(base,-0.30));
    g.addColorStop(0.28,tom(base, 0.32));
    g.addColorStop(0.55,base);
    g.addColorStop(1.00,tom(base,-0.42));
    /* Os dois arcos sao meias-luas nas pontas; o canvas fecha os lados
       com reta, e e essa reta que da o AFUNILAMENTO quando as duas
       espessuras sao diferentes -- elo grosso no mancal, fino na ponta,
       como um braco de verdade. */
    ct.beginPath();
    ct.arc(a[0],a[1],rA,ang+Math.PI/2,ang-Math.PI/2);
    ct.arc(b[0],b[1],rB,ang-Math.PI/2,ang+Math.PI/2);
    ct.closePath();
    ct.fillStyle=g;ct.fill();
    ct.strokeStyle=tom(base,-0.55);ct.lineWidth=1;ct.stroke();
  };

  /* A COR DO DISCO CENTRAL DIZ SE AQUELA JUNTA TEM TORQUE.
     Verde tem, vermelho nao, cinza enquanto o barramento nao confirma --
     "mandei" e "tem torque" deixaram de ser a mesma coisa quando o
     habilita virou Modbus. */
  const corDoEixo=function(k){
    if(D.sonEst===1) return C.letra3;
    return ((k===1)?D.srv1:D.srv2) ? C.pronto : C.brasa;
  };

  /* Um mancal. O anel de foco (junta selecionada) vem por fora de tudo:
     cor diz torque, anel diz foco -- duas perguntas, dois sinais. */
  const mancal=function(x,y,raio,corTorque,focada){
    const g=ct.createRadialGradient(x-raio*0.4,y-raio*0.45,raio*0.08,x,y,raio);
    g.addColorStop(0.00,tom(C.mancal, 0.40));
    g.addColorStop(0.62,C.mancal);
    g.addColorStop(1.00,tom(C.mancal,-0.36));
    ct.beginPath();ct.arc(x,y,raio,0,TAU);
    ct.fillStyle=g;ct.fill();
    ct.strokeStyle=tom(C.mancal,-0.52);ct.lineWidth=1.2;ct.stroke();
    /* Parafusos do flange: so quando ha pixel para eles. Abaixo disso
       viram sujeira e o mancal fica pior do que sem. */
    if(raio>=11){
      ct.fillStyle=tom(C.mancal,-0.30);
      for(let k=0;k<6;k++){
        const a=k*TAU/6+0.45;
        ct.beginPath();
        ct.arc(x+Math.cos(a)*raio*0.72,y+Math.sin(a)*raio*0.72,
               Math.max(1,raio*0.10),0,TAU);
        ct.fill();
      }
    }
    ct.beginPath();ct.arc(x,y,raio*0.46,0,TAU);
    ct.fillStyle=corTorque;ct.fill();
    ct.strokeStyle=C.juntaF;ct.lineWidth=1.5;ct.stroke();
    if(focada){
      ct.save();
      ct.strokeStyle=C.arco;ct.lineWidth=2.5;ct.globalAlpha=.95;
      ct.beginPath();ct.arc(x,y,raio+4.5,0,TAU);ct.stroke();
      ct.restore();
    }
  };

  /* Base parafusada no chao: o braco deixa de flutuar. Nao e enfeite --
     e ela que diz de onde a maquina nasce, e por isso o eixo 1 nunca
     esta "no meio do nada". */
  {
    const rb=Math.max(14,e1*1.55);
    const g=ct.createLinearGradient(ox-rb,oy-rb,ox+rb,oy+rb);
    g.addColorStop(0,tom(C.base, 0.26));
    g.addColorStop(1,tom(C.base,-0.32));
    ct.beginPath();ct.arc(ox,oy,rb,0,TAU);
    ct.fillStyle=g;ct.fill();
    ct.strokeStyle=tom(C.base,-0.50);ct.lineWidth=1;ct.stroke();
    /* Quatro furos de chumbador, nas diagonais: e o que faz a figura
       parecer PARAFUSADA e nao pousada. */
    ct.fillStyle=tom(C.base,-0.42);
    for(let k=0;k<4;k++){
      const a=Math.PI/4+k*Math.PI/2;
      ct.beginPath();
      ct.arc(ox+Math.cos(a)*rb*0.76,oy+Math.sin(a)*rb*0.76,
             Math.max(1.2,rb*0.10),0,TAU);
      ct.fill();
    }
  }

  ct.save();
  ct.shadowColor=cor("--sombra");ct.shadowBlur=9;ct.shadowOffsetY=3;
  capsula([ox,oy],c,e1,e1*0.80,C.elo1);
  capsula(c,p,e2,e2*0.70,C.elo2);
  ct.restore();

  /* O mancal do cotovelo nao pode encolher junto com o elo 2: ele
     carrega o disco de torque, que precisa ser visto. Piso no elo 1. */
  mancal(ox,oy,e1*0.86,corDoEixo(1),juntaSel===1);
  mancal(c[0],c[1],Math.max(e2*0.95,e1*0.55),corDoEixo(2),juntaSel===2);

  /* rastro proporcional a velocidade da ponta */
  const vv=D.vPonta||0;
  if(vv>0.5){
    const dir=Math.atan2(p[1]-c[1],p[0]-c[0])+Math.PI/2;
    const cauda=Math.min(46,vv*esc*0.5+6);
    ct.strokeStyle=D.solda?C.quente:C.arco;ct.globalAlpha=.4;
    ct.lineWidth=3;ct.beginPath();
    ct.moveTo(p[0],p[1]);
    ct.lineTo(p[0]-Math.cos(dir)*cauda,p[1]-Math.sin(dir)*cauda);ct.stroke();ct.globalAlpha=1;}

  /* A FERRAMENTA na ponta. Um circulo solto nao dizia de que lado o
     bico aponta; agora ha um flange preso ao elo 2 e um bico saindo na
     direcao do elo, que e para onde o cordao vai. Quando a solda esta
     ligada o bico brilha -- e o unico ponto da figura que pulsa, para
     nao competir com nada. */
  {
    const ang=Math.atan2(p[1]-c[1],p[0]-c[0]);
    const rb=Math.max(3.5,e2*0.62);
    ct.save();
    /* flange: continua o elo por um pedaco curto, em metal */
    capsula(p,[p[0]+Math.cos(ang)*rb*1.1,p[1]+Math.sin(ang)*rb*1.1],
            rb*1.35,rb*1.05,C.mancal);
    if(D.solda){
      ct.shadowColor=C.quente;
      ct.shadowBlur=16+7*Math.sin(Date.now()/90);
      ct.fillStyle=C.quente;
    }else{ct.fillStyle=C.arco;}
    ct.beginPath();
    ct.arc(p[0]+Math.cos(ang)*rb*1.1,p[1]+Math.sin(ang)*rb*1.1,rb*0.72,0,TAU);
    ct.fill();
    ct.restore();
  }

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
/* Qual junta esta selecionada no desenho. Os controles da aba Mover
   seguem esta escolha, entao selecionar aqui e o mesmo que escolher la:
   um conceito, dois lugares de tocar. */
let juntaSel = 1;

/* Distancia do ponto ao segmento, em mm da mesa. E como se decide em
   qual elo o dedo caiu. */
function distSegmento(px,py,ax,ay,bx,by){
  const dx=bx-ax, dy=by-ay, L=dx*dx+dy*dy;
  let u = L>1e-9 ? ((px-ax)*dx+(py-ay)*dy)/L : 0;
  u=Math.max(0,Math.min(1,u));
  return Math.hypot(px-(ax+dx*u), py-(ay+dy*u));
}

/* Em qual elo o toque caiu, ou 0 se caiu na mesa. A folga acompanha o
   zoom: o alvo tem de ser do tamanho do dedo, nao do desenho. */
function juntaNoPonto(q){
  const L1=D.l1||200,L2=D.l2||200,PZ=postura();
  const t1=PZ.t1*Math.PI/180, t2=(PZ.t1+PZ.t2)*Math.PI/180;
  const cx=L1*Math.cos(t1), cy=L1*Math.sin(t1);
  const px=cx+L2*Math.cos(t2), py=cy+L2*Math.sin(t2);
  const folga=Math.max(14, 22/(esc||1));
  const d1=distSegmento(q[0],q[1],0,0,cx,cy);
  const d2=distSegmento(q[0],q[1],cx,cy,px,py);
  if(d1>folga && d2>folga) return 0;
  return (d2<=d1) ? 2 : 1;      /* empate vai para o antebraco, que fica por cima */
}

/* A MESA E SO DESENHO.
   Tocar nela nunca manda o braco andar. Existiu aqui um botao IR que,
   ligado, mandava a ponta ate o ponto tocado -- e um botao DES, que
   deixava riscar o caminho com o dedo. Os dois sairam a pedido de quem
   opera: o toque na mesa agora tem um proposito so, escolher o eixo, e
   o caminho se ensina pelos pontos gravados ou importando um DXF.
   Levar o braco a um lugar se faz pelas setas e por "ir para um angulo",
   que dizem para onde vao antes de ir. */
cv.addEventListener("click",function(e){
  /* No modo posicionar o toque e arraste do desenho importado. */
  if(posOn)return;
  /* Tocar SOBRE o braco seleciona aquela junta. E a unica coisa que um
     toque na mesa faz -- e escolher o eixo nunca move o robo. */
  const j=juntaNoPonto(mmDe(e));
  if(!j)return;
  juntaSel=j;
  pintar();
});

/* MAX_PONTOS do firmware. Chega no /api/status: deixar o numero fixo
   aqui fazia a pagina simplificar para um limite que o robo nao tem
   mais. Ate a primeira resposta vale o valor conservador. */
let MAX_PTS=40;

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
/* Arrastar o desenho IMPORTADO para posiciona-lo na mesa. E o unico
   gesto de arraste que sobrou no canvas: riscar o caminho com o dedo
   saiu junto com o botao DES. */
let arrastando=false,arrasteDe=null;
cv.addEventListener("pointerdown",function(e){
  if(!posOn)return;
  e.preventDefault();
  try{cv.setPointerCapture(e.pointerId);}catch(x){}
  arrastando=true;arrasteDe=mmDe(e);
});
cv.addEventListener("pointermove",function(e){
  if(!posOn||!arrastando)return;
  const q=mmDe(e);
  T.tx+=q[0]-arrasteDe[0];T.ty+=q[1]-arrasteDe[1];
  arrasteDe=q;posContar();
});
/* Sem pointerleave: sair da area arrastando nao pode largar o desenho. */
["pointerup","pointercancel","lostpointercapture"].forEach(function(v){
  cv.addEventListener(v,function(){arrastando=false;});
});

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
  if(posOn)irAba("mesa");
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
  /* Viu ligado e agora nao esta: caiu por fora -- emergencia, o
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
              "registrador recusado","formato inesperado",
              "salto impossivel: leitura recusada"];
const ENC_AMOSTRAS=240;          /* uns 60 s a 4 Hz de consulta */
/* Amostra INTEIRA, nao so o erro: e o que a analise detalhada mostra e o
   que vai para o CSV. Guardar so o erro obrigaria a olhar duas telas
   para responder "o erro subiu porque o braco andou ou porque a leitura
   falhou?" -- que e a primeira pergunta de sempre. */
const encAmostras=[];
let encT0=0;
let encD=null, encCarregou=false;

const cvPos=$("cvPos"), ctPos=cvPos?cvPos.getContext("2d"):null;

/* ---------------------------------------------------------------------
   As duas rodinhas.

   Cada uma e a junta vista de cima. O ponteiro diz onde o encoder mede
   que o eixo esta -- e so isso. O ponteiro fino do comandado saiu: ele
   girava sozinho numa maquina em montagem e fazia o painel parecer
   travado.

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

  /* O PONTEIRO FINO AZUL SAIU.
     Ele mostrava o COMANDADO -- a contagem de passos do firmware. Numa
     maquina em montagem essa contagem anda sozinha, e o risquinho azul
     ficava girando sem parar em volta do mostrador: o painel parecia
     estar processando alguma coisa, ou travado. E o mesmo motivo pelo
     qual o numero comandado ja tinha saido da tela (ver ACHADOS R108) --
     faltava tirar o risco. Fica o ponteiro do que importa: onde a junta
     ESTA. */

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
function encMedir(){ medirTela(cvPos,ctPos); }
addEventListener("resize",encMedir);

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

  /* A analise deixou de ser gaveta propria e virou secao da mesma
     leitura. O tamanho da coleta continua tendo que aparecer: a tabela
     mostra so as ultimas 40, e sem esse numero nao da para saber se a
     estatistica acima veio de 40 leituras ou de 4000. */
  const total=encAmostras.length;
  $("sbAnal").textContent=total?("· "+total+" amostras"):"";

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
    if(L.ok){
      encCelula("eM"+(i+1),L.graus.toFixed(2)+"°");

    }else{
      /* Registrador 0 quer dizer "esta junta nao foi ligada ainda".
         Nao e falha: e o estado normal de quem so tem um driver na
         bancada, e chamar isso de falha assusta a toa. */
      const reg=(i===0)?d.reg1:d.reg2;
      encCelula("eM"+(i+1), !d.ativo?"desligado"
                          : !reg?"nao ligada"
                          : MOTIVO[L.motivo||1]);
      /* Sem leitura o historico continua andando com zero, senao o
         grafico mente dizendo que estava tudo bem no buraco. */

    }

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

  window.__encN=encAmostras.length;  /* o banco de interface confere */

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
  encMedir();posPintar();analisar(d);corrAplicar(d);zeroAplicar(d);
  rodaPintar(0,d);rodaPintar(1,d);
}

let encEmVoo = false, encDesde = 0;
function encAtualizar(){
  /* Mesma regra do estado: uma por vez, com o mesmo prazo de escape. O
     painel do encoder e a segunda consulta mais frequente da pagina, e
     duas filas crescendo somam. */
  const agoraEnc = Date.now();
  if(encEmVoo && (agoraEnc - encDesde) < 3000) return Promise.resolve();
  encEmVoo = true;
  encDesde = agoraEnc;
  return fetch("/api/encoder").then(function(r){return r.json();})
   .then(function(d){encEmVoo=false;return encAplicar(d);})
   .catch(function(e){
     encEmVoo=false;
     /* O catch existe para a REDE cair sem encher a tela de erro. Ele
        engolia tambem defeito de codigo dentro de encAplicar, e ai meia
        tela parava de atualizar sem nada explicando -- foi assim que uma
        variavel removida derrubou tabela, travamento e zero de uma vez.
        Falha de rede segue silenciosa; defeito vai para o console. */
     if(e instanceof TypeError && /fetch|network/i.test(e.message||"")) return;
     if(e) console.error("encAplicar:", e);
   });
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
   .then(function(){encCarregou=false;encAmostras.length=0;encT0=0;});
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
    encCarregou=false;encAmostras.length=0;encT0=0;});
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
    encAmostras.length=0;encT0=0;});
};

/* ---------- status ---------- */
const RM={MANUAL:"manual",GRAVANDO:"gravando",REPRODUZINDO:"repetindo",
 EXECUTANDO:"executa",POSICIONANDO:"movendo",CALIBRANDO:"calibra",FALHA:"falha"};
/* Quatro marcas, e acabou. Nao ha etapa de volta ao zero, nem numero a
   digitar: o curso, o zero (o meio do curso) e a escala do encoder saem
   todos das proprias marcas. */
/* Dois gestos do operador; o resto e da maquina. Os estados de viagem
   nao pedem nada: eles so contam o que esta acontecendo, para ninguem
   achar que a tela travou enquanto o braco anda. */
const PC={
 INDO_A:   [1,"A maquina esta levando o braco ao zero. Espere."],
 LADO_A:   [1,"Motores soltos. Empurre o braco ate o extremo de UM lado -- os "+
              "dois eixos de uma vez -- e toque em Guardar."],
 VOLTANDO: [2,"Extremo guardado. A maquina esta voltando ao zero."],
 LADO_B:   [2,"Motores soltos de novo. Agora o extremo do OUTRO lado."],
 CONCLUIDO:[2,"Medido. A maquina esta voltando ao zero para terminar."]};
/* Nas viagens quem anda e a maquina: o botao nao tem o que guardar. */
const PC_ESPERA={INDO_A:1,VOLTANDO:1,CONCLUIDO:1};

let quedas=0,ultN=-1,ultCal="";
/* Um botao fora de acao tem que dizer por que. Desabilitar em silencio e
   o que faz o operador achar que o sistema esta quebrado. */
function acao(id,motivo){
  const b=$("bt"+id), q=$("q"+id);
  if(b)b.disabled=!!motivo;
  if(q){
    q.textContent=motivo||"";
    q.style.display=motivo?"block":"none";
    /* O que esta funcao escreve nunca e ERRO: e pre-requisito -- "falta
       fazer isto antes". Saia em laranja como o resto do .pq2 e a tela
       de Programa vira uma coluna de avisos vermelhos com a maquina
       inteira em ordem. Marcado como pre-requisito, fica cinza, com um
       ponto na frente, e o laranja volta a significar so o que deu
       errado de verdade. */
    q.classList.add("pre");
  }
}
/* Motivo comum a tudo que move o braco, na ordem em que o operador
   precisa resolver. */
/* Motivo comum a tudo que move o braco, na ordem em que o operador
   precisa resolver. O jog NAO exige calibracao -- sem ela o robo esta em
   modo de instalacao, que existe justamente para o operador conseguir
   levar o braco ate os limites. */
function porQueNaoMove(d,exigeCalib){
  if(d.modo==="FALHA")            return "sistema em falha: rearme os servos primeiro";
  if(!d.servos)                   return "habilite os servos";
  if(exigeCalib&&(!d.cal1||!d.cal2))
    return "calibre as juntas";
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

/* =====================================================================
   A BARRA DE ESTADO

   Tres perguntas, nesta ordem, e todas respondidas sem rolar:

     1. Em que pe a maquina esta?  -> uma palavra, grande.
     2. Por que ela nao anda?      -> a razao, em frase.
     3. O que eu faco agora?       -> um botao, quando ha um passo obvio.

   Antes so a (2) existia, em cinza claro, corpo 12: a informacao mais
   valiosa da tela era a menos visivel dela. E a (3) nao existia em lugar
   nenhum -- quem chegava tinha de adivinhar por onde comecar.
   ===================================================================== */
const PROXIMO=[
  /* Ordem de precedencia: o primeiro que casar e o que aparece. Um passo
     de cada vez -- duas coisas a fazer ao mesmo tempo e nenhuma. */
  {quando:function(d){return d.modo==="FALHA";},
   rotulo:"Rearmar a maquina", faz:function(){post("/api/servos?v=1");}},
  {quando:function(d){return !d.srv1&&!d.srv2;},
   rotulo:"Habilitar os dois eixos", faz:function(){post("/api/servos?v=1");}},
  {quando:function(d){return !d.srv1||!d.srv2;},
   rotulo:"Habilitar o outro eixo", faz:function(){post("/api/servos?v=1");}},
  {quando:function(d){return !d.cal1&&!d.cal2;},
   rotulo:"Medir o curso do braco", faz:function(){post("/api/calib/iniciar");}},
  {quando:function(d){return (d.progN||0)<2;},
   rotulo:"Ensinar o caminho", faz:function(){irAba("prog");abrir(2);}}
];
let proximoAtual=null;

function pintarEstado(d,motivoErro,movendo){
  const falha  = (d.modo==="FALHA");
  const soldando = !!d.solda;
  const calib  = (d.calib!=="INATIVO");

  /* (1) O estado em UMA palavra. */
  let est="PARADA", classe="";
  if(falha)              { est="FALHA";     classe="er";  }
  else if(motivoErro)    { est="RECUSADO";  classe="er";  }
  else if(soldando)      { est="SOLDANDO";  classe="hot"; }
  else if(calib)         { est="MEDINDO";   classe="at";  }
  else if(d.modo==="EXECUTANDO")    { est="EXECUTANDO";  classe="at"; }
  else if(d.modo==="REPRODUZINDO")  { est="REPRODUZINDO";classe="at"; }
  else if(d.modo==="GRAVANDO")      { est="GRAVANDO";    classe="at"; }
  else if(movendo)       { est="MOVENDO";   classe="at";  }
  else if(d.srv1||d.srv2){ est="PRONTA";    classe="";    }
  $("teEst").textContent=tr(est);

  /* (2) O contexto curto, e a razao por extenso. */
  const eixos=(d.srv1&&d.srv2)?tr("os dois eixos com torque")
             :(d.srv1||d.srv2)?tr("um eixo com torque")
             :tr("sem torque");
  $("teSub").textContent=eixos;
  $("teMsg").textContent=motivoErro?(tr("Recusado")+": "+motivoErro):d.msg;
  $("tira").className="tira"+(classe?" "+classe:"");

  /* (3) O proximo passo. Some quando nao ha nenhum obvio -- botao que
     aparece sem ter o que fazer ensina a ignorar a barra. */
  const bt=$("teAcao");
  let achou=null;
  for(let i=0;i<PROXIMO.length&&!achou;i++)
    if(PROXIMO[i].quando(d))achou=PROXIMO[i];
  /* Enquanto a maquina anda sozinha, nao ha proximo passo do operador. */
  if(calib||movendo||soldando||d.modo==="EXECUTANDO")achou=null;
  proximoAtual=achou;
  /* So mexe no DOM quando o passo MUDOU. Reescrever display e texto a
     cada estado (4,5 vezes por segundo) refazia o layout da coluna
     inteira o tempo todo -- e e esse layout que carrega as setas de
     jog. */
  const rot=achou?tr(achou.rotulo):"";
  if(bt.dataset.rot!==rot){
    bt.dataset.rot=rot;
    bt.textContent=rot;
    bt.style.display=achou?"block":"none";
  }
}

function aplicar(d){
  Object.assign(D,d);
  if(!jaEnquadrou){jaEnquadrou=true;autoEnquadrar();}
  sonPintar(d);
  motorPintar(d);
  /* Posicao atual da junta selecionada -- e o numero que o operador
     confere antes de mandar o proximo comando. Diz tambem DE ONDE veio:
     medido pelo encoder ou comandado pelo firmware. */
  {
    const de=$("irDe");
    if(de){
      /* AQUI E A CONTA DO FIRMWARE, de proposito -- a mesma que
         /api/mover usa para calcular o destino. Mostrar o angulo do
         encoder faria a linha prometer "de -65,9 para 0" e a maquina
         andar outra distancia. Onde o braco esta de verdade se le na
         linha de cima ("medido") e na regua do rodape. */
      const v=(juntaSel===1)?d.t1:d.t2;
      de.textContent=(v||0).toFixed(1)+"\u00b0";
    }
  }
  /* A escala ensinada, se houver. Numero grande e o normal: um encoder
     de 17 bits com reducao 16 da milhares de contagens por grau. */
  const e1=$("escAtual");
  if(e1){
    const cg=[d.cg1,d.cg2];
    e1.textContent=[1,2].map(function(k){
      const v=cg[k-1];
      return "junta "+k+": "+((v===undefined||Math.abs(v)<0.0001)
        ? tr("nao ensinada (usa contagens por volta e reducao)")
        : (v.toFixed(2)+" "+tr("contagens por grau")));
    }).join("\n");
  }
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

  pintarEstado(d,erro,movendo);

  $("e1").classList.toggle("feita",pronto);
  $("sb1").textContent=pronto?("elos "+d.l1.toFixed(0)+"+"+d.l2.toFixed(0)+" mm · calibrado")
    :(d.servos?"falta calibrar":"servos desligados");

  const nq=pontos.filter(function(p,i){return i<pontos.length-1&&p.s;}).length;
  const nRuim=pontos.filter(function(p,i){return i<pontos.length-1&&p.av;}).length;
  $("e2").classList.toggle("feita",d.progN>=2&&!nRuim);
  $("sb2").textContent=d.progN===0?"nenhum ponto":
    (d.progN+" pontos · "+nq+" cordao(oes)"+(nRuim?" · "+nRuim+" trecho(s) com problema":""));
  /* A aba da mao livre GRAVA na mesma lista que o Programa executa: o
     contador e o mesmo numero, visto do lado de quem esta gravando. */
  $("sbMaoPt").textContent=d.progN===0?"nenhum ponto":(d.progN+" pontos");
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
  /* Virou um atalho ao lado de lento/normal/rapido: e um jeito de
     andar, como eles. Aceso quando ligado -- o estado esta no botao, nao
     numa palavra dentro dele. */
  $("btPrec").classList.toggle("on",!!d.precisao);
  $("btPrec").title=d.precisao?tr("precisao ligada"):tr("precisao desligada");

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
    if($("inVmn")&&document.activeElement!==$("inVmn"))$("inVmn").value=d.velMn;
    if($("inVmx")&&document.activeElement!==$("inVmx"))$("inVmx").value=d.velMx;
    $("inVc").value=d.velCordao;$("inVc2").value=d.velCordao;
    $("inA1").value=d.acel1;$("inA2").value=d.acel2;
    $("inSuav").value=d.suav;
    $("inPv1").value=d.ppv1;$("inRd1").value=d.red1;
    $("inPv2").value=d.ppv2;$("inRd2").value=d.red2;
    if($("inFv1"))$("inFv1").value=(d.fvel1!==undefined?d.fvel1:1);
    /* Nao reescrever o campo enquanto o dedo esta nele: a barra pulava de
       volta para o valor da maquina no meio do arrasto. */
    if($("velNiveis")&&d.velN!==undefined&&!velEnviando&&
       document.activeElement!==$("inVelMm")){
      velMostrar(d.velN);
    }
    if($("inFv2"))$("inFv2").value=(d.fvel2!==undefined?d.fvel2:1);
    $("inL1").value=d.l1;$("inL2").value=d.l2;$("inDb").value=d.dobra;
    $("inEy").value=d.envY;$("inEr").value=d.envR;
    $("inEsc").value=d.escala;

  }

  const veu=$("veu");
  if(d.calib==="INATIVO"){
    veu.classList.remove("on");
    if(ultCal&&ultCal!=="INATIVO")abrir(d.progN<2?2:3);
  }else{
    veu.classList.add("on");
    const p=PC[d.calib]||[0,""];
    $("cPasso").textContent="EXTREMO "+p[0]+" DE 2";
    $("cInstr").textContent=p[1];
    $("cBarra").style.width=(p[0]/2*100)+"%";
    /* Nas viagens quem anda e a maquina: o botao nao tem o que guardar,
       e um botao que existe sem fazer nada e pior que um ausente. */
    const espera=!!PC_ESPERA[d.calib];
    $("cOk").disabled=espera;
    $("cOk").textContent=espera?"a maquina esta andando…":"Guardar este extremo";
    /* Onde as DUAS juntas estao agora -- medidas, quando o encoder le. */
    const ondeJ=function(n){
      const med=(n===1)?d.m1:d.m2, tem=(n===1)?d.m1ok:d.m2ok;
      const cont=(n===1)?d.t1:d.t2;
      return "junta "+n+": "+(tem?med.toFixed(1):cont.toFixed(1))+"\u00b0";
    };
    $("cOnde").textContent=ondeJ(1)+"    "+ondeJ(2);
    /* A conferencia de sentido so na PRIMEIRA parada: da segunda em
       diante ja ha extremo guardado, e trocar o sinal do eixo inverteria
       o significado dele. */
    $("cSent").style.display=(d.calib==="LADO_A")?"block":"none";
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
      (d.trajMs/1000).toFixed(1)+" s · "+tr("reproduza abaixo ou salve em Arquivos");
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
       : !d.servos ? "habilite os servos" : "");
  acao("Mover", porQueNaoMove(d,true));
  acao("TrajLimpar", (d.trajN<2) ? "nao ha trajetoria para apagar"
       : (d.modo!=="MANUAL") ? "so com o robo parado no modo manual" : "");
  const bloqJog=porQueNaoMove(d);
  const instalacao=d.servos&&!(d.cal1&&d.cal2)&&d.modo!=="FALHA";
  joy.classList.toggle("bloq",!!bloqJog);
  /* Nem o subtitulo nem o rodape do joystick repetem o motivo: quem
     responde "por que nao anda" e a tarja de estado, logo acima, em
     corpo maior e com o botao do proximo passo. O joystick continua
     dizendo que esta bloqueado pelo proprio jeito -- apagado. */
  $("sbMover").textContent=instalacao?"modo de instalacao · jog livre":
    (d.precisao?"precisao ligada":"passo, angulo e referencia");
  if(d.maxPts>1&&d.maxPts!==MAX_PTS){MAX_PTS=d.maxPts;posContar();}
  if(arqAberta())sdEstadoSalvar();
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

let encPulos = 0;
let statusEmVoo = false, statusDesde = 0;
function tick(){
  /* O cartao so e consultado quando a aba de arquivos esta aberta: o
     WebServer atende uma conexao por vez e cada requisicao a mais
     concorre com o heartbeat do jog. */
  if(arqAberta())sdAtualizar(false);
  /* Consulta quando o painel do encoder esta NA TELA, nao quando a aba
     esta escolhida. No computador ele virou coluna fixa e nao ha mais
     aba "enc" para escolher -- amarrar a consulta a aba deixava a coluna
     sempre aberta mostrando dado do momento em que a pagina carregou. */
  /* Enquanto o operador esta movendo, o painel do encoder cede a vez. O
     WebServer do ESP32 atende UMA conexao por vez: status + encoder a
     cada 220 ms disputando com o heartbeat do jog de 100 ms atrasava o
     heartbeat, e jog sem heartbeat por 350 ms PARA o eixo -- travada de
     verdade, no motor, nao no desenho. */
  /* A leitura do encoder alimenta duas telas: o painel da aba Encoder e
     a pagina Encoder da configuracao (zero absoluto, assentamento,
     travamento). Consultar so quando o PAINEL esta a vista deixava a
     pagina de configuracao mostrando "--" para sempre. */
  const cfgEnc = $("veuCfg") && $("veuCfg").classList.contains("on") &&
                 $("cfgEncoder") && $("cfgEncoder").offsetParent;
  const precisaEnc = cfgEnc || ($("pnEnc") && $("pnEnc").offsetParent);
  /* Ceder a vez durante o jog e ECONOMIA, nao regra: se um temporizador
     de jog vazasse, "jogando" ficaria verdadeiro para sempre e o painel
     do encoder morreria em silencio, sem nada na tela explicando.
     Entao a cada quarta volta a consulta acontece de qualquer jeito. */
  encPulos = jogando() ? (encPulos + 1) : 0;
  if(precisaEnc && (!jogando() || encPulos >= 4)){
    if(encPulos >= 4) encPulos = 0;
    encAtualizar();
  }
  /* UMA consulta de estado por vez.

     O relogio dispara a cada 220 ms, mas o WebServer do ESP32 atende UMA
     conexao de cada vez. Quando a maquina engasga -- Wi-Fi ruim, um
     salvamento em memoria nao volatil, o barramento do encoder esperando
     um timeout -- as consultas nao esperam a anterior: elas se empilham,
     e a fila cresce enquanto a origem do atraso durar. Dali para a frente
     tudo chega tarde: o heartbeat do jog, o botao que se aperta, o
     proprio estado. E era isto que fazia a tela parecer travada e o
     movimento parecer cortado -- nao havia nada errado no motor.

     Pulando a volta quando a anterior nao voltou, a fila nunca passa de
     uma requisicao. */
  /* Com uma valvula de escape: fetch nao tem prazo proprio, e uma
     requisicao que nunca resolve deixaria a pagina muda para sempre --
     defeito pior que o que se esta consertando. Passado o prazo, a volta
     seguinte tenta de novo. */
  const agoraMs = Date.now();
  if(statusEmVoo && (agoraMs - statusDesde) < 3000) return;
  statusEmVoo = true;
  statusDesde = agoraMs;
  fetch("/api/status").then(function(r){return r.json();}).then(function(d){
    statusEmVoo=false;quedas=0;aplicar(d);
  }).catch(function(){
    statusEmVoo=false;
    quedas++;
    if(quedas>=2){
      lamp($("lRede"),"er");
      $("teEst").textContent="SEM REDE";
      $("teSub").textContent="";
      $("teMsg").textContent="Sem comunicacao com o robo. Movimento e arco "+
        "foram cortados por seguranca.";
      $("teAcao").style.display="none";
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
 /* velocidade */
 "na junta":"at the joint",
 "precisao":"fine","lento":"slow","normal":"normal","rapido":"fast",
 "precisao ligada":"fine mode on","precisao desligada":"fine mode off",
 "Velocidade":"Speed",
 "medido na ponta com o braco esticado":"measured at the tip, arm extended",
 /* lista do programa */
 "cordao":"weld","so desloca":"travel only","percurso":"path",
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
 "Declarar esta posicao como referencia":"Declare this position as reference",
 "Mudar a origem":"Change the origin",
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
 "abrir":"open","apagar":"delete","ver":"view",
 "programa":"program","trajetoria":"path",
 "Guardar o que esta na maquina":"Save what is in the machine",
 "No cartao":"On the card",
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
 "Travamentos":"Stalls",
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
 /* MAO LIVRE: uma mao levando a ponta, e o rastro que ela deixa. E a
    funcao de destaque da maquina -- fica antes do Programa porque e ali
    que o trabalho comeca: primeiro se ensina o caminho, depois se roda. */
 ["mao","Mao livre","M9 11V5.5a1.5 1.5 0 013 0V11m0-1.5a1.5 1.5 0 013 0V11m0-.5a1.5 1.5 0 013 0V15a6 6 0 01-6 6h-1a6 6 0 01-6-6v-3a1.5 1.5 0 013 0"],
 ["prog","Programa","M5 6h14M5 12h9M5 18h5M17 15l2 2 3-4"],
 ["enc","Encoder","M12 3a9 9 0 100 18 9 9 0 000-18zM12 12l5-3M12 12v-4"],
];
/* Paineis da tela de TRABALHO. Ajustes, configuracao do encoder e
   sistema sairam daqui: moram na gaveta da engrenagem, porque sao coisa
   de instalar uma vez, nao de usar no turno. */
/* Arquivos saiu daqui: virou gaveta de tela cheia, como a Configuracao.
   Guardar e abrir trabalho e uma biblioteca, e biblioteca quer largura --
   nao um terco de coluna ao lado do braco. */
const PANES={mover:"pnMover",mao:"pnMao",prog:"pnProg",enc:"pnEnc"};

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
  if(qual === "maquina"){ roteiroPintar(); }
  /* Em coluna larga a aba abre com TUDO a vista: sao poucos cartoes por
     assunto, eles cabem lado a lado, e assim ninguem precisa clicar para
     descobrir se o ajuste que procura existe. Fechar continua sendo um
     clique, para quem quer so um assunto na frente. */
  if(cfgEmColunas())
    $(validas[qual]).querySelectorAll(".et").forEach(function(x){
      if(x.querySelector(".cab .chv")) x.classList.add("aberta");});
  cfgColunas();
  try{localStorage.setItem("cfg", qual);}catch(e){}
}

/* Quantas colunas a aba aberta usa.

   Deixar so o `column-width` do CSS decidir dava uma coluna VAZIA: o
   navegador cria quantas couberem na largura e depois reparte o
   conteudo entre elas, entao tres cartoes numa tela larga viravam duas
   colunas cheias e um terco de tela em branco a direita. Contando os
   cartoes a gente pede exatamente as colunas que ha conteudo para
   encher, e o que sobra vira margem dos dois lados em vez de um buraco
   de um lado so. */
function cfgColunas(){
  const alvo={maquina:"cfgMaquina",calib:"cfgCalib",
              encoder:"cfgEncoder",sistema:"cfgSistema"}[cfgAtual];
  const pane=alvo&&$(alvo);
  if(!pane)return;
  if(!cfgEmColunas()){
    pane.style.columnCount="";pane.style.maxWidth="";pane.style.margin="";
    return;}
  let n=0;
  pane.childNodes.forEach(function(x){
    if(x.classList&&x.classList.contains("et"))n++;});
  /* Largura util = a do rolo menos o respiro lateral dele. Medir o
     proprio painel nao serve: ele ja carrega o teto que esta funcao
     acabou de por, e a conta se morderia. */
  const larg=pane.parentElement.clientWidth-44;
  const cabem=Math.max(1,Math.floor((larg+20)/430));
  const col=Math.max(1,Math.min(n||1,cabem));
  pane.style.columnCount=String(col);
  /* Sem o teto a coluna se estica para encher a tela e a linha vira um
     nome perdido na esquerda com o campo la na direita. Com ele o bloco
     fica centrado e cada linha continua legivel de ponta a ponta. */
  pane.style.maxWidth=(col*430)+"px";
  pane.style.margin="0 auto";
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
addEventListener("resize", function(){ medirCabecalho(); cfgColunas(); });
medirCabecalho();

/* =====================================================================
   O ROTEIRO DE INSTALACAO
   Cinco passos, na ordem em que se faz. Cada um diz se ja esta feito --
   lido do estado real da maquina, nao de um "ja marquei essa" guardado
   no navegador -- e leva ao cartao onde se faz.
   ===================================================================== */
const ROTEIRO=[
 {t:"Medidas do braco",
  q:function(){return null;},   /* nao da para saber: so mostra o valor */
  v:function(){return "elo 1 "+Math.round(D.l1||0)+" mm · elo 2 "+
                      Math.round(D.l2||0)+" mm";},
  ir:function(){irCfg("maquina");abrirEt($("e1"));}},
 {t:"Torque nos motores",
  q:function(){return !!(D.srv1&&D.srv2);},
  v:function(){return D.srv1&&D.srv2 ? "os dois eixos com torque"
             : (D.srv1||D.srv2) ? "so um eixo com torque"
             : "os dois soltos";},
  ir:function(){irCfg("maquina");abrirEt($("e1"));}},
 {t:"Calibrar o braco",
  q:function(){return !!(D.cal1&&D.cal2);},
  v:function(){return D.cal1&&D.cal2 ? "curso medido nas duas juntas"
             : (D.cal1||D.cal2) ? "so uma junta medida"
             : "nao medido — a maquina opera assim mesmo";},
  ir:function(){irCfg("calib");}},
 {t:"Area da mesa",
  q:function(){return !!D.mesaOn;},
  v:function(){return D.mesaOn ? "cantos ensinados" : "nao ensinada";},
  ir:function(){irCfg("calib");abrirEt($("btMesaCanto").closest(".et"));}},
 {t:"Zero absoluto",
  q:function(){return !!(D.zEn1&&D.zEn2);},
  v:function(){return (D.zEn1&&D.zEn2) ? "as duas juntas se localizam ao ligar"
             : (D.zEn1||D.zEn2) ? "so uma junta ensinada"
             : "a maquina nao sabe onde esta ao ligar";},
  ir:function(){irCfg("maquina");}}
];
function abrirEt(et){
  if(!et)return;
  const painel=et.closest(".pane")||document;
  painel.querySelectorAll(".et").forEach(function(x){
    if(x.querySelector(".cab .chv"))x.classList.remove("aberta");});
  et.classList.add("aberta");
  et.scrollIntoView({block:"start"});
}
let roteiroAssim="";
function roteiroPintar(){
  const cx=$("roteiro"); if(!cx)return;
  let feitos=0,total=0,h="";
  ROTEIRO.forEach(function(r,i){
    const ok=r.q();
    if(ok!==null){ total++; if(ok)feitos++; }
    h+='<div class="rtItem'+(ok?" ok":"")+'">'+
       '<div class="n">'+(ok?"\u2713":(i+1))+'</div>'+
       '<div class="tx"><div class="tt2">'+r.t+'</div>'+
       '<span class="st">'+r.v()+'</span></div>'+
       '<button class="mb" data-rt="'+i+'">abrir</button></div>';
  });
  /* Redesenhar de meio em meio segundo trocaria os botoes por outros
     iguais o tempo todo -- e um clique que cai entre a destruicao e a
     criacao se perde. So mexe no DOM quando alguma coisa mudou de
     verdade. */
  if(h===roteiroAssim){ $("sbRoteiro").textContent=feitos+" de "+total+" passos feitos"; return; }
  roteiroAssim=h;
  cx.innerHTML=h;
  traduzirDom(cx);
  cx.querySelectorAll("[data-rt]").forEach(function(b){
    b.onclick=function(){ROTEIRO[+b.dataset.rt].ir();};});
  $("sbRoteiro").textContent=feitos+" de "+total+" passos feitos";
}

/* =====================================================================
   PROCURAR UM AJUSTE
   Casa o que foi digitado com o TEXTO INTEIRO de cada cartao -- titulo,
   rotulos dos campos, notas. Procurar "aceleracao" tem que achar o
   cartao certo mesmo que a palavra so apareca no rotulo de um campo.
   ===================================================================== */
function cfgProcurar(){
  const alvo=$("cfgProcurar").value.trim().toLowerCase();
  $("cfgProcurarX").style.display=alvo?"inline-flex":"none";
  document.body.classList.toggle("cfgProcurando",!!alvo);
  const cartoes=document.querySelectorAll(".cfgRol .et");
  if(!alvo){
    cartoes.forEach(function(e){e.classList.remove("foraDaBusca");});
    irCfg(cfgAtual);
    return;
  }
  /* Sem acento e sem cedilha dos dois lados: quem procura no celular
     raramente acentua, e "calibracao" tem que achar "calibração". */
  const limpar=function(t){
    return t.toLowerCase().normalize("NFD").replace(/[\u0300-\u036f]/g,"");
  };
  const q=limpar(alvo);
  cartoes.forEach(function(e){
    const casa=limpar(e.textContent||"").indexOf(q)>=0;
    e.classList.toggle("foraDaBusca",!casa);
    if(casa&&e.querySelector(".cab .chv"))e.classList.add("aberta");
  });
  cfgColunas();
}
$("cfgProcurar").oninput=cfgProcurar;
$("cfgProcurarX").onclick=function(){
  $("cfgProcurar").value="";cfgProcurar();$("cfgProcurar").focus();};

function abrirCfg(){
  fecharArq();
  medirCabecalho();
  irCfg(cfgAtual);
  $("veuCfg").classList.add("on");
  $("btCfg").classList.add("on");
  /* So agora a tela tem largura: enquanto o veu estava escondido toda
     medida dava zero, e a conta das colunas caia sempre em uma. */
  cfgColunas();
}
function fecharCfg(){
  $("veuCfg").classList.remove("on");
  $("btCfg").classList.remove("on");
}

/* A COLUNA DE DIAGNOSTICO NASCE FECHADA.
   Ela e nivel 3 -- rodinhas, grafico e quinze numeros de manutencao --
   e estava ocupando um terco da tela de operacao, na frente de quem
   nunca viu a maquina. Quem precisa dela abre num toque; a escolha fica
   gravada no navegador, entao quem usa todo dia abre uma vez so. */
function mostrarEnc(v){
  document.body.classList.toggle("comEnc",v);
  $("btEnc").classList.toggle("on",v);
  try{localStorage.setItem("enc",v?"1":"0");}catch(e){}
  /* A coluna aparecendo muda a largura do desenho: remedir na hora evita
     um quadro esticado ate o proximo redimensionamento. */
  setTimeout(function(){medir();encMedir();},60);
}
$("btEnc").onclick=function(){
  mostrarEnc(!document.body.classList.contains("comEnc"));
};
try{ if(localStorage.getItem("enc")==="1") mostrarEnc(true); }catch(e){}

/* ARQUIVOS: gaveta de tela cheia, no molde da Configuracao.
   Era um terco de coluna ao lado do braco, e ali uma lista de trabalhos
   nunca cabia. Guardar e abrir trabalho e uma biblioteca -- biblioteca
   quer largura, e nao se olha para o braco enquanto se escolhe arquivo.
   Duas gavetas com a mesma forma sao uma coisa so de aprender: mesmo
   lugar do titulo, mesmo fechar, mesmo Esc, mesmo toque fora. */
function arqAberta(){ return $("veuArq").classList.contains("on"); }
function abrirArq(){
  fecharCfg();
  medirCabecalho();
  $("veuArq").classList.add("on");
  $("btArq").classList.add("on");
  sdAtualizar(true);
}
function fecharArq(){
  $("veuArq").classList.remove("on");
  $("btArq").classList.remove("on");
}
$("btArq").onclick   = function(){ if(arqAberta()) fecharArq(); else abrirArq(); };
$("arqFechar").onclick = fecharArq;
$("veuArq").addEventListener("click", function(e){
  if(e.target === $("veuArq")) fecharArq();
});

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
/* Esc fecha a gaveta que estiver aberta -- as duas se comportam igual. */
addEventListener("keydown", function(e){
  if(e.key !== "Escape") return;
  if($("veuCfg").classList.contains("on")) fecharCfg();
  else if(arqAberta()) fecharArq();
});

/* MARCAR PONTO PELO TECLADO.
   Com o braco solto, as duas maos estao nele -- alcancar o mouse para
   clicar "marcar" e justamente o gesto que move a ponta que se acabou de
   posicionar. Uma tecla resolve, e o teclado sem fio fica no carrinho.

   So com a aba da mao livre aberta, e so fora de campo de digitacao:
   tecla que grava ponto de qualquer lugar da tela gravaria ponto no meio
   de alguem preenchendo um numero. */
addEventListener("keydown", function(e){
  if(e.key !== "g" && e.key !== "G") return;
  if(e.ctrlKey || e.altKey || e.metaKey) return;
  if(abaAtual !== "mao") return;
  if($("veuCfg").classList.contains("on") || arqAberta()) return;
  const a = document.activeElement;
  if(a && (a.tagName === "INPUT" || a.tagName === "TEXTAREA" ||
           a.isContentEditable)) return;
  e.preventDefault();
  post("/api/ponto/gravar","qAprMarcar").then(lerPontos);
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
  ajudaPintar();
  try{localStorage.setItem("aba",nome);}catch(e){}
}

/* Uma frase por aba: o que ela e, e o primeiro passo. Escrita para quem
   nunca viu a maquina -- sem jargao de firmware, sem nome de registrador
   e sem mandar ler outra tela. */
const AJUDA={
 mesa:["A mesa vista de cima",
   "O desenho mostra onde o braco esta. Arraste para girar a vista e toque "+
   "num elo para escolher aquele eixo. Tocar na mesa nao move o braco."],
 mover:["Mover o braco na mao",
   "Use o joystick ou as setas de cada junta. A velocidade em mm/s e a da "+
   "PONTA. Se nada andar, a barra cinza acima diz o porque."],
 mao:["Ensinar o caminho com a mao",
   "Solte o braco, leve a ponta e marque cada ponto -- entre eles a ponta anda "+
   "em reta. Para curva, use o modo continuo: segure e mova. Os pontos vao "+
   "para o Programa, e e la que eles rodam."],
 prog:["Ensinar o caminho",
   "Leve o braco ate um lugar bom e grave o ponto. A lista vira o programa; "+
   "so depois de gravada e que ela roda."],
 enc:["Conferencia do encoder",
   "Compara o angulo que a maquina COMANDOU com o que o encoder MEDIU. "+
   "Serve para diagnostico; nao e preciso mexer aqui para operar."]
};
function ajudaPintar(){
  const c=$("ajudaAba"); if(!c)return;
  const on=$("btAjuda")&&$("btAjuda").classList.contains("on");
  const a=AJUDA[abaAtual];
  c.hidden=!(on&&a);
  if(on&&a){c.innerHTML="<b></b><span></span>";
    c.querySelector("b").textContent=a[0];
    c.querySelector("span").textContent=a[1];
    if(typeof traduzirDom==="function")traduzirDom(c);}
}
(function(){
  const b=$("btAjuda"); if(!b)return;
  const por=function(v){
    b.classList.toggle("on",v);
    b.title=v?"Esconder a ajuda desta aba":"O que faco nesta aba?";
    try{localStorage.setItem("ajudaAba",v?"1":"0");}catch(e){}
    ajudaPintar();
  };
  b.onclick=function(){por(!b.classList.contains("on"));};
  /* Nasce FECHADA. Aberta, ela ocupa quase cem pixels bem em cima das
     setas de jog -- e era o que obrigava o painel a rolar. Quem esta
     comecando abre no "?" e a escolha fica gravada no navegador. */
  let g="0";
  try{g=localStorage.getItem("ajudaAba")||"0";}catch(e){}
  por(g==="1");
})();

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
  prog:{arqs:[],rot:"programa",   ver:true},
  traj:{arqs:[],rot:"trajetoria", ver:false}
};
/* O que o Salvar vai gravar. So vira uma PERGUNTA na tela quando a
   maquina tem as duas coisas ao mesmo tempo -- que e raro. No resto do
   tempo a escolha se faz sozinha e o operador nem ve que existia. */
let tipoGuardar="prog";
$("btSdMontar").onclick=function(){post("/api/sd/montar").then(function(){sdSeq=-1;});};

function sdQuanto(tipo){ return (tipo==="prog"?(D.progN||0):(D.trajN||0)); }
function sdTem(tipo){ return sdQuanto(tipo)>=2; }

$("segGuardar").querySelectorAll("[data-t]").forEach(function(b){
  b.onclick=function(){ tipoGuardar=b.dataset.t; sdEstadoSalvar(); };
});
$("sdNome").oninput=function(){sdEstadoSalvar();};
$("btSdSalvar").onclick=function(){
  const n=$("sdNome").value.trim();
  if(!n){acao("SdSalvar","informe um nome para o arquivo");return;}
  post("/api/sd/salvar?tipo="+tipoGuardar+"&nome="+encodeURIComponent(n))
   .then(function(){sdSeq=-1;});
};

/* O que "Salvar" vai gravar, e por que ele nao pode agora.
   Antes o botao respondia 200 sempre: o firmware enfileirava o pedido e
   a recusa ("nada para salvar", "cartao ausente") aparecia so na tira de
   mensagem, que rola. O operador apertava e concluia que nao funcionava. */
function sdEstadoSalvar(){
  const temP=sdTem("prog"), temT=sdTem("traj");
  /* A maquina so tem uma das duas coisas: nao ha o que escolher. E sem
     nenhuma das duas o texto volta a falar de programa, que e o que a
     pessoa quase sempre esta tentando guardar -- dizer "nao ha
     trajetoria" a quem acabou de desenhar uma peca so confunde. */
  if(temP&&!temT)tipoGuardar="prog";
  if(temT&&!temP)tipoGuardar="traj";
  if(!temP&&!temT)tipoGuardar="prog";
  const dois=temP&&temT;
  $("segGuardar").style.display=dois?"flex":"none";
  $("segGuardar").querySelectorAll("[data-t]").forEach(function(b){
    b.classList.toggle("on",b.dataset.t===tipoGuardar);});

  const nome=$("sdNome").value.trim();
  const quanto=sdQuanto(tipoGuardar);
  $("sdOque").textContent =
      (tipoGuardar==="prog")
    ? (temP ? "vai gravar o programa que esta na maquina: "+quanto+" pontos"
            : "nao ha programa na maquina. Desenhe na mesa, importe um DXF ou grave pontos na aba Mover")
    : (temT ? "vai gravar a trajetoria na memoria: "+quanto+" amostras"
            : "nao ha trajetoria gravada. Use \"Trajetoria a mao livre\" na aba Programa");
  acao("SdSalvar",
      sdEstado==="DESLIGADO" ? "o cartao nao foi iniciado"
    : sdEstado==="SEM_CARTAO" ? "nenhum cartao no slot"
    : sdEstado==="OCUPADO" ? "o cartao esta ocupado, aguarde"
    : (D.modo&&D.modo!=="MANUAL") ? "salve com o robo parado no modo manual"
    : (quanto<2) ? (tipoGuardar==="prog" ? "nao ha programa na maquina para salvar"
                                         : "nao ha trajetoria gravada para salvar")
    : !nome ? "de um nome ao arquivo"
    : /[^A-Za-z0-9 _-]/.test(nome) ? "use so letras, numeros, espaco, hifen e sublinhado"
    : "");
}

/* UMA lista com os dois tipos dentro, cada linha com a sua etiqueta.
   Programas primeiro: e o que se abre no dia a dia. */
function sdPintar(){
  const cx=$("sdLista");
  const tudo=[];
  Object.keys(BIB).forEach(function(tipo){
    BIB[tipo].arqs.forEach(function(a){
      tudo.push({tipo:tipo,n:a.n,b:a.b});});
  });
  if(!tudo.length){
    cx.innerHTML='<div class="nulo">Nenhum arquivo salvo ainda.</div>';return;}
  let h='<div class="lista arqs">';
  tudo.forEach(function(a){
    const b=BIB[a.tipo];
    h+='<div class="arq" data-tipo="'+a.tipo+'">'+
       '<div class="nm">'+a.n+'</div>'+
       '<div class="tag">'+b.rot+'</div>'+
       '<div class="kb">'+(a.b<1024?a.b+" B":(a.b/1024).toFixed(1)+" kB")+'</div>'+
       (b.ver?'<button class="mb" data-ver="'+a.n+'">ver</button>':'')+
       '<button class="mb" data-car="'+a.n+'" data-t="'+a.tipo+'">abrir</button>'+
       '<button class="mb x" data-apg="'+a.n+'" data-t="'+a.tipo+'">apagar</button></div>';
  });
  cx.innerHTML=h+'</div>';
  traduzirDom(cx);
  cx.querySelectorAll("[data-ver]").forEach(function(e){e.onclick=function(){
    verPeca(e.dataset.ver);};});
  cx.querySelectorAll("[data-car]").forEach(function(e){e.onclick=function(){
    post("/api/sd/carregar?tipo="+e.dataset.t+"&nome="+encodeURIComponent(e.dataset.car))
     .then(function(){sdSeq=-1;});};});
  cx.querySelectorAll("[data-apg]").forEach(function(e){e.onclick=function(){
    if(!confirm('Apagar "'+e.dataset.apg+'" do cartao?'))return;
    post("/api/sd/apagar?tipo="+e.dataset.t+"&nome="+encodeURIComponent(e.dataset.apg))
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
       sdPintar();
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

/* SEGURE PARA GRAVAR.
   Mesma captura de ponteiro das setas de jog, e pelo mesmo motivo: o
   botao muda de tamanho quando a barra de estado ganha uma linha, e sem
   captura isso tirava o botao de baixo do dedo e encerrava a gravacao no
   meio do percurso. Com captura, o pointerup chega sempre -- e a gravacao
   acaba onde a mao acaba. */
(function(){
  const b=$("btGravSeg");
  let idAtivo=null, gravando=false;
  b.addEventListener("pointerdown",function(e){
    e.preventDefault();
    idAtivo=e.pointerId;
    try{b.setPointerCapture(e.pointerId);}catch(x){}
    gravando=true;
    b.classList.add("on");
    post("/api/gravar/iniciar","qGravSeg");
  });
  ["pointerup","pointercancel","lostpointercapture"].forEach(function(v){
    b.addEventListener(v,function(e){
      if(idAtivo!==null&&e.pointerId!==undefined&&e.pointerId!==idAtivo)return;
      idAtivo=null;
      if(!gravando)return;
      gravando=false;
      b.classList.remove("on");
      post("/api/gravar/parar","qGravSeg");
    });});
})();
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
/* Os tres botoes de "levar" viraram um so, ligado a junta selecionada:
   o alvo e uma linha, e a junta se escolhe no desenho ou no seletor.
   Ver btMoverSel, junto do passo a passo. */
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
   acima, metade do que a troca de vista mexe ainda nao existe, e
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
