// Roda a interface num Chromium de verdade contra o servidor falso.
// Verifica que ela carrega sem erro de JS, que as abas trocam, que o
// joystick manda /api/jogxy com a intensidade certa e tira as fotos.
const { chromium } = require('/opt/node22/lib/node_modules/playwright');

const BASE = 'http://127.0.0.1:8099';
const SAIDA = 'testes/saida/ui';

let passa = 0, falha = 0;
function checar(ok, texto, extra) {
  if (ok) { passa++; console.log('  \x1b[32mPASSA\x1b[0m    ' + texto); }
  else    { falha++; console.log('  \x1b[31mFALHA\x1b[0m    ' + texto); }
  if (extra) console.log('           \x1b[2m' + extra + '\x1b[0m');
}

// ---------------------------------------------------------------------
// Gaveta de configuracao (a engrenagem). Os ajudantes sao IDEMPOTENTES
// de proposito: `abrirGaveta` numa gaveta ja aberta nao pode fecha-la.
// O botao e um alternador, e um teste que dependa de "quantas vezes ja
// clicaram" quebra na primeira reordenacao.
// ---------------------------------------------------------------------
async function abrirGaveta(pag, alvo) {
  if (!(await pag.locator('#veuCfg.on').count())) {
    await pag.locator('#btCfg').click();
    await pag.waitForTimeout(220);
  }
  if (alvo) {
    await pag.locator('#cfgAbas button[data-cfg="' + alvo + '"]').click();
    await pag.waitForTimeout(250);
  }
}
async function fecharGaveta(pag) {
  if (await pag.locator('#veuCfg.on').count()) {
    await pag.locator('#cfgFechar').click();
    await pag.waitForTimeout(220);
  }
}

(async () => {
  const browser = await chromium.launch();

  // ---------------- CELULAR ----------------
  console.log('\n\x1b[1m== Interface no celular (390x844, toque) ==\x1b[0m');
  const cel = await browser.newContext({
    viewport: { width: 390, height: 844 },
    deviceScaleFactor: 2, isMobile: true, hasTouch: true,
  });
  const p = await cel.newPage();

  const erros = [];
  p.on('pageerror', e => erros.push(String(e)));
  p.on('console', m => { if (m.type() === 'error') erros.push(m.text()); });

  const chamadas = [];
  p.on('request', r => { const u = new URL(r.url()); if (u.pathname.startsWith('/api')) chamadas.push(u.pathname + u.search); });

  await p.goto(BASE, { waitUntil: 'domcontentloaded' });
  await p.waitForTimeout(600);

  checar(erros.length === 0, 'a pagina carrega sem erro de JavaScript',
         erros.length ? erros.slice(0, 3).join(' | ') : 'nenhum erro no console');

  // A pagina chega comprimida, como o ESP32 manda. O navegador tem de
  // descomprimir sozinho -- se nao descomprimisse, nada acima teria
  // funcionado, mas vale conferir o cabecalho e o tamanho na rede.
  const resp = await p.request.get(BASE + '/');
  const cab = resp.headers();
  const bytesRede = parseInt(cab['content-length'] || '0', 10);
  // O teto nao e estetico: a pagina inteira vai comprimida na flash e sai
  // pelo Wi-Fi proprio do robo, que e lento.
  //
  // 80 KB nao e chute: no ponto de acesso do ESP32 a transferencia fica
  // na casa de 1 a 2 Mbit/s efetivos, entao 80 KB sao entre 0,3 e 0,6 s
  // -- o operador nao percebe. Na flash sao 80 KB, e mesmo no esquema de
  // particao com OTA (1,9 MB por imagem) isso nao aperta.
  //
  // O teto existe para o crescimento ser uma DECISAO e nao um acidente.
  // Ja foi 48 KB, depois 64 KB, agora 80 KB. O que subiu desta vez, e por
  // que valeu:
  //
  //   - gerador de QR (~7,5 KB de fonte): substitui uma biblioteca
  //     externa que nao existe aqui -- a maquina nao tem internet, entao
  //     CDN nao e opcao. Conferido contra um decodificador de verdade
  //     (testes/conferir_qr.py).
  //   - aba Maquina inteira: saude, registro de eventos, os dois QR de
  //     conexao, envio de firmware e modo operador.
  //   - miniatura de peca do cartao, com o aviso de elos diferentes.
  //   - dicionario de ingles da superficie do operador.
  //   - controles de producao: pausar, retomar, repetir, desfazer.
  //
  // Nenhum deles e enfeite: sao o que separa uma bancada de um
  // equipamento que faz a centesima peca. Quando ESTE teto estourar, olhe
  // de novo antes de subir.
  checar(cab['content-encoding'] === 'gzip' && bytesRede > 0 && bytesRede < 80000,
         'a pagina e servida comprimida, como o firmware faz',
         'Content-Encoding: ' + cab['content-encoding'] + ', ' + bytesRede +
         ' bytes na rede');

  // Barra de abas presente e com as cinco abas
  const nAbas = await p.locator('#abas button').count();
  checar(nAbas === 5, 'barra de abas inferior com 5 abas de trabalho',
         nAbas + ' abas encontradas');

  // Aba inicial = Mover, com o joystick visivel
  const joyVis = await p.locator('#joy').isVisible();
  checar(joyVis, 'abre na aba Mover com o joystick visivel');
  await p.screenshot({ path: SAIDA + '/celular-1-mover.png' });

  // ---- joystick ----
  const cx = await p.locator('#joy').boundingBox();
  const meioX = cx.x + cx.width / 2, meioY = cx.y + cx.height / 2;
  const raio = cx.width / 2;

  chamadas.length = 0;
  // Arrasta para a direita e um pouco para cima: J1 forte, J2 fraco.
  await p.mouse.move(meioX, meioY);
  await p.mouse.down();
  await p.mouse.move(meioX + raio * 0.8, meioY - raio * 0.3, { steps: 8 });
  await p.waitForTimeout(250);
  const durante = chamadas.filter(c => c.startsWith('/api/jogxy'));
  const ultimo = durante[durante.length - 1] || '';
  const m = ultimo.match(/a=(-?[\d.]+)&b=(-?[\d.]+)/);
  const a = m ? parseFloat(m[1]) : 0, b = m ? parseFloat(m[2]) : 0;
  checar(durante.length >= 2 && a > 0.6 && a < 0.95 && b > 0.15 && b < 0.45,
         'o joystick manda /api/jogxy proporcional aos dois eixos',
         durante.length + ' envios; ultimo a=' + a + ' b=' + b);
  await p.screenshot({ path: SAIDA + '/celular-2-joystick.png' });

  // O rotulo acompanha
  const txtA = await p.locator('#joyA').textContent();
  checar(/\d+%/.test(txtA), 'o painel mostra a intensidade por eixo', 'J1 = ' + txtA);

  // O botao tem que ACOMPANHAR o dedo: com a=0.8 ele fica perto da borda.
  const desl = await p.evaluate(() => {
    const j = document.getElementById('joy').getBoundingClientRect();
    const k = document.getElementById('joyKnob').getBoundingClientRect();
    const raio = j.width / 2;
    return {
      dx: ((k.x + k.width / 2) - (j.x + j.width / 2)) / raio,
      dy: ((j.y + j.height / 2) - (k.y + k.height / 2)) / raio,
    };
  });
  checar(desl.dx > 0.55 && desl.dy > 0.15,
         'o botao do joystick acompanha o dedo na proporcao certa',
         'centro do botao a ' + desl.dx.toFixed(2) + ' / ' + desl.dy.toFixed(2) +
         ' do raio (comando 0.80 / 0.30)');

  chamadas.length = 0;
  await p.mouse.up();
  await p.waitForTimeout(200);
  const zeros = chamadas.filter(c => /jogxy\?a=0\.000&b=0\.000/.test(c));
  checar(zeros.length >= 1, 'soltar o dedo manda o zero imediatamente',
         zeros.length + ' comando(s) de parada');

  // Arrastar ALEM da borda do disco: satura em 1, nao derruba o comando.
  chamadas.length = 0;
  await p.mouse.move(meioX, meioY); await p.mouse.down();
  await p.mouse.move(meioX + raio * 2.5, meioY - raio * 2.5, { steps: 10 });
  await p.waitForTimeout(250);
  const fora = chamadas.filter(c => c.startsWith('/api/jogxy'));
  const ult = fora[fora.length - 1] || '';
  const mf = ult.match(/a=(-?[\d.]+)&b=(-?[\d.]+)/);
  const af = mf ? parseFloat(mf[1]) : 0, bf = mf ? parseFloat(mf[2]) : 0;
  const seguiuVivo = af > 0.5 && bf > 0.5;
  checar(seguiuVivo, 'arrastar para fora do disco satura em vez de soltar o jog',
         'ultimo comando a=' + af + ' b=' + bf + ' (modulo ' + Math.hypot(af, bf).toFixed(2) + ')');
  await p.mouse.up();
  await p.waitForTimeout(150);

  // Tela apagando para o jog
  await p.mouse.move(meioX, meioY); await p.mouse.down();
  await p.mouse.move(meioX + raio * 0.7, meioY, { steps: 4 });
  await p.waitForTimeout(150);
  chamadas.length = 0;
  await p.evaluate(() => { Object.defineProperty(document, 'hidden', { value: true, configurable: true }); document.dispatchEvent(new Event('visibilitychange')); });
  await p.waitForTimeout(200);
  const parouEscondido = chamadas.some(c => /jogxy\?a=0\.000&b=0\.000/.test(c));
  checar(parouEscondido, 'app indo para segundo plano para o jog na hora');
  await p.mouse.up();

  // ---- abas de trabalho ----
  for (const [aba, alvo, nome] of [
    ['prog',   '#e2',      'Programa'],
    ['arq',    '#sdBar',   'Arquivos'],
    ['enc',    '#eM1',     'Encoder'],
    ['mesa',   '#cv',      'Mesa'],
  ]) {
    await p.locator('#abas button[data-aba="' + aba + '"]').click();
    await p.waitForTimeout(350);
    const vis = await p.locator(alvo).isVisible();
    checar(vis, 'aba ' + nome + ' mostra o seu conteudo (' + alvo + ')');
    await p.screenshot({ path: SAIDA + '/celular-3-' + aba + '.png' });
  }

  // ---- a gaveta da engrenagem ----
  // Os ajustes sairam da barra de abas. A barra ficou so com o que se usa
  // no turno; instalacao mora atras da engrenagem.
  await abrirGaveta(p);
  const gaveta = await p.evaluate(() => ({
    aberta: document.getElementById('veuCfg').classList.contains('on'),
    abas: [...document.querySelectorAll('#cfgAbas button')].map(b => b.textContent.trim()),
  }));
  checar(gaveta.aberta, 'a engrenagem abre a gaveta de configuracao');
  checar(gaveta.abas.length === 4, 'com as quatro paginas de ajuste',
         gaveta.abas.join(' | '));
  for (const [cfg, alvo, nome] of [
    ['maquina', '#e1',        'Maquina'],
    ['calib',   '#calResumo', 'Calibracao'],
    ['encoder', '#sbCorr',    'Encoder'],
    ['sistema', '#saudeG',    'Sistema'],
  ]) {
    await p.locator('#cfgAbas button[data-cfg="' + cfg + '"]').click();
    await p.waitForTimeout(300);
    const vis = await p.locator(alvo).isVisible();
    checar(vis, 'gaveta: pagina ' + nome + ' mostra o seu conteudo (' + alvo + ')');
  }
  await p.screenshot({ path: SAIDA + '/celular-3-gaveta.png' });

  // Fechar pelo Esc, que e o gesto que todo mundo tenta.
  await p.keyboard.press('Escape');
  await p.waitForTimeout(300);
  const fechou = await p.evaluate(() =>
    !document.getElementById('veuCfg').classList.contains('on'));
  checar(fechou, 'e o Esc fecha a gaveta');

  // O botao de PARAR fica FORA da gaveta e continua alcancavel com ela
  // aberta: emergencia que depende de fechar uma janela nao e emergencia.
  await abrirGaveta(p);
  // "Visivel" nao basta: um veu por cima deixa o botao visivel e morto.
  // A pergunta certa e QUEM esta no ponto onde o dedo vai encostar.
  const pararClicavel = await p.evaluate(() => {
    const b = document.getElementById('btParar');
    const r = b.getBoundingClientRect();
    const alvo = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
    return !!alvo && (alvo === b || b.contains(alvo));
  });
  checar(pararClicavel,
         'o botao PARAR continua CLICAVEL com a gaveta aberta, nao so visivel');
  await fecharGaveta(p);

  // A mesa de tracado desenha depois de trocar de aba
  const dim = await p.evaluate(() => { const c = document.getElementById('cv'); return [c.width, c.height]; });
  checar(dim[0] > 100 && dim[1] > 100, 'o canvas e redimensionado ao abrir a aba Mesa',
         dim[0] + 'x' + dim[1] + ' px');

  // Arquivos: UMA biblioteca. Havia dois cartoes lado a lado, cada um
  // com o seu campo de nome, o seu Salvar e a sua lista -- e para usar
  // era preciso saber ANTES em qual das duas palavras (programa ou
  // trajetoria) o que voce acabou de fazer se encaixa.
  await p.locator('#abas button[data-aba="arq"]').click();
  await p.waitForTimeout(700);
  await p.evaluate(() => {
    document.querySelectorAll('#pnArq .et').forEach(x => x.classList.add('aberta'));
  });
  await p.waitForTimeout(400);
  const bib = await p.evaluate(() => ({
    linhas: document.querySelectorAll('#sdLista .arq').length,
    prog: document.querySelectorAll('#sdLista .arq[data-tipo="prog"]').length,
    traj: document.querySelectorAll('#sdLista .arq[data-tipo="traj"]').length,
    etiquetas: [...document.querySelectorAll('#sdLista .arq .tag')]
                 .map(e => e.textContent.trim()),
    duasListas: !!document.getElementById('sdListaProg') ||
                !!document.getElementById('sdListaTraj'),
    doisNomes: !!document.getElementById('sdNomeProg') ||
               !!document.getElementById('sdNomeTraj'),
    seletor: !!document.getElementById('segTipo'),
    comoUsado: [...document.querySelectorAll('#pnArq .tt')]
                 .some(e => /Como o cartao e usado/.test(e.textContent)),
    titulo: document.getElementById('sdTit').textContent,
  }));
  checar(bib.linhas === 3 && bib.prog === 2 && bib.traj === 1,
         'Arquivos: uma lista so, com os dois tipos dentro',
         bib.prog + ' programa(s) e ' + bib.traj + ' trajetoria(s) em '
         + bib.linhas + ' linhas; "' + bib.titulo + '"');
  checar(bib.etiquetas.includes('programa') && bib.etiquetas.includes('trajetoria'),
         'Arquivos: o tipo vira etiqueta na linha, depois de salvo',
         bib.etiquetas.join(' '));
  checar(!bib.duasListas && !bib.doisNomes,
         'Arquivos: nao sobrou nenhuma das duas bibliotecas antigas');
  checar(!bib.seletor,
         'Arquivos: o seletor de tres posicoes saiu -- nao ha mais "qual lista e esta?"');
  checar(!bib.comoUsado,
         'Arquivos: o texto "Como o cartao e usado" saiu; era so a arvore de pastas');

  // "Nao consegui salvar o desenho no cartao": o botao respondia 200 e a
  // recusa ia so para a tira de mensagem. Agora ele diz o que falta.
  await p.locator('#sdNome').fill('peca teste');
  await p.waitForTimeout(300);
  const salvarOk = await p.evaluate(() => ({
    dis: document.getElementById('btSdSalvar').disabled,
    oque: document.getElementById('sdOque').textContent.trim(),
    escolha: getComputedStyle(document.getElementById('segGuardar')).display,
  }));
  checar(!salvarOk.dis && /3 pontos/.test(salvarOk.oque),
         'Arquivos: com programa na maquina, Salvar libera e diz o que vai gravar',
         salvarOk.oque);
  // A maquina de teste tem as DUAS coisas: e o unico caso em que a
  // escolha de tipo aparece na tela.
  checar(salvarOk.escolha !== 'none',
         'Arquivos: com as duas coisas na maquina, a escolha aparece',
         salvarOk.escolha);
  await p.locator('#segGuardar [data-t="traj"]').click();
  await p.waitForTimeout(200);
  const trocouTipo = await p.evaluate(() =>
    document.getElementById('sdOque').textContent.trim());
  checar(/amostras/.test(trocouTipo),
         'Arquivos: e escolher a trajetoria muda o que vai ser gravado', trocouTipo);
  await p.locator('#segGuardar [data-t="prog"]').click();
  await p.waitForTimeout(200);

  // Tirando a trajetoria, sobra so o programa -- e a escolha some, porque
  // deixou de ser uma escolha.
  await p.request.post(BASE + '/teste/estado', { data: { trajN: 0 } });
  await p.waitForTimeout(700);
  const soProg = await p.evaluate(() => ({
    escolha: getComputedStyle(document.getElementById('segGuardar')).display,
    oque: document.getElementById('sdOque').textContent.trim(),
  }));
  checar(soProg.escolha === 'none' && /3 pontos/.test(soProg.oque),
         'Arquivos: so ha programa para guardar, entao nao ha o que escolher',
         soProg.escolha + ' · ' + soProg.oque);

  await p.request.post(BASE + '/teste/estado', { data: { progN: 0 } });
  await p.waitForTimeout(700);
  // Sem programa, sobrou a trajetoria: a escolha se faz sozinha e o
  // Salvar passa a falar de trajetoria, nao de programa.
  await p.request.post(BASE + '/teste/estado', { data: { trajN: 24 } });
  await p.waitForTimeout(700);
  const semProg = await p.evaluate(() => ({
    dis: document.getElementById('btSdSalvar').disabled,
    motivo: document.getElementById('qSdSalvar').textContent.trim(),
    oque: document.getElementById('sdOque').textContent.trim(),
    escolha: getComputedStyle(document.getElementById('segGuardar')).display,
  }));
  checar(/trajetoria/.test(semProg.oque) && semProg.escolha === 'none',
         'Arquivos: sem programa, o Salvar vira o da trajetoria sem perguntar',
         semProg.oque);

  // E sem nenhuma das duas ele trava, dizendo o porque e de onde vem um
  // programa -- antes ele respondia 200 e a recusa ia so para a tira.
  await p.request.post(BASE + '/teste/estado', { data: { trajN: 0 } });
  await p.waitForTimeout(700);
  const semNada = await p.evaluate(() => ({
    dis: document.getElementById('btSdSalvar').disabled,
    motivo: document.getElementById('qSdSalvar').textContent.trim(),
    oque: document.getElementById('sdOque').textContent.trim(),
  }));
  checar(semNada.dis && semNada.motivo.length > 0,
         'Arquivos: sem nada na maquina, Salvar trava dizendo o porque',
         semNada.motivo);
  checar(/Desenhe na mesa|importe um DXF/.test(semNada.oque),
         'Arquivos: e diz de onde vem um programa', semNada.oque);

  await p.request.post(BASE + '/teste/estado', { data: { progN: 3, trajN: 40 } });
  await p.waitForTimeout(700);

  await p.locator('#sdNome').fill('nome/invalido');
  await p.waitForTimeout(300);
  const nomeRuim = await p.evaluate(() =>
    document.getElementById('qSdSalvar').textContent.trim());
  checar(/letras/.test(nomeRuim),
         'Arquivos: nome com caractere proibido e barrado antes de ir ao robo',
         nomeRuim);
  await p.locator('#sdNome').fill('');
  await p.waitForTimeout(200);

  chamadas.length = 0;
  await p.locator('#sdLista .arq[data-tipo="traj"] [data-car]').first().click();
  await p.waitForTimeout(200);
  const abriuTraj = chamadas.find(c => c.startsWith('/api/sd/carregar'));
  checar(!!abriuTraj && /tipo=traj/.test(abriuTraj),
         'Arquivos: abrir uma trajetoria da lista unica pede o tipo certo', abriuTraj);

  chamadas.length = 0;
  await p.locator('#sdLista .arq[data-tipo="prog"] [data-car]').first().click();
  await p.waitForTimeout(200);
  const carregou = chamadas.find(c => c.startsWith('/api/sd/carregar'));
  checar(!!carregou, 'o botao carregar chama a rota certa com o nome escapado', carregou);

  // TRAJETORIA A MAO LIVRE: o botao dizia nada.
  // O estado da gravacao so aparecia no rotulo minusculo do cabecalho do
  // cartao. O operador apertava "Iniciar gravacao", nada visivel mudava
  // na tela em que ele estava, e concluia que o botao nao funcionava.
  await p.locator('#abas button[data-aba="prog"]').click();
  await p.waitForTimeout(400);
  await p.evaluate(() => {
    const alvo = document.getElementById('gravBox').closest('.et');
    document.querySelectorAll('#pnProg .et').forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await p.request.post(BASE + '/teste/estado', { data: { modo: 'MANUAL', trajN: 0 } });
  await p.waitForTimeout(700);
  const gravParado = await p.evaluate(() => ({
    classe: document.getElementById('gravBox').className,
    tit: document.getElementById('gravTit').textContent.trim(),
  }));
  checar(!/on/.test(gravParado.classe) && /Parado/.test(gravParado.tit),
         'Trajetoria: parada, a tarja diz que nao ha nada gravado',
         JSON.stringify(gravParado));

  await p.request.post(BASE + '/teste/estado', { data: { modo: 'GRAVANDO', trajN: 17 } });
  await p.waitForTimeout(700);
  const gravando = await p.evaluate(() => ({
    classe: document.getElementById('gravBox').className,
    tit: document.getElementById('gravTit').textContent.trim(),
    msg: document.getElementById('gravMsg').textContent.trim(),
  }));
  checar(/\bon\b/.test(gravando.classe) && /GRAVANDO/.test(gravando.tit) &&
         /17/.test(gravando.tit),
         'Trajetoria: gravando, a tarja acende e conta as amostras que entram',
         JSON.stringify(gravando));
  checar(/[Mm]ova o braco/.test(gravando.msg) && /joystick|Mover/.test(gravando.msg),
         'Trajetoria: e diz o que fazer em seguida, que era o que faltava',
         gravando.msg);

  await p.request.post(BASE + '/teste/estado', { data: { modo: 'MANUAL', trajN: 42, trajMs: 8300 } });
  await p.waitForTimeout(700);
  const gravFeita = await p.evaluate(() => ({
    classe: document.getElementById('gravBox').className,
    tit: document.getElementById('gravTit').textContent.trim(),
    msg: document.getElementById('gravMsg').textContent.trim(),
  }));
  checar(/tem/.test(gravFeita.classe) && /42/.test(gravFeita.msg),
         'Trajetoria: encerrada, a tarja mostra o que ficou na memoria',
         JSON.stringify(gravFeita));
  await p.request.post(BASE + '/teste/estado', { data: { trajN: 24, trajMs: 4200 } });
  await p.waitForTimeout(500);

  // PARAR alcancavel de qualquer aba
  const parar = await p.locator('#btParar').boundingBox();
  checar(parar && parar.y < 120, 'o botao PARAR fica sempre visivel no topo',
         'y=' + Math.round(parar.y) + 'px');

  checar(erros.length === 0, 'nenhum erro de JavaScript depois de toda a navegacao',
         erros.length ? erros.slice(0, 3).join(' | ') : 'console limpo');

  // ---------------- COMPUTADOR ----------------
  console.log('\n\x1b[1m== Interface no computador (1440x900) ==\x1b[0m');
  const pc = await browser.newContext({ viewport: { width: 1440, height: 900 } });
  const q = await pc.newPage();
  const errosPc = [];
  q.on('pageerror', e => errosPc.push(String(e)));
  await q.goto(BASE, { waitUntil: 'domcontentloaded' });
  await q.waitForTimeout(700);

  const mesaVis = await q.locator('#cv').isVisible();
  const colunaVis = await q.locator('#pnMover').isVisible();
  const abasBaixo = await q.locator('#abas').isVisible();
  checar(mesaVis && colunaVis, 'no computador a mesa de tracado e a coluna aparecem juntas');
  checar(!abasBaixo, 'a barra de abas do celular nao aparece no computador');
  await q.screenshot({ path: SAIDA + '/computador-1-mover.png' });

  await q.locator('#abasTopo button[data-aba="prog"]').click();
  await q.waitForTimeout(300);
  const mesaAinda = await q.locator('#cv').isVisible();
  checar(mesaAinda, 'trocar de aba no computador nao esconde a mesa de tracado');
  await q.screenshot({ path: SAIDA + '/computador-2-programa.png' });

  // A COLUNA DE DIAGNOSTICO NASCE FECHADA.
  // Ela e nivel 3 -- rodinhas, grafico e quinze numeros de manutencao --
  // e ocupava um terco da tela de operacao, na frente de quem nunca viu
  // a maquina. Quem precisa dela abre num toque.
  const encFechada = await q.evaluate(() => ({
    corpo: document.body.classList.contains('comEnc'),
    vis: !!document.getElementById('eM1').offsetParent,
    botao: !!document.getElementById('btEnc').offsetParent,
  }));
  checar(!encFechada.corpo && !encFechada.vis && encFechada.botao,
         'Encoder: a coluna de diagnostico nasce fechada, com um botao para abrir',
         JSON.stringify(encFechada));

  await q.locator('#btEnc').click();
  await q.waitForTimeout(400);
  // Aberta, ela fica aberta enquanto se usa o resto: e para isso que ela
  // saiu da barra de abas. Trocar de aba para olhar o erro seria perder
  // justamente o momento em que ele acontece.
  for (const aba of ['mover', 'prog', 'arq']) {
    await q.locator('#abasTopo button[data-aba="' + aba + '"]').click();
    await q.waitForTimeout(220);
    const vis = await q.locator('#eM1').isVisible();
    checar(vis, 'Encoder: aberta, a coluna continua na tela com a aba ' + aba);
  }
  // E continua aberta tambem com a gaveta de configuracao na tela: quem
  // esta mexendo no registrador Modbus e exatamente quem precisa ver a
  // leitura reagir.
  await abrirGaveta(q);
  const encComGaveta = await q.locator('#eM1').isVisible();
  checar(encComGaveta,
         'Encoder: a coluna continua visivel com a gaveta de configuracao aberta');

  // CONFIGURACAO EM TELA LARGA: os cartoes entram em colunas e nascem
  // abertos. A gaveta exclusiva -- abrir uma fecha as outras -- existe
  // por causa da coluna estreita do celular; aqui ela deixava dois
  // tercos da tela em branco com o resto escondido atras de um clique.
  const cfgCol = await q.evaluate(() => {
    const pane = document.querySelector('#cfgMaquina');
    const cartoes = [...pane.children].filter(x => x.classList.contains('et'));
    return {
      colunas: parseInt(getComputedStyle(pane).columnCount, 10) || 1,
      cartoes: cartoes.length,
      abertos: cartoes.filter(x => x.classList.contains('aberta')).length,
      largura: pane.getBoundingClientRect().width,
      rolo: pane.parentElement.getBoundingClientRect().width,
    };
  });
  checar(cfgCol.colunas > 1 && cfgCol.colunas <= cfgCol.cartoes,
         'Configuracao: em tela larga os cartoes entram em colunas, sem sobrar coluna vazia',
         cfgCol.colunas + ' coluna(s) para ' + cfgCol.cartoes + ' cartao(oes)');
  checar(cfgCol.abertos === cfgCol.cartoes,
         'Configuracao: e nascem todos abertos -- nada escondido atras de um clique',
         cfgCol.abertos + ' de ' + cfgCol.cartoes + ' abertos');
  // Sem teto de largura a coluna se estica e a linha vira um nome na
  // esquerda com o campo la longe, na direita.
  checar(cfgCol.largura < cfgCol.rolo,
         'Configuracao: o bloco tem teto de largura e fica centrado, em vez de esticar',
         Math.round(cfgCol.largura) + ' px de ' + Math.round(cfgCol.rolo));
  // E fechar continua sendo um clique: a alternancia nao pode ter virado
  // exclusividade ao contrario.
  await q.locator('#cfgMaquina .et .cab .chv').first().click();
  await q.waitForTimeout(200);
  const cfgFecha = await q.evaluate(() => {
    const cartoes = [...document.querySelector('#cfgMaquina').children]
      .filter(x => x.classList.contains('et'));
    return { primeiro: cartoes[0].classList.contains('aberta'),
             resto: cartoes.slice(1).filter(x => x.classList.contains('aberta')).length };
  });
  checar(!cfgFecha.primeiro && cfgFecha.resto > 0,
         'Configuracao: fechar um cartao nao fecha os outros junto',
         'restaram ' + cfgFecha.resto + ' aberto(s)');

  await fecharGaveta(q);
  const botaoEnc = await q.evaluate(() => {
    const b = document.querySelector('#abasTopo button[data-aba="enc"]');
    return b ? getComputedStyle(b).display !== 'none' : false;
  });
  checar(!botaoEnc,
         'Encoder: com a coluna aberta, o botao de aba some -- ela ja esta na tela');
  await q.screenshot({ path: SAIDA + '/computador-4-encoder-fixo.png' });
  await q.locator('#abasTopo button[data-aba="mover"]').click();
  await q.waitForTimeout(200);

  // Zero absoluto: a secao nasce TRANCADA em toda visita. O tranco existe
  // para nao se mexer sem querer -- e "sem querer" inclui ter deixado
  // aberto ontem. Errar ali desloca a area util inteira.
  // O zero absoluto e ajuste: mora na gaveta, pagina Encoder.
  await abrirGaveta(q, 'encoder');
  await q.evaluate(() => {
    const alvo = document.getElementById('etZero');
    document.querySelectorAll('#cfgEncoder .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await q.waitForTimeout(300);
  const zTrancado = await q.evaluate(() => ({
    trancado: document.getElementById('etZero').classList.contains('trancado'),
    campoVis: !!document.getElementById('zG').offsetParent,
    estado: document.getElementById('zEstado').textContent,
  }));
  checar(zTrancado.trancado && !zTrancado.campoVis,
         'Zero: a pagina avancada nasce trancada, em toda visita');
  checar(/junta 1: zero ensinado/.test(zTrancado.estado),
         'Zero: mas o ESTADO fica a vista mesmo trancado',
         zTrancado.estado.split('\n')[0]);

  await q.locator('#zCadeado').click();
  await q.waitForTimeout(250);
  const zAberto = await q.evaluate(() =>
    !!document.getElementById('zG').offsetParent);
  checar(zAberto, 'Zero: o cadeado abre os ajustes de origem');
  await q.screenshot({ path: SAIDA + '/computador-7-zero-absoluto.png' });

  // Ensinar o zero PEDE confirmacao: e a origem de onde os limites sao
  // contados, e errar move a area util inteira.
  let pediu = false;
  q.once('dialog', d => { pediu = true; d.dismiss(); });
  await q.locator('#btZensinar').click();
  await q.waitForTimeout(300);
  checar(pediu, 'Zero: ensinar a origem pede confirmacao antes de gravar');

  // A coluna fixa do computador tem de ATUALIZAR sozinha. A consulta
  // estava amarrada a aba escolhida, e no computador nao ha mais aba
  // "enc": a coluna ficava aberta mostrando o dado do momento em que a
  // pagina carregou.
  const n1 = await q.evaluate(() => document.getElementById('anN1').textContent);
  await q.waitForTimeout(1200);
  const n2 = await q.evaluate(() => document.getElementById('anN1').textContent);
  checar(n1 !== n2,
         'Encoder: a coluna fixa se atualiza sozinha, sem a aba estar escolhida',
         n1 + ' -> ' + n2);

  // Travamento: o aviso so aparece quando ha travamento, e fica na tela
  // ate o operador dizer que resolveu. Aviso que some sozinho e aviso
  // que ninguem leu -- e este quer dizer eixo forcando contra ferro.
  await q.evaluate(() => {
    const alvo = document.getElementById('crTrav').closest('.et');
    document.querySelectorAll('#cfgEncoder .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await q.waitForTimeout(300);
  const semTrav = await q.locator('#crTrav').isVisible();
  await q.request.post(BASE + '/teste/encoder',
                       { data: { trvOn: true, trvJ: 2, trvN: 1 } });
  await q.waitForTimeout(2000);
  const comTrav = await q.evaluate(() => ({
    vis: !!document.getElementById('crTrav').offsetParent,
    txt: document.getElementById('crTravTxt').textContent,
  }));
  checar(!semTrav && comTrav.vis,
         'Encoder: o aviso de travamento so aparece quando ha travamento');
  checar(/JUNTA 2 TRAVOU/.test(comTrav.txt),
         'Encoder: e diz qual junta, e o que pode ter acontecido',
         comTrav.txt.split('\n')[0]);
  await q.request.post(BASE + '/teste/encoder',
                       { data: { trvOn: false, trvJ: 0, trvN: 0 } });
  await q.waitForTimeout(700);
  // Fecha a gaveta: daqui para a frente o teste volta a mexer na tela de
  // trabalho, e o veu dela intercepta os cliques.
  await fecharGaveta(q);

  // O "?" do cabecalho nao esconde nada: ele ACRESCENTA uma frase sobre
  // a aba em que a pessoa esta -- o que ela e, e o primeiro passo. As
  // notas curtas de cada painel continuam sempre visiveis, com ele
  // ligado ou desligado.
  await q.evaluate(() => irAba('mover'));
  await q.waitForTimeout(250);
  const ajudaTopo = await q.evaluate(() => ({
    botao: !!document.getElementById('btAjuda'),
    ligado: document.getElementById('btAjuda').classList.contains('on'),
    faixa: (document.getElementById('ajudaAba').textContent || '').trim(),
  }));
  checar(ajudaTopo.botao && ajudaTopo.ligado && /joystick/i.test(ajudaTopo.faixa),
         'Painel: o "?" explica a aba atual',
         JSON.stringify(ajudaTopo));

  await q.evaluate(() => irAba('arq'));
  await q.waitForTimeout(250);
  const ajudaTroca = await q.evaluate(() =>
    (document.getElementById('ajudaAba').textContent || '').trim());
  checar(/cart/i.test(ajudaTroca),
         'Painel: a ajuda troca junto com a aba', ajudaTroca);

  // Desligar o "?" tira a faixa e SO ela: as notas curtas de cada
  // painel nao dependem dele. Comparar antes e depois e o unico jeito
  // honesto de provar isso.
  const notasAntes = await q.evaluate(() =>
    [...document.querySelectorAll('#pnArq .nt')]
      .filter(n => n.getBoundingClientRect().height > 0).length);
  await q.locator('#btAjuda').click();
  await q.waitForTimeout(200);
  const ajudaFora = await q.evaluate(() => ({
    escondida: document.getElementById('ajudaAba').hidden,
    notas: [...document.querySelectorAll('#pnArq .nt')]
             .filter(n => n.getBoundingClientRect().height > 0).length,
  }));
  checar(ajudaFora.escondida && ajudaFora.notas === notasAntes,
         'Painel: quem ja sabe desliga a ajuda, e as notas ficam',
         JSON.stringify(ajudaFora) + ' antes=' + notasAntes);
  await q.locator('#btAjuda').click();
  await q.waitForTimeout(150);
  await q.evaluate(() => irAba('mover'));
  await q.waitForTimeout(250);

  // Os controles continuam todos la -- menos o joystick, que sai DE
  // PROPOSITO no computador: as setas de passo fazem o mesmo com mais
  // precisao e ele so ocupava o espaco dos controles que importam.
  const controles = await q.evaluate(() => ({
    joy: !!document.getElementById('joy').offsetParent,
    prec: !!document.getElementById('btPrec').offsetParent,
    jb: document.querySelectorAll('#pnMover .jb').length,
    vel: !!document.getElementById('inVelMov'),
    rele: !!document.getElementById('btTesteMov'),
  }));
  checar(!controles.joy && controles.prec && controles.jb === 4 &&
         controles.vel && controles.rele,
         'Painel: setas, velocidade e teste de rele na aba Mover; joystick fora no computador',
         JSON.stringify(controles));

  // A tira de estado e UMA peca. Antes eram cinco pares soltos, e no
  // celular a fila encostava no PARAR e a primeira lampada saia da tela.
  const tira = await q.evaluate(() => {
    const l = document.querySelector('.lamps');
    const r = l.getBoundingClientRect();
    const campos = [...l.querySelectorAll('.lp')].map(e => {
      const b = e.getBoundingClientRect();
      return { id: e.id, dentro: b.left >= r.left - 1 && b.right <= r.right + 1,
               pontoELado: e.querySelector('.olho').getBoundingClientRect().left <
                           e.querySelector('span').getBoundingClientRect().left };
    });
    const parar = document.getElementById('btParar').getBoundingClientRect();
    return { n: campos.length, todosDentro: campos.every(c => c.dentro),
             emLinha: campos.every(c => c.pontoELado),
             naoEncosta: r.right <= parar.left + 1 };
  });
  checar(tira.n === 5 && tira.todosDentro && tira.emLinha && tira.naoEncosta,
         'Painel: as cinco lampadas formam uma tira so, e nenhuma sai dela',
         JSON.stringify(tira));

  await q.screenshot({ path: SAIDA + '/computador-6-tira-de-estado.png' });

  // Vista 3D: e a MESMA maquina, vista de outro angulo.  // Vista 3D: e a MESMA maquina, vista de outro angulo. A vista de cima
  // continua sendo a de trabalho (e nela que se desenha e se escolhe
  // ponto), entao o botao alterna e nada mais muda de lugar.
  await q.locator('#z3D').click();
  await q.waitForTimeout(400);
  const v3d = await q.evaluate(() => ({
    ligado: document.getElementById('z3D').classList.contains('on'),
    texto:  document.getElementById('z3D').textContent.trim(),
    mesa:   document.getElementById('cv').offsetParent !== null,
    pintou: (() => {
      const c = document.getElementById('cv');
      const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
      // Alguma coisa foi desenhada se ha pixel diferente do fundo.
      const r0 = d[0], g0 = d[1], b0 = d[2];
      for (let i = 0; i < d.length; i += 4 * 977)
        if (d[i] !== r0 || d[i+1] !== g0 || d[i+2] !== b0) return true;
      return false;
    })(),
  }));
  checar(v3d.ligado && v3d.texto === '2D',
         'Mesa: o botao 3D alterna e passa a oferecer a volta para 2D', v3d.texto);
  checar(v3d.mesa && v3d.pintou,
         'Mesa: a vista 3D desenha de verdade, nao fica em branco');
  await q.screenshot({ path: SAIDA + '/computador-5-vista3d.png' });

  await q.locator('#z3D').click();
  await q.waitForTimeout(300);
  const volta = await q.evaluate(() =>
    document.getElementById('z3D').textContent.trim());
  checar(volta === '3D', 'Mesa: e volta para a vista de cima', volta);

  await q.locator('#zTema').click();
  await q.waitForTimeout(300);
  await q.screenshot({ path: SAIDA + '/computador-3-tema-escuro.png' });
  const tema = await q.evaluate(() => document.documentElement.getAttribute('data-tema'));
  checar(tema === 'escuro', 'o botao TEMA continua alternando prancheta e oficina');

  checar(errosPc.length === 0, 'nenhum erro de JavaScript no computador',
         errosPc.length ? errosPc.slice(0, 3).join(' | ') : 'console limpo');

  // ============== TODOS OS CONTROLES ==============
  console.log('\n\x1b[1m== Todo controle da interface, um por um ==\x1b[0m');
  const t = await cel.newPage();
  const errosT = [];
  t.on('pageerror', e => errosT.push(String(e)));
  t.on('console', m => { if (m.type() === 'error') errosT.push(m.text()); });
  let rotas = [];
  const RUIDO = ['/api/status', '/api/sd', '/api/sd/lista', '/api/pontos', '/api/trajetoria'];
  let ultimoCorpo = '';
  t.on('request', r => {
    const u = new URL(r.url());
    if (!u.pathname.startsWith('/api')) return;
    rotas.push(u.pathname + u.search);
    const c = r.postData();
    if (c) ultimoCorpo = c;
  });
  t.on('dialog', d => d.accept());
  await t.goto(BASE, { waitUntil: 'domcontentloaded' });
  await t.waitForTimeout(700);

  // Nenhum id repetido: getElementById devolve so o primeiro, e o segundo
  // botao ficaria sem handler.
  const dups = await t.evaluate(() => {
    const v = {}, d = [];
    document.querySelectorAll('[id]').forEach(e => { if (v[e.id]) d.push(e.id); else v[e.id] = 1; });
    return d;
  });
  checar(dups.length === 0, 'nenhum id repetido no documento',
         dups.length ? dups.join(', ') : 'todos unicos');

  // ICONES: um conjunto so.
  // A gaveta tinha 18 dingbats de blocos Unicode diferentes, dois deles
  // emoji COLORIDO -- traco, peso e cor mudavam de linha para linha, e no
  // celular mudavam de novo. Agora todo marcador sai do mesmo sprite. Sem
  // este teste o proximo cartao volta a trazer o seu enfeite.
  const ico = await t.evaluate(() => {
    const marcas = [...document.querySelectorAll('.mk')];
    const glifos = [], usos = [], quebrados = [];
    marcas.forEach(m => {
      const u = m.querySelector('use');
      if (u) {
        const alvo = u.getAttribute('href') || '';
        usos.push(alvo);
        if (!document.querySelector(alvo)) quebrados.push(alvo);
        return;
      }
      const t = m.textContent.trim();
      // Numero de etapa e rotulo, nao icone: 1, 2, 3... podem ficar.
      if (t && !/^[0-9]+$/.test(t)) glifos.push(t);
    });
    return { glifos, usos: usos.length, quebrados,
             classes: [...document.querySelectorAll('.mk svg')]
                        .every(e => e.classList.contains('ic')) };
  });
  checar(ico.glifos.length === 0,
         'icones: nenhum marcador solto de Unicode sobrou na interface',
         ico.glifos.length ? 'ainda ha: ' + ico.glifos.join(' ') : ico.usos + ' vindos do sprite');
  checar(ico.quebrados.length === 0,
         'icones: todo <use> aponta para um simbolo que existe',
         ico.quebrados.length ? 'sem alvo: ' + ico.quebrados.join(', ') : 'todos resolvem');
  checar(ico.classes,
         'icones: todos usam a mesma classe, entao mudam de tamanho juntos');

  // A engrenagem do cabecalho virou o link "Configuracao", logo depois do
  // titulo. O que este guarda protege continua sendo o mesmo: o acesso a
  // configuracao existe no cabecalho e tem tamanho de alvo de toque.
  const eng = await t.evaluate(() => {
    const b = document.getElementById('btCfg');
    if (!b) return { ok: false, motivo: 'sem acesso a configuracao no cabecalho' };
    const r = b.getBoundingClientRect();
    return { ok: r.width > 20 && r.height > 12,
             txt: b.textContent.trim(), larg: r.width, alt: r.height };
  });
  checar(eng.ok, 'cabecalho: a Configuracao fica logo depois do titulo, alcancavel',
         JSON.stringify(eng));

  // Todo botao com id tem handler.
  const semH = await t.evaluate(() =>
    [...document.querySelectorAll('button[id]')].filter(e => !e.onclick).map(e => e.id));
  checar(semH.length === 0, 'todo botao identificado tem acao ligada',
         semH.length ? 'sem handler: ' + semH.join(', ') : 'nenhum botao orfao');

  // A sanfona de um painel nao pode fechar a secao de outro painel: era
  // isso que fazia o joystick e "Gravar ponto" sumirem da aba Mover.
  await t.locator('#abas button[data-aba="prog"]').click();
  await t.waitForTimeout(250);
  await t.locator('#e3 .cab').click();
  await t.waitForTimeout(250);
  await t.locator('#abas button[data-aba="mover"]').click();
  await t.waitForTimeout(300);
  const joyDepois = await t.locator('#joy').isVisible();
  const gravDepois = await t.locator('#btGravar').isVisible();
  checar(joyDepois && gravDepois,
         'abrir secao em outra aba nao esconde o joystick nem os atalhos',
         'joystick visivel: ' + joyDepois + ', "Gravar ponto" visivel: ' + gravDepois);

  // Clica cada botao de cada secao, uma secao aberta por vez.
  // Ajustes saiu da barra de abas e virou aba da gaveta da engrenagem.
  const PANES = { mover: '#pnMover', prog: '#pnProg', arq: '#pnArq' };
  const CFG_PANES = { maquina: '#cfgMaquina', calib: '#cfgCalib',
                      encoder: '#cfgEncoder', sistema: '#cfgSistema' };
  const mortos = [], mudos = [];
  let clicados = 0;
  // Abrir o seletor de arquivo E a acao do botao de importar DXF: ele nao
  // chama rota nenhuma. Sem isto o Chromium fica esperando o dialogo e a
  // varredura marca o botao como morto.
  let abriuArquivo = 0;
  t.on('filechooser', fc => { abriuArquivo++; fc.setFiles([]).catch(() => {}); });
  // Percorre os paineis de trabalho e DEPOIS as abas da gaveta: um botao
  // mudo escondido atras da engrenagem continua sendo um botao mudo.
  const TUDO = Object.entries(PANES).concat(
    Object.entries(CFG_PANES).map(([k, v]) => ['cfg:' + k, v]));
  for (const [aba, sel] of TUDO) {
    if (aba.startsWith('cfg:')) {
      await abrirGaveta(t, aba.slice(4));
    } else {
      await fecharGaveta(t);
      await t.locator('#abas button[data-aba="' + aba + '"]').click();
    }
    await t.waitForTimeout(250);
    const nSec = await t.locator(sel + ' .et').count();
    for (let k = 0; k < nSec; k++) {
      await t.evaluate(([sel, k]) => {
        document.querySelectorAll(sel + ' .et').forEach((x, i) => x.classList.toggle('aberta', i === k));
      }, [sel, k]);
      // Sub-bloco recolhido dentro da secao ("Avancado") tambem abre:
      // senao os botoes de dentro contariam como invisiveis e a regra
      // pararia de cobri-los -- que e como um botao morto se esconde.
      await t.evaluate(([sel, k]) => {
        const et = document.querySelectorAll(sel + ' .et')[k];
        et.querySelectorAll('h4.dobra').forEach(h => {
          const alvo = h.nextElementSibling;
          if (alvo && alvo.classList.contains('oculto')) h.click();
        });
      }, [sel, k]);
      await t.waitForTimeout(160);
      const alvos = await t.evaluate(([sel, k]) => {
        const et = document.querySelectorAll(sel + ' .et')[k];
        return [...et.querySelectorAll('button[id]')].map(e => {
          const r = e.getBoundingClientRect();
          const q = document.getElementById('q' + e.id.replace(/^bt/, ''));
          return { id: e.id, vis: r.width > 0 && r.height > 0, dis: e.disabled,
                   motivo: q ? q.textContent.trim() : '' };
        });
      }, [sel, k]);
      for (const a of alvos) {
        // Escondidos DE PROPOSITO, cada um com o seu motivo. A regra
        // existe para pegar botao escondido por acidente; sem esta lista
        // ela vira ruido e para de ser lida.
        //   btTravOk    so existe quando ha travamento para reconhecer
        //   btZensinar  campos do zero absoluto: nascem atras do cadeado
        //   btZesquecer  idem
        //   btZsalvar    idem
        //   btOta       so aparece com particao de OTA no firmware
        //   btRefer     muda a ORIGEM: nasce atras do cadeado da aba Mover
        const ESCONDIDO_DE_PROPOSITO =
          ['btTravOk', 'btZensinar', 'btZesquecer', 'btZsalvar', 'btOta',
           'btRefer'];
        if (ESCONDIDO_DE_PROPOSITO.includes(a.id)) continue;
        // btIdioma recarrega a pagina inteira: clicar nele no meio da
        // varredura invalida todo o resto. Tem cenario proprio.
        if (a.id === 'btIdioma') continue;
        if (!a.vis) { mortos.push(a.id + ' (invisivel na propria secao)'); continue; }
        if (a.dis) { if (!a.motivo) mudos.push(a.id); continue; }
        rotas = [];
        try { await t.locator('#' + a.id).click({ timeout: 1500 }); }
        catch (e) { mortos.push(a.id + ' (nao clicavel)'); continue; }
        await t.waitForTimeout(230);
        clicados++;
        const uteis = rotas.filter(x => !RUIDO.includes(x));
        // Recusa local proposital -- campo vazio, palavra de confirmacao
        // que falta -- nao e botao morto, DESDE QUE ela apareca na tela.
        // Antes isto era uma lista de ids liberados, e lista de excecao
        // envelhece calada: o botao parava de explicar e continuava
        // passando. Agora a excecao e MERECIDA -- vale quem escreveu o
        // motivo no proprio "q" depois do clique.
        const explicou = await t.evaluate((id) => {
          const q = document.getElementById('q' + id.replace(/^bt/, ''));
          return q ? q.textContent.trim().length > 0 : false;
        }, a.id);
        // btDxfAbrir abre o seletor de arquivo, que nao e uma rota;
        // btSoldar e btOta pedem uma segunda acao de proposito (dois
        // toques para o arco, escolher o .bin para o firmware) -- os dois
        // tem cenario proprio mais abaixo, entao aqui nao sao mudos.
        const local = explicou ||
                      (a.id === 'btSoldar') || (a.id === 'btOta') ||
                      (a.id === 'btDxfAbrir' && abriuArquivo > 0);
        if (!uteis.length && !local) mortos.push(a.id + ' (nao chamou nada nem explicou)');
      }
    }
  }
  checar(mortos.length === 0, 'todo botao visivel e habilitado dispara uma acao',
         mortos.length ? 'mortos: ' + mortos.join(', ') : clicados + ' botoes clicados, todos responderam');
  await fecharGaveta(t);
  checar(mudos.length === 0, 'botao desabilitado explica o motivo na tela',
         mudos.length ? 'sem motivo: ' + mudos.join(', ') : 'todos os bloqueios sao explicados');

  // Trecho que o robo nao consegue percorrer tem de aparecer NA LISTA,
  // enquanto o operador ensina -- nao so ao apertar Executar.
  await t.locator('#abas button[data-aba="prog"]').click();
  await t.waitForTimeout(250);
  await t.evaluate(() => document.querySelectorAll('#pnProg .et').forEach((x, i) => x.classList.toggle('aberta', i === 0)));
  await t.waitForTimeout(250);
  const avisos = await t.evaluate(() => {
    const a = [...document.querySelectorAll('#lista .avTr')].map(e => e.textContent.trim());
    return { n: a.length, txt: a[0] || '', sb: document.getElementById('sb2').textContent };
  });
  checar(avisos.n === 1 && /junta 2/.test(avisos.txt) && /trecho\(s\) com problema/.test(avisos.sb),
         'trecho impercorrivel aparece na lista assim que o ponto e ensinado',
         avisos.txt || 'nenhum aviso na lista');
  await t.screenshot({ path: SAIDA + '/celular-5-trecho-ruim.png' });

  // ------------------------------------------------------------------
  // Modo aprendizado. Quando ele esta ligado o BRACO ESTA SOLTO -- isso
  // nao pode depender de o operador estar olhando para a letra miuda,
  // nem sumir da tela de quem nao instalou o botao da ponteira.
  // ------------------------------------------------------------------
  rotas = [];
  await t.locator('#btApr').click();
  await t.waitForTimeout(350);
  const chamouApr = rotas.find(x => x.split('?')[0] === '/api/aprender');
  checar(!!chamouApr && /on=1/.test(chamouApr),
         'Aprendizado: o botao da tela pede para ENTRAR no modo',
         chamouApr || 'nada');

  await t.request.post(BASE + '/teste/estado',
    { data: { apr: true, aprSolto: true, aprN: 2, aprBotao: true } });
  await t.waitForTimeout(600);
  const aprLigado = await t.evaluate(() => ({
    bt:  document.getElementById('btApr').textContent.trim(),
    est: document.getElementById('aprEst').textContent.trim(),
    on:  document.getElementById('aprEst').classList.contains('on'),
  }));
  checar(/solto/i.test(aprLigado.est) && aprLigado.on,
         'Aprendizado: com o modo ligado a tela diz que o braco esta SOLTO',
         aprLigado.est);
  checar(/2 pontos/.test(aprLigado.est),
         'Aprendizado: e conta os pontos gravados nesta sessao', aprLigado.est);
  checar(/Sair/.test(aprLigado.bt),
         'Aprendizado: o botao vira "sair" -- um botao so, dois estados',
         aprLigado.bt);

  rotas = [];
  await t.locator('#btApr').click();
  await t.waitForTimeout(350);
  const saiuApr = rotas.find(x => x.split('?')[0] === '/api/aprender');
  checar(!!saiuApr && /on=0/.test(saiuApr),
         'Aprendizado: e o mesmo botao pede para SAIR', saiuApr || 'nada');

  // Com torque (junta sem zero ensinado) a tela nao pode dizer "solto".
  await t.request.post(BASE + '/teste/estado', { data: { aprSolto: false } });
  await t.waitForTimeout(600);
  const comTorque = await t.evaluate(() =>
    document.getElementById('aprEst').textContent.trim());
  checar(/torque/i.test(comTorque) && !/solto/i.test(comTorque),
         'Aprendizado: sem encoder acompanhando, a tela diz que e COM torque',
         comTorque);

  // Fora do modo manual o botao trava dizendo o porque.
  await t.request.post(BASE + '/teste/estado',
    { data: { apr: false, modo: 'EXECUTANDO' } });
  await t.waitForTimeout(600);
  const aprBloq = await t.evaluate(() => ({
    dis: document.getElementById('btApr').disabled,
    motivo: document.getElementById('qApr').textContent.trim(),
  }));
  checar(aprBloq.dis && aprBloq.motivo.length > 0,
         'Aprendizado: fora do manual o botao trava e diz o motivo',
         aprBloq.motivo || 'MUDO');
  await t.request.post(BASE + '/teste/estado',
    { data: { modo: 'MANUAL', aprN: 0, aprBotao: false } });
  await t.waitForTimeout(500);

  // Curso calibrado: barra por junta e area desenhada na mesa.
  await t.locator('#abas button[data-aba="mover"]').click();
  await t.waitForTimeout(300);
  const faixa = await t.evaluate(() => {
    const e = document.getElementById('fx1');
    const b = e.querySelector('.fxB'), i = b && b.querySelector('i');
    return { txt: e.textContent.trim(), temBarra: !!b,
             pos: i ? i.style.left : '', margens: b ? b.querySelectorAll('u').length : 0 };
  });
  checar(faixa.temBarra && faixa.margens === 2 && /%$/.test(faixa.pos),
         'a junta mostra onde esta dentro do curso calibrado',
         faixa.txt + '  |  marcador em ' + faixa.pos + ', ' + faixa.margens +
         ' faixas de margem');

  // Sem calibracao a barra some e o texto diz o porque.
  await t.request.post(BASE + '/teste/estado', { data: { cal1: false } });
  await t.waitForTimeout(500);
  const semCal = await t.evaluate(() => document.getElementById('fx1').textContent.trim());
  checar(semCal === 'sem curso', 'sem calibracao a barra da lugar a "sem curso"', semCal);
  await t.request.post(BASE + '/teste/estado', { data: { cal1: true } });
  await t.waitForTimeout(400);

  // Area alcancavel na mesa: aparece com a protecao de curso ligada.
  await t.locator('#abas button[data-aba="mesa"]').click();
  await t.waitForTimeout(500);
  await t.screenshot({ path: SAIDA + '/celular-6-curso.png' });

  // CALIBRAR SAO QUATRO MARCAS, e nenhuma delas pede numero.
  //
  // A tela antiga tinha campo de medida em duas etapas: o angulo real na
  // referencia e o curso medido com transferidor. Os dois sairam -- o
  // zero e o meio do curso, e a escala do encoder sai das proprias
  // marcas.
  for (const [calib, passo, espera] of [
    ['INDO_A', 1, true], ['LADO_A', 1, false],
    ['VOLTANDO', 2, true], ['LADO_B', 2, false],
  ]) {
    await t.request.post(BASE + '/teste/estado', { data: { calib, calibEixo: 3 } });
    await t.waitForTimeout(500);
    const est = await t.evaluate(() => ({
      veu: document.getElementById('veu').classList.contains('on'),
      passo: document.getElementById('cPasso').textContent,
      instr: document.getElementById('cInstr').textContent,
      onde: document.getElementById('cOnde').textContent,
      trancado: document.getElementById('cOk').disabled,
      campos: document.querySelectorAll('#veu input[type=number]').length,
    }));
    checar(est.veu && new RegExp('EXTREMO ' + passo + ' DE 2').test(est.passo) &&
           est.campos === 0,
           'Calibracao: ' + calib + ' e o extremo ' + passo + ' de 2, sem campo a digitar',
           est.passo + ' | ' + est.instr);
    // Enquanto a MAQUINA anda, o botao nao tem o que guardar. Botao que
    // existe sem fazer nada e pior que botao ausente.
    checar(est.trancado === espera,
           'Calibracao: em ' + calib + ' o botao ' +
           (espera ? 'espera a maquina' : 'guarda o extremo'),
           'desabilitado=' + est.trancado);
    // Os DOIS eixos aparecem: com o braco solto, os dois se calibram na
    // mesma ida.
    checar(/junta 1:/.test(est.onde) && /junta 2:/.test(est.onde),
           'Calibracao: a tela diz onde as DUAS juntas estao agora', est.onde);
  }

  // O sentido do eixo se descobre errado APERTANDO a seta. Tem de dar
  // para consertar ali, sem cancelar tudo.
  await t.request.post(BASE + '/teste/estado',
    { data: { calib: 'LADO_A', calibEixo: 3 } });
  await t.waitForTimeout(500);
  // As setas de jog sairam da calibracao: o batente se alcanca com a MAO,
  // com o motor solto -- que e como se sente o fim do curso. Com torque o
  // operador empurra o eixo contra o ferro e so descobre pelo barulho.
  const semSetas = await t.evaluate(() => ({
    jog: document.querySelectorAll('#veu .jb').length,
    sent: document.getElementById('cSent').style.display !== 'none',
  }));
  checar(semSetas.jog === 0,
         'Calibracao: sem setas de jog -- o extremo se alcanca com a mao',
         semSetas.jog + ' setas');
  checar(semSetas.sent,
         'Calibracao: na primeira parada aparece a conferencia de sentido');

  rotas = [];
  await t.locator('#cInv1').click();
  await t.waitForTimeout(300);
  const inv = rotas.find(x => x.split('?')[0] === '/api/sentido');
  checar(!!inv && /j=1/.test(inv),
         'Calibracao: inverter a junta 1 chama /api/sentido sem cancelar a calibracao',
         inv || 'nada');

  // Depois da primeira marca o sentido some: ja ha medida que seria
  // invertida junto.
  await t.request.post(BASE + '/teste/estado', { data: { calib: 'LADO_B' } });
  await t.waitForTimeout(500);
  checar(!(await t.evaluate(() =>
             document.getElementById('cSent').style.display !== 'none')),
         'Calibracao: feita a primeira marca, a troca de sentido sai da tela');

  // Marcar chama a rota, e sem numero nenhum a tiracolo.
  rotas = [];
  await t.locator('#cOk').click();
  await t.waitForTimeout(300);
  const conf = rotas.find(x => x.startsWith('/api/calib/confirmar'));
  checar(!!conf && !/g1=|g2=/.test(conf),
         'Calibracao: marcar chama a rota sem numero a tiracolo -- nao ha o que perguntar',
         conf || 'nada');
  await t.request.post(BASE + '/teste/estado', { data: { calib: 'INATIVO' } });
  await t.waitForTimeout(400);

  // Controles da lista de pontos.
  await t.locator('#abas button[data-aba="prog"]').click();
  await t.waitForTimeout(250);
  await t.evaluate(() => document.querySelectorAll('#pnProg .et').forEach((x, i) => x.classList.toggle('aberta', i === 0)));
  await t.waitForTimeout(200);
  for (const [sel, rota, nome] of [
    ['#lista [data-ir]',  '/api/ponto/ir',      'ir ate o ponto'],
    ['#lista [data-sw]',  '/api/ponto/solda',   'chave de solda do trecho'],
    ['#lista [data-del]', '/api/ponto/remover', 'apagar ponto'],
  ]) {
    rotas = [];
    await t.locator(sel).first().click();
    await t.waitForTimeout(250);
    checar(rotas.some(x => x.split('?')[0] === rota),
           'lista de pontos: ' + nome + ' chama ' + rota);
  }

  // O PROGRAMA E UMA SEQUENCIA, e a lista tem de se ler como uma: cada
  // trecho carrega um trilho a esquerda, laranja onde ha cordao. Sem
  // isso a pergunta "esse trecho solda?" so se responde lendo texto.
  const trilho = await t.evaluate(() => {
    const tr = [...document.querySelectorAll('#lista .tr')];
    const cor = tr.map(x => getComputedStyle(x, '::before').backgroundColor);
    return { n: tr.length, cores: cor,
             comCordao: tr.filter(x => x.classList.contains('q')).length,
             distintas: new Set(cor).size };
  });
  checar(trilho.n > 0 && trilho.cores.every(c => c && c !== 'rgba(0, 0, 0, 0)'),
         'Programa: cada trecho tem o trilho desenhado ao lado',
         trilho.n + ' trecho(s)');
  checar(trilho.comCordao > 0,
         'Programa: e o trecho com cordao esta marcado como tal',
         trilho.comCordao + ' com cordao');

  // As duas contas que se faz antes de mandar executar: quanto o braco
  // anda ao todo e quanto disso sai com arco aberto.
  const soma = await t.evaluate(() => {
    const s = document.querySelector('#lista .somaProg');
    return s ? s.textContent.replace(/\s+/g, ' ').trim() : '';
  });
  checar(/percurso/.test(soma) && /cordao/.test(soma) && /mm/.test(soma),
         'Programa: o rodape soma o percurso e quanto dele e cordao', soma);

  // Pre-requisito nao e erro. "Falta fazer isto antes" saia na mesma cor
  // de "deu errado", e a coluna inteira parecia uma pilha de falhas com
  // a maquina em ordem.
  const preCor = await t.evaluate(() => {
    const q = document.getElementById('qAprMarcar');
    if (!q) return null;
    return { pre: q.classList.contains('pre'),
             cor: getComputedStyle(q).color,
             erro: getComputedStyle(document.documentElement)
                     .getPropertyValue('--quente').trim() };
  });
  checar(preCor && preCor.pre,
         'Programa: o motivo de um passo estar bloqueado e marcado como pre-requisito',
         preCor ? preCor.cor : 'sem elemento');

  // Mesa de tracado: clique comanda XY, zoom e tema respondem.
  await t.locator('#abas button[data-aba="mesa"]').click();
  await t.waitForTimeout(350);
  // LEVAR A PONTA COM UM TOQUE PRECISA SER PEDIDO.
  // Era o padrao: tocar em qualquer lugar vazio mandava o robo para la.
  // Quem esta olhando o desenho toca nele o tempo todo -- para conferir
  // uma cota, para escolher o eixo -- e cada toque virava um movimento
  // que ninguem pediu.
  rotas = [];
  const cvb = await t.locator('#cv').boundingBox();
  await t.mouse.click(cvb.x + cvb.width * 0.62, cvb.y + cvb.height * 0.4);
  await t.waitForTimeout(250);
  checar(!rotas.some(x => x.split('?')[0] === '/api/mover_xy'),
         'tocar na mesa NAO manda o braco andar: o desenho e so desenho',
         rotas.join(' ') || 'nenhuma rota');

  rotas = [];
  await t.locator('#zIr').click();
  await t.waitForTimeout(150);
  await t.mouse.click(cvb.x + cvb.width * 0.62, cvb.y + cvb.height * 0.4);
  await t.waitForTimeout(250);
  checar(rotas.some(x => x.split('?')[0] === '/api/mover_xy'),
         'com o botao IR ligado, o toque comanda a ponta', rotas.join(' '));

  // IR e DES disputam o mesmo toque: ligar um desliga o outro.
  await t.locator('#zDes').click();
  await t.waitForTimeout(200);
  const modos = await t.evaluate(() => ({
    ir: document.getElementById('zIr').classList.contains('on'),
    des: document.getElementById('zDes').classList.contains('on'),
  }));
  checar(modos.des && !modos.ir,
         'IR e DES nao ficam ligados juntos: os dois consomem o mesmo toque',
         JSON.stringify(modos));
  await t.locator('#zDes').click();
  await t.waitForTimeout(150);
  const v1 = await t.evaluate(() => window.__vista);
  await t.locator('#zMais').click(); await t.waitForTimeout(150);
  await t.locator('#zAuto').click(); await t.waitForTimeout(150);
  checar(true, 'botoes de zoom e enquadramento respondem sem erro');
  void v1;

  // Desenhar o caminho com o dedo sobre a mesa.
  rotas = [];
  await t.locator('#zDes').click();
  await t.waitForTimeout(200);
  const barraVis = await t.locator('#barraDes').isVisible();
  checar(barraVis, 'o botao DES abre o modo de desenho sobre a mesa');

  // Risca um arco com o dedo, bem no meio da area util. Curva de proposito:
  // numa reta o simplificador devolveria dois pontos e o teste nao provaria
  // que ele preserva a forma.
  const cvd = await t.locator('#cv').boundingBox();
  const dcx = cvd.x + cvd.width * 0.5, dcy = cvd.y + cvd.height * 0.56;
  const dR = Math.min(cvd.width, cvd.height) * 0.22;
  await t.mouse.move(dcx + dR, dcy);
  await t.mouse.down();
  for (let i = 1; i <= 40; i++) {
    const a = (-Math.PI / 3) * (i / 40);
    await t.mouse.move(dcx + dR * Math.cos(a), dcy + dR * Math.sin(a));
  }
  await t.mouse.up();
  await t.waitForTimeout(200);
  const contagem = await t.locator('#dCnt').textContent();
  const nPts = parseInt((contagem.match(/(\d+) pontos/) || [0, 0])[1], 10);
  checar(/\d+ amostras/.test(contagem) && nPts >= 3 && nPts <= 40,
         'o traco a mao livre e simplificado, sem virar uma reta nem estourar os 40 pontos',
         contagem);

  // No modo desenho o toque nao pode comandar a ponta.
  checar(!rotas.some(x => x.split('?')[0] === '/api/mover_xy'),
         'desenhando, o toque na mesa nao manda o braco para la');

  rotas = [];
  await t.locator('#dEnviar').click();
  await t.waitForTimeout(350);
  const des = rotas.find(x => x.split('?')[0] === '/api/prog/desenho');
  checar(!!des, 'o traco e enviado para virar programa', des || 'nada enviado');
  const corpoDes = ultimoCorpo;
  checar(/^-?[\d.]+,-?[\d.]+(;-?[\d.]+,-?[\d.]+)+$/.test(corpoDes),
         'o corpo vai em milimetros de chapa, "x,y;x,y"',
         corpoDes.slice(0, 60) + (corpoDes.length > 60 ? '...' : ''));
  const saiu = await t.locator('#barraDes').isVisible();
  checar(!saiu, 'enviado o desenho, a mesa volta ao modo normal');

  // Importar DXF: ler o arquivo, posicionar na mesa e virar programa.
  // Curso folgado para o caso de aplicar ser exercitado de verdade; o
  // caso apertado ja e coberto pela propria conta de alcance.
  await t.request.post(BASE + '/teste/estado',
    { data: { j1min: -150, j1max: 150, j2min: -150, j2max: 150, protEnv: false } });
  await t.waitForTimeout(400);
  await t.locator('#abas button[data-aba="prog"]').click();
  await t.waitForTimeout(250);
  await t.evaluate(() => document.querySelectorAll('#pnProg .et')
    .forEach(x => x.classList.toggle('aberta', x.id === 'eDxf')));
  await t.waitForTimeout(150);
  checar(await t.locator('#btDxfPos').isDisabled(),
         'DXF: sem arquivo, "Posicionar na mesa" fica travado com motivo');

  await t.setInputFiles('#dxfArq', 'testes/interface/amostras/peca.dxf');
  await t.waitForTimeout(400);
  const info = (await t.locator('#dxfInfo').textContent()).replace(/\n/g, ' | ');
  checar(/LWPOLYLINE/.test(info) && /CIRCLE/.test(info) && /contorno/.test(info),
         'DXF: o arquivo e lido e resumido para o operador', info);
  checar(/2 entidade\(s\) ignorada\(s\)/.test(info),
         'DXF: TEXT e DIMENSION sao contados como ignorados, nao viram trajeto');
  // As duas LINE que se encostam tem de virar UM contorno, nao dois.
  const nCont = parseInt((info.match(/(\d+) contorno/) || [0, 0])[1], 10);
  checar(nCont === 4,
         'DXF: linhas que se encostam sao emendadas num contorno so',
         nCont + ' contornos (poli, circulo, arco e as duas linhas emendadas)');

  await t.locator('#btDxfPos').click();
  await t.waitForTimeout(400);
  checar(await t.locator('#barraPos').isVisible(),
         'DXF: "Posicionar" leva para a mesa com a barra de posicionamento');
  const cnt0 = await t.locator('#pCnt').textContent();
  checar(/pontos/.test(cnt0), 'DXF: a barra conta os pontos e o que cai fora', cnt0);

  // Arrastar, girar e espelhar tem de mexer no desenho sem erro.
  const cvp = await t.locator('#cv').boundingBox();
  await t.mouse.move(cvp.x + cvp.width * 0.5, cvp.y + cvp.height * 0.4);
  await t.mouse.down();
  await t.mouse.move(cvp.x + cvp.width * 0.56, cvp.y + cvp.height * 0.44, { steps: 6 });
  await t.mouse.up();
  await t.waitForTimeout(200);
  await t.locator('#pGirarP').click(); await t.waitForTimeout(120);
  await t.locator('#pEsp').click();    await t.waitForTimeout(120);
  await t.locator('#pCentro').click(); await t.waitForTimeout(200);
  const cnt1 = await t.locator('#pCnt').textContent();
  checar(/pontos/.test(cnt1), 'DXF: arrastar, girar, espelhar e centralizar respondem', cnt1);

  // ---- origem marcada COM O BRACO ----
  // Arrastar o desenho na tela pede que o operador saiba onde a peca
  // esta em milimetros. Na bancada ele nao sabe: sabe onde a peca ESTA,
  // porque esta olhando para ela. Entao o caminho e o contrario -- solta
  // o braco, leva a ponta ate onde o desenho comeca, confirma.
  await t.request.post(BASE + '/teste/estado', { data: { apr: false, x: 300, y: 120 } });
  await t.waitForTimeout(500);
  rotas = [];
  await t.locator('#pOrigem').click();
  await t.waitForTimeout(400);
  const soltou = rotas.find(x => x.split('?')[0] === '/api/aprender');
  checar(!!soltou && /on=1/.test(soltou),
         'Origem: o botao solta o braco para o operador levar a ponta',
         soltou || rotas.join(' '));
  checar(/confirmar/i.test(await t.locator('#pOrigem').textContent()),
         'Origem: e o proprio botao passa a pedir a confirmacao',
         await t.locator('#pOrigem').textContent());

  // Confirmado, o PRIMEIRO ponto do desenho tem de cair na ponta -- e o
  // primeiro, e nao o centro, porque foi ali que o operador encostou.
  await t.request.post(BASE + '/teste/estado', { data: { apr: true, x: 300, y: 120 } });
  await t.waitForTimeout(500);
  rotas = [];
  await t.locator('#pOrigem').click();
  await t.waitForTimeout(500);
  const p0 = await t.evaluate(() => posTransformado()[0][0]);
  checar(Math.abs(p0[0] - 300) < 0.6 && Math.abs(p0[1] - 120) < 0.6,
         'Origem: confirmado, o desenho comeca onde a ponta parou',
         'primeiro ponto em ' + p0.map(v => v.toFixed(1)).join(', ') + ' (ponta: 300, 120)');
  const origemFechou = rotas.find(x => x.split('?')[0] === '/api/aprender');
  checar(!!origemFechou && /on=0/.test(origemFechou),
         'Origem: e o braco volta a ficar preso ao confirmar',
         origemFechou || rotas.join(' '));

  // O modo pode cair por fora -- emergencia, botao da ponteira.
  // Cancelar em silencio deixaria o botao mentindo.
  await t.locator('#pOrigem').click();
  await t.request.post(BASE + '/teste/estado', { data: { apr: true } });
  await t.waitForTimeout(700);
  await t.request.post(BASE + '/teste/estado', { data: { apr: false } });
  await t.waitForTimeout(700);
  checar(!/confirmar/i.test(await t.locator('#pOrigem').textContent()),
         'Origem: se o aprendizado cair por fora, o botao volta ao inicio',
         await t.locator('#pOrigem').textContent());

  rotas = [];
  ultimoCorpo = '';
  const podeAplicar = !(await t.locator('#pAplicar').isDisabled());
  if (podeAplicar) {
    await t.locator('#pAplicar').click();
    await t.waitForTimeout(400);
    checar(rotas.some(x => x.split('?')[0] === '/api/prog/desenho'),
           'DXF: aplicar manda o desenho pela mesma rota do traco a dedo');
    checar(/^-?[\d.]+,-?[\d.]+,[01](;-?[\d.]+,-?[\d.]+,[01])+$/.test(ultimoCorpo),
           'DXF: cada ponto leva o proprio estado de arco (x,y,solda)',
           ultimoCorpo.slice(0, 56) + '...');
    // Ultimo ponto de cada contorno tem de fechar o arco.
    const fim = ultimoCorpo.split(';').pop();
    checar(/,0$/.test(fim), 'DXF: o ultimo ponto nao deixa o arco aberto', fim);
  } else {
    checar(true, 'DXF: com pontos fora do alcance, aplicar fica travado',
           await t.locator('#pCnt').textContent());
    await t.locator('#pCancel').click();
  }
  await t.waitForTimeout(200);
  checar(!(await t.locator('#barraPos').isVisible()),
         'DXF: terminado o posicionamento, a mesa volta ao normal');
  await t.request.post(BASE + '/teste/estado',
    { data: { j1min: -95, j1max: 95, j2min: -120, j2max: 30 } });
  await t.waitForTimeout(400);

  // Encoder: leitura ao vivo e configuracao do registrador.
  //
  // O comandado e o erro sairam da tela. O comandado e a contagem de
  // pulsos do firmware, que numa maquina em montagem anda sozinha: o
  // painel chegou a mostrar "comandado 1986,79 / medido -230,05 / erro
  // +2216,85". Nenhum dos tres ajudava a operar, e o do meio -- o unico
  // que descreve o braco -- ficava perdido entre dois que nao descrevem
  // nada. Ficou o MEDIDO, que e onde a junta esta.
  await t.locator('#abas button[data-aba="enc"]').click();
  await t.waitForTimeout(900);
  const enc = await t.evaluate(() => ({
    med:  document.getElementById('eM1').textContent.trim(),
    med2: document.getElementById('eM2').textContent.trim(),
    sb:   document.getElementById('sbEnc').textContent.trim(),
    temErro: !!(document.getElementById('eE1') || document.getElementById('eC1')),
  }));
  checar(/°$/.test(enc.med),
         'Encoder: mostra o angulo MEDIDO da junta, em graus', enc.med);
  checar(!enc.temErro,
         'Encoder: o comandado e o erro sairam -- so o angulo que descreve o braco fica',
         enc.temErro ? 'ainda ha celula de erro/comandado' : 'so o medido');
  // Um driver so ligado e o caso normal de bancada: a junta 2 nao esta
  // "com defeito", ela nao esta ligada. Contar isso como falha manda o
  // operador cacar problema que nao existe.
  checar(enc.med2 === 'nao ligada',
         'Encoder: junta sem registrador aparece como nao ligada, nao como falha',
         enc.med2);
  const est2 = await t.evaluate(() => document.getElementById('encEstado').textContent);
  checar(/junta 2: nao ligada/.test(est2),
         'Encoder: e o resumo tambem, sem contador de falha inventado',
         est2.replace(/\n/g, ' | '));
  const quadro = await t.evaluate(() => document.getElementById('encQuadro').textContent);
  checar(/->/.test(quadro) && /<-/.test(quadro),
         'Encoder: a tela mostra o ultimo quadro cru trocado no fio',
         quadro);

  // Agora a junta 2 LIGADA e muda: ai sim e falha, e a tela tem que
  // dizer qual.
  await t.request.post(BASE + '/teste/encoder', { data: { reg2: 4098, motivo2: 2 } });
  await t.waitForTimeout(700);
  const med2b = await t.evaluate(() => document.getElementById('eM2').textContent.trim());
  checar(med2b === 'sem resposta',
         'Encoder: junta ligada que nao responde diz POR QUE, nao so "nada"',
         med2b);
  await t.request.post(BASE + '/teste/encoder', { data: { reg2: 0, motivo2: 1 } });
  await t.waitForTimeout(700);
  checar(enc.sb === 'lendo', 'Encoder: o cabecalho da aba mostra o estado', enc.sb);
  /* O grafico do erro saiu junto com o erro. O grafico da ANALISE
     DETALHADA continua, e e ele que precisa ser dimensionado ao abrir a
     aba -- canvas com largura zero desenha no vazio. */
  const larguraPos = await t.evaluate(() => {
    const c = document.getElementById('cvPos');
    return c ? c.width : 0;
  });
  checar(larguraPos > 100, 'Encoder: o grafico da analise e dimensionado ao abrir a aba',
         larguraPos + ' px');

  // ---- a gaveta abre enxuta ----
  // A queixa era de proporcao: entre um campo e o proximo cabia uma
  // pagina de manual, e quem so queria mudar a velocidade rolava cinco
  // telas. As explicacoes continuam la, atras do "?" da propria gaveta.
  await t.evaluate(() => {
    try { localStorage.removeItem('notasCfg'); } catch (e) {}
  });
  await t.reload({ waitUntil: 'domcontentloaded' });
  await t.waitForTimeout(700);
  await t.locator('#btCfg').click();
  await t.waitForTimeout(400);
  // A gaveta lembra a ultima pagina aberta; aqui a medida e da Maquina.
  await t.locator('#cfgAbas button[data-cfg="maquina"]').click();
  await t.waitForTimeout(250);
  const estadoGaveta = await t.evaluate(() => ({
    veu: document.getElementById('veuCfg').className,
    maq: document.getElementById('cfgMaquina').className,
    bt:  !!document.getElementById('btCfg').onclick,
  }));
  checar(/\bon\b/.test(estadoGaveta.veu) && /\bon\b/.test(estadoGaveta.maq),
         'Gaveta: a engrenagem abre a configuracao na pagina Maquina',
         JSON.stringify(estadoGaveta));
  await t.evaluate(() => {
    document.querySelectorAll('#cfgMaquina .et').forEach(x => x.classList.add('aberta'));
  });
  await t.waitForTimeout(200);
  const notas = await t.evaluate(() => {
    const ns = [...document.querySelectorAll('#cfgMaquina .nt')];
    const vis = ns.filter(n => n.getBoundingClientRect().height > 0);
    return { total: ns.length, visiveis: vis.length,
             alturaPane: document.getElementById('cfgMaquina').scrollHeight };
  });
  checar(notas.total > 0 && notas.visiveis === 0,
         'Gaveta: as explicacoes longas nascem escondidas, e a gaveta abre enxuta',
         notas.total + ' notas, ' + notas.visiveis + ' visiveis, pane ' +
         notas.alturaPane + 'px');

  await t.locator('#cfgAjuda').click();
  await t.waitForTimeout(250);
  const comNotas = await t.evaluate(() => ({
    visiveis: [...document.querySelectorAll('#cfgMaquina .nt')]
                .filter(n => n.getBoundingClientRect().height > 0).length,
    alturaPane: document.getElementById('cfgMaquina').scrollHeight,
  }));
  // O que importa e a PROPRIEDADE -- o "?" traz as notas de volta e a
  // gaveta cresce -- e nao quantas notas existem. Amarrar num numero fez
  // este guarda reprovar quando as explicacoes longas foram enxugadas,
  // que era exatamente o que se queria fazer.
  checar(comNotas.visiveis > 0 && comNotas.visiveis > notas.visiveis &&
         comNotas.alturaPane > notas.alturaPane,
         'Gaveta: e o "?" da propria gaveta traz o manual de volta',
         comNotas.visiveis + ' notas, pane ' + comNotas.alturaPane + 'px');

  // O "?" do cabecalho e o da gaveta sao interruptores separados: a tela
  // de trabalho continua ensinando enquanto a gaveta fica limpa.
  const separados = await t.evaluate(() => {
    document.getElementById('cfgAjuda').click();     // esconde de novo na gaveta
    return { cfg: document.body.classList.contains('semNotasCfg'),
             tela: document.body.classList.contains('semNotas') };
  });
  checar(separados.cfg && !separados.tela,
         'Gaveta: esconder na gaveta nao apaga as notas da tela de trabalho',
         JSON.stringify(separados));

  await t.locator('#cfgAbas button[data-cfg="sistema"]').click();
  await t.waitForTimeout(250);

  // ---- apagar tudo: a palavra digitada, e a diferenca entre os dois ----
  // Restaurar padroes e apagar tudo moram no mesmo cartao de proposito:
  // o operador precisa VER que sao coisas diferentes antes de escolher.
  await t.evaluate(() => {
    const alvo = document.getElementById('btApagarTudo').closest('.et');
    document.querySelectorAll('#cfgSistema .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(250);

  rotas = [];
  await t.locator('#btApagarTudo').click();
  await t.waitForTimeout(400);
  const apgVazio = await t.evaluate(() =>
    document.getElementById('qApagarTudo').textContent);
  checar(!rotas.some(u => u.indexOf('/api/apagar') === 0) && /APAGAR/.test(apgVazio),
         'Apagar tudo: sem a palavra digitada nada sai, e a tela diz o que falta',
         apgVazio);

  await t.evaluate(() => { document.getElementById('apgConf').value = 'apagar'; });
  rotas = [];
  await t.locator('#btApagarTudo').click();
  await t.waitForTimeout(600);
  checar(rotas.some(u => u.indexOf('/api/apagar/tudo?conf=APAGAR') === 0),
         'Apagar tudo: com a palavra e a confirmacao, a rota vai com conf=APAGAR',
         rotas.join(' '));
  const apgLimpo = await t.evaluate(() =>
    document.getElementById('apgConf').value);
  checar(apgLimpo === '',
         'Apagar tudo: o campo e limpo depois, para nao ficar armado na tela');

  // Os dois botoes estao no mesmo cartao, e o de apagar e o unico
  // marcado como perigoso.
  const zona = await t.evaluate(() => {
    const et = document.getElementById('btApagarTudo').closest('.et');
    return { perigo: et.classList.contains('zPerigo'),
             temReset: !!et.querySelector('#btReset'),
             classeApagar: document.getElementById('btApagarTudo').className,
             classeReset: document.getElementById('btReset').className };
  });
  checar(zona.perigo && zona.temReset &&
         /perigoso/.test(zona.classeApagar) && !/perigoso/.test(zona.classeReset),
         'Apagar tudo: os dois moram no mesmo cartao, e so um esta pintado de perigo',
         JSON.stringify(zona));

  await t.locator('#cfgFechar').click();
  await t.waitForTimeout(250);
  await t.locator('#abas button[data-aba="enc"]').click();
  await t.waitForTimeout(400);

  // ---- analise detalhada ----
  await t.evaluate(() => {
    const alvo = document.getElementById('tabEnc').closest('.et');
    document.querySelectorAll('#pnEnc .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(1400);
  const anal = await t.evaluate(() => ({
    linhas: document.querySelectorAll('#tabEnc tbody tr').length,
    n1:  document.getElementById('anN1').textContent,
    hz1: document.getElementById('anHz1').textContent,
    me1: document.getElementById('anMe1').textContent,
    sd1: document.getElementById('anSd1').textContent,
    vo1: document.getElementById('anVo1').textContent,
    n2:  document.getElementById('anN2').textContent,
    sub: document.getElementById('sbAnal').textContent,
    larg: document.getElementById('cvPos').width,
  }));
  // 1 linha e so o cabecalho: sem amostra a tabela nao prova nada.
  checar(anal.linhas > 1, 'Encoder: a tabela lista as amostras captadas',
         (anal.linhas - 1) + ' amostras na tabela');
  checar(/°$/.test(anal.me1) && /°$/.test(anal.sd1),
         'Encoder: erro medio e oscilacao saem em graus',
         'medio ' + anal.me1 + ', oscilacao ' + anal.sd1);
  checar(/\/s$/.test(anal.hz1),
         'Encoder: leituras por segundo sao MEDIDAS, nao o periodo pedido',
         anal.hz1);
  checar(anal.vo1 !== '--', 'Encoder: as voltas do motor aparecem em numero',
         anal.vo1 + ' voltas');

  // Velocidade, RPM, sentido, passos e inversoes vem PRONTOS do
  // firmware: ele le a 20 Hz e a pagina consulta a 4 Hz, entao calcular
  // aqui seria medir com uma regua cinco vezes mais grossa.
  const der = await t.evaluate(() => ({
    ve: document.getElementById('anVe1').textContent,
    rp: document.getElementById('anRp1').textContent,
    se: document.getElementById('anSe1').textContent,
    pa: document.getElementById('anPa1').textContent,
    iv: document.getElementById('anIv1').textContent,
    fx: document.getElementById('anFx1').textContent,
    se2: document.getElementById('anSe2').textContent,
  }));
  checar(/c\/s$/.test(der.ve) && /rpm$/.test(der.rp),
         'Encoder: velocidade e RPM medidos pelo encoder aparecem',
         der.ve + ', ' + der.rp);
  checar(/cresce|decresce|parado/.test(der.se),
         'Encoder: o sentido aparece em palavra, nao em numero', der.se);
  checar(der.pa !== '--' && der.pa !== '0' && der.iv !== '--',
         'Encoder: passos andados e inversoes aparecem',
         der.pa + ' passos, ' + der.iv + ' inversoes');
  checar(der.fx !== '--', 'Encoder: a faixa percorrida aparece', der.fx);
  checar(der.se2 === '--',
         'Encoder: junta nao ligada nao ganha sentido inventado', der.se2);
  checar(anal.larg > 100, 'Encoder: o grafico da posicao e dimensionado',
         anal.larg + ' px');
  // A junta 2 nao esta ligada nesta bancada: nao pode inventar estatistica.
  checar(anal.n2 === '--',
         'Encoder: junta nao ligada nao ganha estatistica inventada', anal.n2);
  checar(/amostras/.test(anal.sub),
         'Encoder: o cabecalho da secao diz quantas amostras ha', anal.sub);

  // CSV: a pagina do robo roda num navegador de verdade, entao o download
  // funciona -- mas o que importa e o CONTEUDO ter todas as colunas.
  const csv = await t.evaluate(() => {
    let capturado = null;
    const criar = URL.createObjectURL;
    URL.createObjectURL = function (b) { capturado = b; return criar.call(URL, b); };
    document.getElementById('btEncCsv').click();
    URL.createObjectURL = criar;
    return capturado ? capturado.text() : null;
  });
  const cabCsv = csv ? csv.split('\n')[0] : '';
  checar(/ms,bruto1,medido1,comandado1,erro1,vel1,rpm1/.test(cabCsv),
         'Encoder: o CSV traz bruto, medido, comandado e erro das duas juntas',
         cabCsv);
  checar(csv && csv.trim().split('\n').length > 2,
         'Encoder: e traz as amostras, nao so o cabecalho',
         csv ? (csv.trim().split('\n').length - 1) + ' linhas' : 'nada');

  // No CELULAR nao ha largura honesta para duas colunas: o Encoder volta
  // a ser uma aba como as outras, e some quando outra aba esta aberta.
  await t.evaluate(() => irAba('mover'));
  await t.waitForTimeout(200);
  const somiu = await t.evaluate(() =>
    getComputedStyle(document.getElementById('dockEnc')).display === 'none');
  checar(somiu, 'Encoder: no celular ele e aba, e sai da tela nas outras');
  await t.evaluate(() => irAba('enc'));
  await t.waitForTimeout(250);

  const rodas = await t.evaluate(() => {
    const a = document.getElementById('cvR1'), b = document.getElementById('cvR2');
    // Pixel nao transparente no meio = alguem desenhou o mostrador.
    const ct = a.getContext('2d');
    const px = ct.getImageData(Math.floor(a.width / 2), Math.floor(a.height / 2), 1, 1).data;
    return { w1: a.width, w2: b.width, pintou: px[3] > 0 };
  });
  checar(rodas.w1 > 60 && rodas.w2 > 60 && rodas.pintou,
         'Encoder: as duas rodinhas sao desenhadas com a posicao do motor',
         rodas.w1 + 'x' + rodas.w1 + ' px, centro pintado: ' + rodas.pintou);

  // A coleta tem de crescer com as consultas. Antes o contador vinha do
  // historico do grafico de erro; esse grafico saiu junto com o erro (ver
  // ACHADOS R108) e o historico ficou alimentando um desenho que nao
  // existia mais. Agora o contador e o da tabela de amostras, que e o que
  // se ve na tela.
  await t.waitForTimeout(1600);
  const nAmostras = await t.evaluate(() => window.__encN || 0);
  checar(nAmostras >= 2, 'Encoder: a coleta de amostras cresce a cada consulta',
         nAmostras + ' amostras');

  // Abre a secao PELO CONTEUDO, nao pelo indice: acrescentar uma secao
  // nova no painel nao pode quebrar um teste que nao e sobre ela.
  // A ligacao Modbus e ajuste: mora na gaveta, pagina Encoder.
  await abrirGaveta(t, 'encoder');
  await t.evaluate(() => {
    const alvo = document.getElementById('encReg1').closest('.et');
    document.querySelectorAll('#cfgEncoder .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(300);
  const cfg = await t.evaluate(() => ({
    reg1: document.getElementById('encReg1').value,
    baud: document.getElementById('encBaud').value,
    on:   document.getElementById('encAtivo').classList.contains('on'),
  }));
  checar(cfg.reg1 === '90' && cfg.baud === '19200' && cfg.on,
         'Encoder: a configuracao vinda do robo preenche os campos',
         'reg ' + cfg.reg1 + ', ' + cfg.baud + ' bps');

  rotas = [];
  await abrirGaveta(t, 'encoder');
  await t.evaluate(() => {
    const alvo = document.getElementById('encReg1').closest('.et');
    document.querySelectorAll('#cfgEncoder .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(300);
  await t.locator('#encReg1').fill('8192');
  await t.locator('#btEncSalvar').click();
  await t.waitForTimeout(400);
  const salvou = rotas.find(x => x.split('?')[0] === '/api/encoder/config');
  checar(!!salvou && /reg1=8192/.test(salvou),
         'Encoder: mudar o registrador vai pela rota de configuracao',
         salvou || 'nada');

  // "Zerar a contagem aqui" ficou no painel de LEITURA, nao na gaveta:
  // e uma acao que se faz olhando o numero mudar.
  await fecharGaveta(t);
  rotas = [];
  await t.evaluate(() => {
    const alvo = document.getElementById('btEncZerar').closest('.et');
    document.querySelectorAll('#pnEnc .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(300);
  await t.locator('#btEncZerar').click();
  await t.waitForTimeout(300);
  checar(rotas.some(x => x.split('?')[0] === '/api/encoder/zerar'),
         'Encoder: "Zerar a contagem aqui" chama a rota certa');
  await fecharGaveta(t);

  // Rede: a maquina tem Wi-Fi proprio, o painel so diz por onde chegar.
  await abrirGaveta(t, 'maquina');
  await t.waitForTimeout(250);
  await t.evaluate(() => document.querySelectorAll('#cfgMaquina .et')
    .forEach(x => x.classList.toggle('aberta', x.id === 'eRede')));
  await t.waitForTimeout(600);
  const endereco = (await t.locator('#redeEnd').textContent()).replace(/\n/g, ' | ');
  checar(/192\.168\.4\.1/.test(endereco) && /robo2dof\.local/.test(endereco),
         'Rede: o painel mostra os dois enderecos de acesso', endereco);
  const semConfig = await t.evaluate(() =>
    !document.getElementById('btRedeVarrer') &&
    !document.getElementById('btRedeConectar') &&
    !document.getElementById('redeSenha'));
  checar(semConfig,
         'Rede: nao sobrou nenhum controle do modo estacao na tela');

  // Declarar a referencia: fica atras de um cadeado, porque muda a
  // ORIGEM -- e o botao vizinho, "Ir para o zero da maquina", so ANDA
  // ate ela. Nomes parecidos, consequencias opostas.
  await t.locator('#abas button[data-aba="mover"]').click();
  await t.waitForTimeout(250);
  const origemTrancada = await t.evaluate(() =>
    document.getElementById('movOrig').classList.contains('trancado') &&
    !document.getElementById('btRefer').offsetParent);
  checar(origemTrancada,
         'Mover: mudar a origem nasce atras do cadeado');
  await t.locator('#movCadeado').click();
  await t.waitForTimeout(200);
  rotas = [];   /* o aceite do confirm() ja esta armado la em cima */
  await t.locator('#btRefer').click();
  await t.waitForTimeout(300);
  checar(rotas.some(x => x.split('?')[0] === '/api/referenciar'),
         '"Declarar esta posicao como referencia" confirma e chama /api/referenciar');

  // ------------------------------------------------------------------
  // A GAVETA GANHOU UM COMECO E UMA BUSCA.
  //
  // Eram quinze cartoes sem ordem nenhuma: quem monta a maquina pela
  // primeira vez descobria a sequencia abrindo cartao por cartao. O
  // roteiro poe os cinco passos em ordem, cada um lendo do estado REAL
  // da maquina se ja esta feito.
  // ------------------------------------------------------------------
  await abrirGaveta(t, 'maquina');
  await t.waitForTimeout(500);
  const rot = await t.evaluate(() => ({
    passos: [...document.querySelectorAll('#roteiro .rtItem')]
              .map(e => e.querySelector('.tt2').textContent.trim()),
    feitos: [...document.querySelectorAll('#roteiro .rtItem.ok')]
              .map(e => e.querySelector('.tt2').textContent.trim()),
    resumo: document.getElementById('sbRoteiro').textContent.trim(),
  }));
  checar(rot.passos.length === 5 && /Calibrar o braco/.test(rot.passos.join('|')),
         'Gaveta: o roteiro lista os cinco passos da instalacao, em ordem',
         rot.passos.join(' > '));
  checar(/\d+ de \d+ passos feitos/.test(rot.resumo),
         'Gaveta: e o cabecalho do roteiro conta quantos ja estao feitos',
         rot.resumo);
  checar(rot.feitos.length > 0,
         'Gaveta: os passos ja feitos vem riscados, lidos do estado da maquina',
         rot.feitos.join(', ') || 'nenhum');

  // O atalho de cada passo tem que LEVAR ao lugar. "Calibrar o braco"
  // mora noutra pagina da gaveta: se o botao so abrisse o cartao, quem
  // clicasse ficaria olhando para uma tela que nao mudou.
  await t.evaluate(() => document.getElementById('etRoteiro').classList.add('aberta'));
  await t.waitForTimeout(200);
  await t.locator('#roteiro .rtItem:nth-child(3) [data-rt]').click();
  await t.waitForTimeout(350);
  const foiParaCalib = await t.evaluate(() =>
    document.getElementById('cfgCalib').classList.contains('on'));
  checar(foiParaCalib,
         'Gaveta: o atalho do roteiro troca de pagina, nao so abre o cartao');

  // A busca varre o TEXTO INTEIRO do cartao, de todas as paginas: quem
  // procura "aceleracao" nao sabe (nem tem de saber) que ela mora em
  // "Ajustes da maquina", dentro de "Avancado".
  await t.locator('#cfgProcurar').fill('aceleracao');
  await t.waitForTimeout(350);
  const busca = await t.evaluate(() => ({
    procurando: document.body.classList.contains('cfgProcurando'),
    achados: [...document.querySelectorAll('.cfgRol .et')]
               .filter(e => !e.classList.contains('foraDaBusca'))
               .map(e => e.querySelector('.tt').textContent.trim()),
  }));
  checar(busca.procurando && busca.achados.includes('Ajustes da maquina'),
         'Gaveta: procurar "aceleracao" acha o cartao onde ela mora',
         busca.achados.join(', '));
  checar(busca.achados.length < 5,
         'Gaveta: e esconde os cartoes que nao tem nada a ver',
         busca.achados.length + ' cartao(oes) de pe');

  // Sem acento: quem procura no celular raramente acentua.
  await t.locator('#cfgProcurar').fill('calibracao');
  await t.waitForTimeout(350);
  const semAcento = await t.evaluate(() =>
    [...document.querySelectorAll('.cfgRol .et')]
      .filter(e => !e.classList.contains('foraDaBusca')).length);
  checar(semAcento > 0,
         'Gaveta: a busca ignora acento dos dois lados', semAcento + ' achado(s)');

  await t.locator('#cfgProcurarX').click();
  await t.waitForTimeout(350);
  const limpou = await t.evaluate(() => ({
    procurando: document.body.classList.contains('cfgProcurando'),
    escondidos: document.querySelectorAll('.cfgRol .et.foraDaBusca').length,
  }));
  checar(!limpou.procurando && limpou.escondidos === 0,
         'Gaveta: limpar a busca devolve a gaveta inteira',
         JSON.stringify(limpou));

  // ------------------------------------------------------------------
  // Aba Calibracao: as duas medidas, e a area da mesa.
  //
  // As duas medem coisas DIFERENTES e nao podem ser confundidas: o passo
  // 1 mede a engrenagem eletronica (o encoder faz sozinho), o passo 2
  // mede a reducao mecanica (o encoder nao ve o redutor, entao precisa de
  // uma referencia).
  // ------------------------------------------------------------------
  await abrirGaveta(t, 'calib');
  await t.waitForTimeout(600);

  const resumo = await t.evaluate(() => ({
    n: document.querySelectorAll('#calResumo .sl').length,
    txt: document.getElementById('calResumo').textContent,
    vivo: document.getElementById('calVivo').textContent,
  }));
  checar(resumo.n >= 9,
         'Calibracao: o quadro resume resolucao, reducao, curso e mesa',
         resumo.n + ' linhas');
  checar(/16\.500|16,500/.test(resumo.txt) && /458/.test(resumo.txt),
         'Calibracao: com a reducao e a resolucao de cada junta',
         resumo.txt.slice(0, 70));

  // O REDUTOR mudou de lugar: mora embaixo da medicao do encoder daquela
  // junta. O encoder conta no eixo do MOTOR, antes do redutor -- a
  // contagem so vira grau da junta passando por ele. Os dois numeros
  // pertencem a mesma conta, e estavam em telas diferentes.
  const semAvulsa = await t.evaluate(() => ({
    afericao: !!document.getElementById('btAfMarcar'),
    reducaoEnc: !!document.getElementById('btRdAplicar'),
    lista: !!document.getElementById('guiaLista'),
    calibrar: !!document.getElementById('btCalIni2'),
  }));
  checar(!semAvulsa.afericao && !semAvulsa.reducaoEnc && !semAvulsa.lista,
         'Calibracao: as telas de afericao avulsa e a lista guiada sairam -- '
         + 'a calibracao mede sozinha');
  checar(semAvulsa.calibrar,
         'Calibracao: e o que sobrou e um botao so -- calibrar');

  await t.locator('#cfgAbas button[data-cfg="encoder"]').click();
  await t.waitForTimeout(500);
  await t.evaluate(() => {
    const alvo = document.getElementById('inRd1').closest('.et');
    document.querySelectorAll('#cfgEncoder .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
    const d = document.getElementById('inRd1').closest('.sub');
    if (d) d.style.display = 'block';
  });
  await t.waitForTimeout(400);
  const red = await t.evaluate(() => {
    const cv1 = document.getElementById('encCv1');
    const rd1 = document.getElementById('inRd1');
    // "Embaixo da medicao": o redutor da junta 1 vem depois das
    // contagens por volta dela, e antes de qualquer coisa da junta 2.
    const pos = cv1.compareDocumentPosition(rd1);
    const cv2 = document.getElementById('encCv2');
    return { depoisDaJ1: !!(pos & Node.DOCUMENT_POSITION_FOLLOWING),
             antesDaJ2: !!(rd1.compareDocumentPosition(cv2) &
                           Node.DOCUMENT_POSITION_FOLLOWING),
             v1: rd1.value };
  });
  checar(red.depoisDaJ1 && red.antesDaJ2,
         'Encoder: o redutor de cada junta fica embaixo da medicao daquela junta',
         JSON.stringify(red));

  // A FAIXA DA BARRA se configura na maquina, em Ajustes: dois campos,
  // um botao, e a barra da aba Mover passa a percorrer o que eles dizem.
  await t.locator('#cfgAbas button[data-cfg="maquina"]').click();
  await t.waitForTimeout(400);
  await t.evaluate(() => {
    const alvo = document.getElementById('inVmn').closest('.et');
    document.querySelectorAll('#cfgMaquina .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(300);
  rotas = [];
  await t.evaluate(() => {
    document.getElementById('inVmn').value = '5';
    document.getElementById('inVmx').value = '90';
  });
  await t.locator('#btFaixaSalvar').click();
  await t.waitForTimeout(400);
  const posFaixa = rotas.find(x => x.split('?')[0] === '/api/config');
  checar(!!posFaixa && /velMin=5/.test(posFaixa) && /velMax=90/.test(posFaixa),
         'Ajustes: o minimo e o maximo da barra de velocidade sao configuraveis',
         posFaixa || 'nada');

  // Faixa invertida e recusada na tela, com motivo.
  await t.evaluate(() => {
    document.getElementById('inVmn').value = '90';
    document.getElementById('inVmx').value = '5';
  });
  rotas = [];
  await t.locator('#btFaixaSalvar').click();
  await t.waitForTimeout(300);
  const motivoFaixa = await t.evaluate(() =>
    document.getElementById('qFaixaSalvar').textContent.trim());
  checar(!rotas.some(x => x.split('?')[0] === '/api/config') &&
         /menor/.test(motivoFaixa),
         'Ajustes: faixa invertida e recusada antes de sair da tela, com motivo',
         motivoFaixa);
  await t.locator('#cfgAbas button[data-cfg="encoder"]').click();
  await t.waitForTimeout(400);
  await t.evaluate(() => {
    const alvo = document.getElementById('inRd1').closest('.et');
    document.querySelectorAll('#cfgEncoder .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
    const d = document.getElementById('inRd1').closest('.sub');
    if (d) d.style.display = 'block';
  });
  await t.waitForTimeout(300);

  rotas = [];
  await t.evaluate(() => { document.getElementById('inRd1').value = '20'; });
  await t.locator('#btRedSalvar').click();
  await t.waitForTimeout(400);
  const posRed = rotas.find(x => x.split('?')[0] === '/api/config');
  checar(!!posRed && /red1=20/.test(posRed),
         'Encoder: salvar o redutor manda so ele, sem carregar o resto junto',
         posRed || 'nada');
  await t.locator('#cfgAbas button[data-cfg="calib"]').click();
  await t.waitForTimeout(500);

  // Area da mesa.
  await t.evaluate(() => {
    const alvo = document.getElementById('btMesaCanto').closest('.et');
    document.querySelectorAll('#cfgCalib .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(300);
  const mesa = await t.evaluate(() => ({
    est: document.getElementById('mesaEstado').textContent.trim(),
    sb: document.getElementById('sbMesa').textContent.trim(),
  }));
  checar(/180/.test(mesa.est) && /640/.test(mesa.est),
         'Calibracao: a area ensinada aparece com os quatro limites', mesa.est);
  checar(/460/.test(mesa.sb) && /520/.test(mesa.sb),
         'Calibracao: e o cabecalho diz o tamanho da mesa', mesa.sb);

  rotas = [];
  await t.locator('#btMesaCanto').click();
  await t.waitForTimeout(350);
  checar(rotas.some(x => x.split('?')[0] === '/api/mesa/canto'),
         'Calibracao: "Gravar canto" chama a rota certa', rotas.join(' '));

  // ENSINAR A MESA NAO EXIGE MAIS CALIBRACAO. Calibrar virou opcional em
  // toda a maquina: o canto e uma coordenada, e a coordenada existe com
  // ou sem limites medidos.
  await t.request.post(BASE + '/teste/estado', { data: { cal1: false } });
  await t.waitForTimeout(600);
  const mesaBloq = await t.evaluate(() => ({
    dis: document.getElementById('btMesaCanto').disabled,
    motivo: document.getElementById('qMesaCanto').textContent.trim(),
  }));
  checar(!mesaBloq.dis && !/calibre/i.test(mesaBloq.motivo),
         'Calibracao: sem limites medidos a mesa continua podendo ser ensinada',
         mesaBloq.motivo || 'sem impedimento');
  await t.request.post(BASE + '/teste/estado', { data: { cal1: true, afer1: 0 } });
  await t.request.post(BASE + '/teste/calibracao',
    { data: { marca1: false, passos1: 0, voltas1: 0 } });
  await fecharGaveta(t);

  // ---- selecionar o eixo tocando no proprio braco ----
  // A cor de cada elo diz se aquela junta tem torque; o anel diz qual
  // esta selecionada. Duas perguntas diferentes, dois sinais diferentes.
  await t.locator('#abas button[data-aba="mesa"]').click();
  await t.waitForTimeout(400);
  await t.request.post(BASE + '/teste/estado',
    { data: { t1: 0, t2: 0, m1ok: false, m2ok: false, srv1: true, srv2: false, sonEst: 2 } });
  await t.waitForTimeout(800);

  // Toca no MEIO do elo 1: da base ao cotovelo, com as duas juntas em 0
  // o braco fica deitado no eixo X.
  const selJ = await t.evaluate(() => {
    const L1 = D.l1 || 200;
    const r = cv.getBoundingClientRect();
    const clique = (mx, my) => {
      const ev = new MouseEvent('click', { bubbles: true,
        clientX: r.left + ox + mx * esc, clientY: r.top + oy - my * esc });
      cv.dispatchEvent(ev);
    };
    clique(L1 * 0.5, 0);            /* meio do elo 1 */
    const a = juntaSel;
    clique(L1 + (D.l2 || 200) * 0.5, 0);   /* meio do elo 2 */
    return { aposElo1: a, aposElo2: juntaSel };
  });
  checar(selJ.aposElo1 === 1 && selJ.aposElo2 === 2,
         'Robo 2D: tocar em cada elo seleciona aquela junta',
         JSON.stringify(selJ));

  // Tocar SOBRE o braco nao pode mandar o robo andar: seria o oposto do
  // que "escolher o eixo" quer dizer.
  rotas = [];
  await t.evaluate(() => {
    const r = cv.getBoundingClientRect();
    cv.dispatchEvent(new MouseEvent('click', { bubbles: true,
      clientX: r.left + ox + (D.l1 || 200) * 0.5 * esc, clientY: r.top + oy }));
  });
  await t.waitForTimeout(300);
  const andou = rotas.filter(x => x.split('?')[0] === '/api/mover_xy');
  checar(andou.length === 0,
         'Robo 2D: tocar no braco escolhe o eixo, nao manda a ponta para la',
         andou.length + ' chamadas a /api/mover_xy');

  // O seletor da aba Mover segue a escolha feita no desenho: um
  // conceito, dois lugares de tocar. Escolhe o eixo 2 por ultimo, que e
  // o que se vai conferir la.
  await t.evaluate(() => {
    const r = cv.getBoundingClientRect();
    cv.dispatchEvent(new MouseEvent('click', { bubbles: true,
      clientX: r.left + ox + ((D.l1 || 200) + (D.l2 || 200) * 0.5) * esc,
      clientY: r.top + oy }));
  });
  await t.waitForTimeout(300);
  await t.locator('#abas button[data-aba="mover"]').click();
  await t.waitForTimeout(300);
  const casou = await t.evaluate(() => document.getElementById('selJunta').value);
  checar(casou === '2', 'Robo 2D: o seletor da aba Mover segue o eixo escolhido no desenho',
         'selJunta = ' + casou);

  // ---- JOG: segurar anda, soltar para ----
  // Foi ensaiado aqui um modo "passo" com incrementos fixos de 1, 5, 10
  // e 30 graus. Saiu a pedido de quem opera: na bancada o que se quer e
  // encostar e ver o braco andar, nao escolher um numero antes. O gesto
  // voltou a ser um so -- segurar anda, soltar para -- e o valor exato
  // vai pelo campo "ir para o angulo", que existe para isso.
  await t.locator('#abas button[data-aba="mover"]').click();
  await t.waitForTimeout(250);
  await t.request.post(BASE + '/teste/estado',
    { data: { t1: 20, t2: 0, m1ok: false, m2ok: false } });
  await t.waitForTimeout(700);
  rotas = [];
  const seta = t.locator('#pnMover .jb').first();
  await seta.dispatchEvent('pointerdown', { pointerId: 5 });
  await t.waitForTimeout(230);
  await seta.dispatchEvent('pointerup', { pointerId: 5 });
  await t.waitForTimeout(180);
  const jogs = rotas.filter(x => x.split('?')[0] === '/api/jog');
  checar(jogs.length >= 2, 'as setas de jog mandam heartbeat e depois o zero',
         jogs.length + ' chamadas a /api/jog');

  // VELOCIDADE EM MILIMETRO POR SEGUNDO.
  // A maquina so entende grau por segundo, mas ninguem na bancada pensa
  // em grau por segundo -- pensa na ponta andando, na mesma unidade do
  // cordao. A conversao e a do braco esticado: R = elo1 + elo2.
  rotas = [];
  await t.evaluate(() => {
    const c = document.getElementById('inVelMm');
    c.value = '300';
    c.dispatchEvent(new Event('change', { bubbles: true }));
  });
  await t.waitForTimeout(400);
  const posVel = rotas.find(x => x.split('?')[0] === '/api/config');
  const grausEsperado = 300 * 180 / (Math.PI * (450 + 400));
  const lidoN = posVel && +(/velN=([\d.]+)/.exec(posVel) || [])[1];
  const lidoA = posVel && +(/velA=([\d.]+)/.exec(posVel) || [])[1];
  checar(!!posVel && Math.abs(lidoN - grausEsperado) < 0.6,
         'Velocidade: digitar em mm/s vira grau/s pelo alcance do braco',
         posVel + ' (esperado ~' + grausEsperado.toFixed(1) + ')');
  // Jog e "ir para um angulo" sao o mesmo gesto para quem opera. Ate aqui
  // obedeciam a dois campos diferentes, e subir a barra do jog deixava o
  // posicionamento lerdo do mesmo jeito.
  checar(lidoA === lidoN,
         'Velocidade: o mesmo ajuste vale para o jog e para o ir-para-angulo',
         'velN=' + lidoN + ', velA=' + lidoA);

  // E a linha de baixo diz a mesma velocidade na unidade da maquina --
  // com o alcance usado na conta guardado no titulo, para explicar o
  // numero sem quebrar a linha de quem so quer operar.
  const eq = await t.evaluate(() => {
    const b = document.getElementById('velMovTx');
    return { txt: b.textContent, tit: b.title };
  });
  checar(/\u00b0\/s/.test(eq.txt) && /850/.test(eq.tit),
         'Velocidade: a equivalencia em grau/s fica a vista, e o alcance usado no titulo',
         eq.txt + ' | ' + eq.tit);

  // Os tres degraus repartem a FAIXA configurada na maquina, nao um teto
  // cravado no codigo: numa maquina cujo maximo util e 20 graus/s,
  // "rapido" tem de ser 20, e nao um numero que ela nunca alcanca.
  rotas = [];
  await t.locator('[data-vel="1"]').click();
  await t.waitForTimeout(400);
  const rapido = rotas.find(x => x.split('?')[0] === '/api/config');
  const gRapido = rapido && +(/velN=([\d.]+)/.exec(rapido) || [])[1];
  checar(!!rapido && Math.abs(gRapido - 60) < 0.6,
         'Velocidade: o atalho "rapido" leva ao maximo da faixa configurada',
         'velN=' + gRapido + ' (faixa 2..60)');

  // E a barra percorre exatamente essa faixa.
  const faixaVel = await t.evaluate(() => {
    const b = document.getElementById('inVelMov');
    return { min: b.min, max: b.max };
  });
  checar(+faixaVel.min === 2 && +faixaVel.max === 60,
         'Velocidade: a barra percorre a faixa que a maquina publica',
         faixaVel.min + ' a ' + faixaVel.max + ' °/s');

  // Maquina sem calibracao e sem servos: o que bloqueia tem que dizer.
  await t.request.post(BASE + '/teste/estado',
    { data: { cal1: false, cal2: false, servos: false, progN: 0, trajN: 0 } });
  await t.waitForTimeout(600);
  const bloqueios = await t.evaluate(() => {
    const r = {};
    ['Home', 'Ensaio', 'Soldar', 'Repro', 'Mover'].forEach(k => {
      const b = document.getElementById('bt' + k), q = document.getElementById('q' + k);
      r[k] = { dis: b ? b.disabled : null, motivo: q ? q.textContent.trim() : '' };
    });
    return r;
  });
  const semMotivo = Object.entries(bloqueios).filter(([, v]) => v.dis && !v.motivo).map(([k]) => k);
  checar(semMotivo.length === 0,
         'sem servos e sem calibracao, cada acao bloqueada diz o porque',
         Object.entries(bloqueios).map(([k, v]) => k + ': ' + (v.motivo || (v.dis ? 'MUDO' : 'liberado'))).join(' | '));
  const joyBloq = await t.evaluate(() => ({
    dim: document.getElementById('joy').classList.contains('bloq'),
    motivo: document.getElementById('joyMotivo').textContent.trim(),
  }));
  checar(joyBloq.dim && joyBloq.motivo.length > 0,
         'o joystick mostra que esta bloqueado, em vez de parecer pronto',
         'apagado: ' + joyBloq.dim + ' | "' + joyBloq.motivo + '"');
  await t.screenshot({ path: SAIDA + '/celular-4-bloqueios.png' });

  await t.request.post(BASE + '/teste/estado',
    { data: { cal1: true, cal2: true, servos: true, progN: 3, trajN: 24 } });
  await t.waitForTimeout(500);


  // ------------------------------------------------------------------
  // Producao: pausar, repetir, desfazer, e o arco em DOIS toques.
  // ------------------------------------------------------------------
  await t.request.post(BASE + '/teste/estado',
    { data: { modo: 'EXECUTANDO', progN: 3, progIdx: 1, trecho: 42,
              ciclos: 137, cicSes: 9, pausa: false, desf: true } });
  await t.waitForTimeout(600);
  await t.locator('#abas button[data-aba="prog"]').click();
  await t.waitForTimeout(250);
  await t.evaluate(() => {
    const alvo = document.getElementById('btSoldar').closest('.et');
    document.querySelectorAll('#pnProg .et').forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(250);

  const contando = await t.evaluate(() => document.getElementById('contPecas').textContent.trim());
  checar(/137/.test(contando) && /9/.test(contando),
         'Producao: a contagem de pecas aparece durante a execucao', contando);

  rotas = [];
  await t.locator('#btPausa').click();
  await t.waitForTimeout(300);
  const pausou = rotas.find(x => x.split('?')[0] === '/api/prog/pausar');
  checar(!!pausou && /on=1/.test(pausou), 'Producao: Pausar chama a rota certa',
         pausou || 'nada');

  await t.request.post(BASE + '/teste/estado', { data: { pausa: true } });
  await t.waitForTimeout(600);
  const pausado = await t.evaluate(() => ({
    bt: document.getElementById('btPausa').textContent.trim(),
    cont: document.getElementById('contPecas').textContent.trim(),
  }));
  checar(/Retomar/.test(pausado.bt), 'Producao: pausado, o mesmo botao vira Retomar', pausado.bt);
  checar(/42%/.test(pausado.cont) && /2.*3/.test(pausado.cont),
         'Producao: pausado, a tela diz em que trecho e a que altura dele parou',
         pausado.cont);

  rotas = [];
  await t.locator('#btPausa').click();
  await t.waitForTimeout(300);
  checar(rotas.some(x => /\/api\/prog\/pausar\?on=0/.test(x)),
         'Producao: Retomar volta pela mesma rota com on=0', rotas.join(' '));

  // O arco em dois toques. O primeiro NAO pode executar.
  await t.request.post(BASE + '/teste/estado',
    { data: { modo: 'MANUAL', pausa: false } });
  await t.waitForTimeout(600);
  rotas = [];
  await t.locator('#btSoldar').click();
  await t.waitForTimeout(250);
  const armado = await t.evaluate(() => document.getElementById('btSoldar').textContent.trim());
  checar(!rotas.some(x => x.startsWith('/api/prog/executar')),
         'Arco: o primeiro toque NAO executa nada', rotas.join(' ') || 'nenhuma rota');
  checar(/Confirmar/i.test(armado),
         'Arco: o primeiro toque arma o botao e ele diz que esta armado', armado);

  rotas = [];
  await t.locator('#btSoldar').click();
  await t.waitForTimeout(300);
  const soldou = rotas.find(x => x.split('?')[0] === '/api/prog/executar');
  checar(!!soldou && /ensaio=0/.test(soldou) && /conf=1/.test(soldou),
         'Arco: o segundo toque executa, e a confirmacao vai na requisicao',
         soldou || 'nada');

  rotas = [];
  await t.locator('#btRepetir').click();
  await t.waitForTimeout(300);
  const repetiu = rotas.find(x => x.split('?')[0] === '/api/prog/repetir');
  checar(!!repetiu && /conf=1/.test(repetiu),
         'Producao: "Mais uma peca" tambem exige confirmacao na requisicao',
         repetiu || 'nada');

  // ------------------------------------------------------------------
  // Leitura de angulo: comandado e medido, lado a lado.
  // ------------------------------------------------------------------
  await t.request.post(BASE + '/teste/estado',
    { data: { t1: 30.0, m1: 30.04, m1ok: true, t2: -14.4, m2: -12.1, m2ok: true } });
  await t.waitForTimeout(600);
  const ang = await t.evaluate(() => ({
    m1: document.getElementById('hM1').textContent.trim(),
    c1: document.getElementById('hM1').className,
    m2: document.getElementById('hM2').textContent.trim(),
    c2: document.getElementById('hM2').className,
  }));
  checar(/30\.04/.test(ang.m1) && !/dif/.test(ang.c1),
         'Angulo: a leitura do encoder aparece junto do comandado', ang.m1);
  checar(/12\.1/.test(ang.m2) && /dif/.test(ang.c2),
         'Angulo: divergencia grande fica marcada, a pequena nao', ang.m2 + ' | ' + ang.c2);

  await t.request.post(BASE + '/teste/estado', { data: { m1ok: false } });
  await t.waitForTimeout(600);
  const semLeitura = await t.evaluate(() => document.getElementById('hM1').textContent.trim());
  checar(/sem leitura/i.test(semLeitura),
         'Angulo: sem encoder a tela diz "sem leitura" em vez de mostrar zero',
         semLeitura);
  await t.request.post(BASE + '/teste/estado', { data: { m1ok: true } });

  // ------------------------------------------------------------------
  // Aba Maquina: saude, registro, QR e modo operador.
  // ------------------------------------------------------------------
  await abrirGaveta(t, 'sistema');
  await t.waitForTimeout(700);
  const saude = await t.evaluate(() => ({
    n: document.querySelectorAll('#saudeG .sl').length,
    txt: document.getElementById('saudeG').textContent,
    sb: document.getElementById('sbSaude').textContent.trim(),
  }));
  checar(saude.n >= 12, 'Maquina: a tela de saude lista os indicadores',
         saude.n + ' linhas');
  checar(/pecas|Pecas/.test(saude.txt) && /Encoder/.test(saude.txt),
         'Maquina: producao e encoder estao entre eles', saude.sb);

  const reg = await t.evaluate(() => document.getElementById('regLista').textContent.trim());
  checar(reg.length > 0, 'Maquina: o registro de eventos aparece', reg.slice(0, 60));

  // Os dois QR tem de ser DESENHADOS, nao so existir como canvas vazio.
  await t.evaluate(() => {
    const alvo = document.getElementById('qrRede').closest('.et');
    document.querySelectorAll('#cfgSistema .et').forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(500);
  const qr = await t.evaluate(() => {
    const r = {};
    ['qrRede', 'qrPainel'].forEach(id => {
      const cv = document.getElementById(id);
      const d = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height).data;
      let escuros = 0;
      for (let i = 0; i < d.length; i += 4) if (d[i] < 128) escuros++;
      r[id] = escuros;
    });
    return r;
  });
  checar(qr.qrRede > 500 && qr.qrPainel > 500,
         'Maquina: os dois codigos QR sao realmente desenhados',
         'modulos escuros: rede ' + qr.qrRede + ', painel ' + qr.qrPainel);

  // O modo operador foi REMOVIDO. Ele escondia as abas de instalacao
  // atras de uma senha que nao era seguranca de rede (quem esta no Wi-Fi
  // da maquina alcanca a API direto), e mantinha um estado a mais que
  // toda tela precisava consultar. Nesta maquina ninguem usava.
  const semOperador = await t.evaluate(() => ({
    botao:  !!document.getElementById('btOp'),
    senha:  !!document.getElementById('opAtual'),
    classe: document.body.classList.contains('operador'),
    abas:   [...document.querySelectorAll('#cfgAbas button')]
              .filter(b => !!b.offsetParent).length,
  }));
  checar(!semOperador.botao && !semOperador.senha && !semOperador.classe,
         'Sem modo operador: nem botao, nem senha, nem estado sobrando',
         JSON.stringify(semOperador));
  checar(semOperador.abas === 4,
         'Sem modo operador: as quatro paginas da gaveta ficam sempre a mao',
         semOperador.abas + ' paginas visiveis');

  // Idioma: portugues e ingles, e so, com portugues de padrao.
  const idioma = await t.evaluate(() => ({
    botao: document.getElementById('btIdioma').textContent.trim(),
    sub:   document.getElementById('sbIdioma').textContent.trim(),
  }));
  checar(idioma.botao === 'English' && idioma.sub === 'portugues',
         'Idioma: o padrao e portugues, e o unico outro e ingles',
         JSON.stringify(idioma));


  // ------------------------------------------------------------------
  // CALIBRACAO GUIADA: quatro passos, na ordem que importa.
  // O pedido era "uma calibracao automatica guiada"; o que a maquina
  // precisa de fato e sentido, reducao, curso e mesa -- nessa ordem,
  // porque cada um usa o anterior. O cartao nao refaz nenhum deles: diz
  // qual e o proximo e abre quem faz o trabalho.
  // ------------------------------------------------------------------
  // O passo do sentido nao tem medida: e uma conferencia, e a marca dela
  // vive no navegador. Limpa-se antes para o cenario comecar com um
  // passo pendente de verdade -- senao a maquina do banco ja nasce toda
  // ------------------------------------------------------------------
  // AJUSTES DA MAQUINA em linguagem de operador.
  // A queixa: "queria algo mais simples, de forma que um operador nao
  // experiente consiga entender no que ele esta mexendo". Os numeros
  // continuam todos la -- o que mudou e a ordem: tres botoes na frente,
  // graus por segundo ao quadrado atras de "Ajustar".
  // ------------------------------------------------------------------
  // O btCfg ALTERNA: clicar nele com a gaveta ja aberta a fecha, e o
  // clique seguinte cai num botao invisivel.
  await t.evaluate(() => {
    if (!document.getElementById('veuCfg').classList.contains('on'))
      document.getElementById('btCfg').click();
  });
  await t.waitForTimeout(300);
  await t.locator('#cfgAbas button[data-cfg="maquina"]').click();
  await t.waitForTimeout(250);
  await t.evaluate(() => {
    const alvo = document.getElementById('segVel').closest('.et');
    document.querySelectorAll('#cfgMaquina .et')
      .forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(600);

  const aj = await t.evaluate(() => ({
    velAceso: [...document.querySelectorAll('#segVel button')]
                .filter(b => b.classList.contains('on')).map(b => b.dataset.v),
    rampaAceso: [...document.querySelectorAll('#segRampa button')]
                .filter(b => b.classList.contains('on')).map(b => b.dataset.r),
    numerosVel: !!document.getElementById('velCustom').offsetParent,
    avancado: !!document.getElementById('avancado').offsetParent,
    resumo: document.getElementById('resumoVel').textContent.trim(),
    sub: document.getElementById('sbAjustes').textContent.trim(),
  }));
  checar(aj.velAceso.length === 1 && aj.rampaAceso.length === 1,
         'Ajustes: a tela abre com a velocidade e a partida atuais ACESAS',
         'velocidade: ' + aj.velAceso + ', partida: ' + aj.rampaAceso);
  checar(!aj.numerosVel && !aj.avancado,
         'Ajustes: os numeros crus comecam recolhidos, nao apagados',
         JSON.stringify({ velCustom: aj.numerosVel, avancado: aj.avancado }));
  checar(/°\/s/.test(aj.resumo) && /mm\/s/.test(aj.resumo),
         'Ajustes: e o resumo em uma linha diz o que aqueles botoes valem',
         aj.resumo);
  checar(aj.sub.length > 0,
         'Ajustes: o cabecalho do cartao ja adianta em que pe esta', aj.sub);

  // Escolher um preset grava de verdade -- nao e so pintar botao.
  rotas = [];
  await t.locator('#segVel button[data-v="lento"]').click();
  await t.waitForTimeout(500);
  const gravou = rotas.find(u => u.indexOf('/api/config?') === 0);
  checar(!!gravou && /velN=8/.test(gravou) && /velCordao=3/.test(gravou),
         'Ajustes: escolher "Lento" grava os quatro numeros de uma vez',
         gravou);

  // "Ajustar" revela os campos, e nao mexe em nada sozinho.
  rotas = [];
  await t.locator('#segVel button[data-v="custom"]').click();
  await t.waitForTimeout(400);
  const custom = await t.evaluate(() => ({
    visivel: !!document.getElementById('velCustom').offsetParent,
    campos: ['inVn', 'inVp', 'inVa', 'inVc2'].every(i =>
      !!document.getElementById(i) && document.getElementById(i).value !== ''),
  }));
  checar(custom.visivel && custom.campos &&
         !rotas.some(u => u.indexOf('/api/config?') === 0),
         'Ajustes: "Ajustar" so mostra os campos -- nao grava sozinho',
         JSON.stringify(custom));

  // O "Avancado" existe e abre. Era o pedido oposto ao de simplificar:
  // simples na frente, completo atras -- nada some da maquina.
  await t.locator('#hAvancado').click();
  await t.waitForTimeout(300);
  const av = await t.evaluate(() => ({
    aberto: !!document.getElementById('avancado').offsetParent,
    temResolucao: !!document.getElementById('inPv1').offsetParent,
    temMargens: !!document.getElementById('inEy').offsetParent,
    temSentido: !!document.getElementById('sInv1').offsetParent,
  }));
  checar(av.aberto && av.temResolucao && av.temMargens && av.temSentido,
         'Ajustes: o "Avancado" guarda resolucao, sentido e margens -- nada sumiu',
         JSON.stringify(av));

  // As protecoes viraram uma frase antes de virarem tres chaves.
  const prot = await t.evaluate(() =>
    document.getElementById('resumoArea').textContent.trim());
  checar(/protec/.test(prot),
         'Ajustes: as protecoes se resumem em uma frase antes das chaves', prot);

  await t.locator('#cfgFechar').click();
  await t.waitForTimeout(250);

  // ------------------------------------------------------------------
  // Biblioteca de pecas: ver a miniatura ANTES de carregar.
  // ------------------------------------------------------------------
  await t.locator('#abas button[data-aba="arq"]').click();
  await t.waitForTimeout(500);
  await t.evaluate(() => {
    const alvo = document.getElementById('sdLista').closest('.et');
    document.querySelectorAll('#pnArq .et').forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(400);
  rotas = [];
  await t.locator('#sdLista [data-ver]').first().click();
  await t.waitForTimeout(900);
  const pediuPrevia = rotas.some(x => x.split('?')[0] === '/api/sd/prever');
  checar(pediuPrevia, 'Biblioteca: "ver" pede a previa e NAO carrega a peca',
         rotas.join(' ') || 'nada');
  checar(!rotas.some(x => x.split('?')[0] === '/api/sd/carregar'),
         'Biblioteca: ver uma peca nao troca a que esta na maquina');

  const pv = await t.evaluate(() => {
    const cv = document.getElementById('pvTela');
    const d = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height).data;
    let pintados = 0;
    for (let i = 3; i < d.length; i += 4) if (d[i] > 0) pintados++;
    return { aberto: document.getElementById('veuPeca').classList.contains('on'),
             pintados: pintados,
             info: document.getElementById('pvInfo').textContent.trim(),
             aviso: document.getElementById('pvAviso').textContent.trim() };
  });
  checar(pv.aberto && pv.pintados > 300,
         'Biblioteca: a miniatura da peca e realmente desenhada',
         pv.pintados + ' pixels pintados');
  checar(/4 pontos/.test(pv.info) && /cordao/.test(pv.info),
         'Biblioteca: e diz quantos pontos e quantos cordoes', pv.info);
  checar(/380/.test(pv.aviso) && /450/.test(pv.aviso),
         'Biblioteca: peca feita com outros elos ACENDE o aviso, com os dois numeros',
         pv.aviso.slice(0, 90));

  // Peca da propria maquina: nenhum aviso.
  await t.request.post(BASE + '/teste/previa', { data: { l1: 450.0, l2: 400.0 } });
  await t.locator('#pvFechar').click();
  await t.waitForTimeout(200);
  await t.locator('#sdLista [data-ver]').first().click();
  await t.waitForTimeout(900);
  const semAviso = await t.evaluate(() =>
    document.getElementById('pvAviso').style.display);
  checar(semAviso === 'none',
         'Biblioteca: peca feita nesta maquina nao acende aviso nenhum',
         'display: ' + semAviso);

  rotas = [];
  await t.locator('#pvCarregar').click();
  await t.waitForTimeout(400);
  checar(rotas.some(x => /\/api\/sd\/carregar/.test(x)),
         'Biblioteca: dali mesmo da para carregar a peca vista', rotas.join(' '));


  // ------------------------------------------------------------------
  // Aprendizado guiado. Os tres passos sao o mesmo caminho que ja
  // existia -- entrar, gravar, sair -- so que na ordem em que acontecem
  // e no mesmo cartao. O botao de gravar morava na aba Mover: ensinar um
  // cordao obrigava a trocar de aba entre cada ponto, com a mao no braco.
  // ------------------------------------------------------------------
  await t.request.post(BASE + '/teste/estado', { data: { apr: false, aprSolto: false, aprN: 0 } });
  await t.evaluate(() => irAba('prog'));
  await t.waitForTimeout(700);
  await t.evaluate(() => {
    const a = document.getElementById('btApr');
    if (a) a.closest('.et').classList.add('aberta');
  });
  await t.waitForTimeout(300);

  const guiaFora = await t.evaluate(() => {
    const g = document.getElementById('aprGuia');
    return { passos: g ? g.querySelectorAll('.gp').length : 0,
             agora: g ? g.querySelectorAll('.gp.agora').length : 0,
             marcarTravado: document.getElementById('btAprMarcar').disabled };
  });
  checar(guiaFora.passos === 3 && guiaFora.agora === 1,
         'Aprendizado: tres passos, e exatamente UM apontado como o de agora',
         JSON.stringify(guiaFora));
  checar(guiaFora.marcarTravado,
         'Aprendizado: fora do modo, marcar ponto fica travado e diz por que',
         String(guiaFora.marcarTravado));

  rotas = [];
  await t.locator('#btApr').click();
  await t.waitForTimeout(500);
  const entrou = rotas.find(x => x.split('?')[0] === '/api/aprender');
  checar(!!entrou && /on=1/.test(entrou),
         'Aprendizado: o passo 1 solta o braco', entrou || rotas.join(' '));

  await t.request.post(BASE + '/teste/estado', { data: { apr: true, aprSolto: true, aprN: 1 } });
  await t.waitForTimeout(800);
  const dentro = await t.evaluate(() => {
    const g = document.getElementById('aprGuia');
    return { txt: g ? g.textContent : '',
             marcarLivre: !document.getElementById('btAprMarcar').disabled };
  });
  checar(dentro.marcarLivre,
         'Aprendizado: dentro do modo, marcar ponto libera');
  checar(/fim do cordao/i.test(dentro.txt),
         'Aprendizado: com um ponto marcado, o guia pede o proximo em palavras',
         dentro.txt.replace(/\s+/g, ' ').slice(0, 90));

  rotas = [];
  await t.locator('#btAprMarcar').click();
  await t.waitForTimeout(500);
  const marcou = rotas.find(x => x.split('?')[0] === '/api/ponto/gravar');
  checar(!!marcou, 'Aprendizado: marcar grava o ponto onde a ponta esta',
         marcou || rotas.join(' '));

  rotas = [];
  await t.locator('#btAprFim').click();
  await t.waitForTimeout(500);
  const aprSaiu = rotas.find(x => x.split('?')[0] === '/api/aprender');
  checar(!!aprSaiu && /on=0/.test(aprSaiu),
         'Aprendizado: e o passo 3 encerra', aprSaiu || rotas.join(' '));

  await t.request.post(BASE + '/teste/estado', { data: { apr: false, aprSolto: false, aprN: 0 } });
  await t.waitForTimeout(600);

  // ------------------------------------------------------------------
  // Fluidez do desenho. O /api/status chega a cada 220 ms e o desenho
  // roda por quadro: sem suavizacao o braco repetia a mesma pose varias
  // vezes e SALTAVA -- uns 4,5 quadros por segundo de movimento, que e o
  // "parece que esta travando". O robo nao anda aos saltos; era o
  // desenho que mostrava assim.
  // ------------------------------------------------------------------
  // Amostra o desenho DURANTE a transicao, pelo caminho de verdade: um
  // valor novo chega do /api/status e os quadros seguintes tem de passar
  // por angulos no meio, em vez de saltar.
  // O desenho so segue o ENCODER (ja passado pela reducao) -- nunca o
  // comandado. m1 e o angulo medido, m1ok diz que a leitura vale.
  await t.request.post(BASE + '/teste/estado', { data: { m1: 0, m1ok: true, m2ok: false } });
  await t.waitForTimeout(900);

  await t.evaluate(() => {
    window.__am = [];
    window.__amOn = true;
    (function laco(){
      if (!window.__amOn) return;
      window.__am.push(postura().t1);
      requestAnimationFrame(laco);
    })();
  });
  await t.request.post(BASE + '/teste/estado', { data: { m1: 10 } });
  await t.waitForTimeout(1400);
  const am = await t.evaluate(() => { window.__amOn = false; return window.__am; });

  const meio = am.filter(v => v > 0.05 && v < 9.9).length;
  checar(meio >= 2,
         'Movimento: o braco desenhado glisa entre as amostras em vez de saltar',
         meio + ' quadros no meio do caminho, de ' + am.length);
  checar(am.length && am[am.length - 1] === 10,
         'Movimento: e encosta no valor de verdade, sem sobra permanente',
         String(am[am.length - 1]));

  // O DESENHO SO OBEDECE AO ENCODER. Perder a leitura no meio do
  // movimento nao pode fazer o braco "voltar" a seguir o comandado --
  // ele tem de CONGELAR na ultima postura que de fato foi medida. Se
  // voltasse a seguir o comandado, um driver sem encoder no barramento
  // desenharia uma posicao que ninguem mediu, e um passo perdido nunca
  // apareceria no desenho.
  await t.request.post(BASE + '/teste/estado', { data: { t1: 47, m1ok: false } });
  await t.waitForTimeout(400);
  const congelado = await t.evaluate(() => postura().t1);
  checar(Math.abs(congelado - 10) < 0.5,
         'Movimento: sem encoder o desenho congela na ultima medida, ignora o comandado',
         'desenhado=' + congelado + ' (comandado foi para 47)');

  // E quando a leitura volta, o desenho retoma dali -- do encoder, nunca
  // do comandado que ficou mudando enquanto a leitura estava fora.
  // (8, nao 15: tem de ficar fora da faixa 11-89 que o proximo teste usa
  // para reconhecer "isto e um salto, nao um deslizar".)
  await t.request.post(BASE + '/teste/estado', { data: { m1: 8, m1ok: true } });
  await t.waitForTimeout(700);
  const retomou = await t.evaluate(() => postura().t1);
  checar(Math.abs(retomou - 8) < 0.5,
         'Movimento: a leitura volta e o desenho retoma do encoder, nao do comandado',
         'desenhado=' + retomou);

  // Salto que NAO e movimento -- zerar a maquina, recuperar posicao pelo
  // encoder -- nao pode ser glisado: o braco nao percorreu aquele
  // caminho, e desenha-lo seria mostrar movimento que nao houve.
  await t.evaluate(() => {
    window.__am2 = [];
    window.__amOn2 = true;
    (function laco(){
      if (!window.__amOn2) return;
      window.__am2.push(postura().t1);
      requestAnimationFrame(laco);
    })();
  });
  await t.request.post(BASE + '/teste/estado', { data: { m1: 90 } });
  await t.waitForTimeout(1000);
  const am2 = await t.evaluate(() => { window.__amOn2 = false; return window.__am2; });
  const meio2 = am2.filter(v => v > 11 && v < 89).length;
  checar(meio2 === 0,
         'Movimento: mudanca de referencial pula direto, sem desenhar percurso',
         meio2 + ' quadros no meio (tem de ser zero)');

  // O desenho tem de andar no ritmo do monitor, nao num temporizador
  // fixo que nao se alinha com os quadros.
  const porQuadro = await t.evaluate(async () => {
    let n = 0;
    const antes = pintar;
    window.pintar = function(){ n++; return antes.apply(this, arguments); };
    await new Promise(r => setTimeout(r, 350));
    window.pintar = antes;
    return n;
  });
  checar(porQuadro >= 8,
         'Movimento: o desenho roda por quadro do monitor, nao a cada 45 ms',
         porQuadro + ' quadros em 350 ms');

  await t.request.post(BASE + '/teste/estado', { data: { t1: 0, t2: 0 } });
  await t.waitForTimeout(700);

  // ------------------------------------------------------------------
  // Os botoes do motor no cabecalho. Ligar e desligar torque e a coisa
  // que mais se aperta na maquina, e estava enterrada numa gaveta de
  // Ajustes. Sao DOIS porque cada driver e um escravo Modbus proprio:
  // com um driver so na bancada, exigir os dois nao habilitava nada.
  // ------------------------------------------------------------------
  await t.request.post(BASE + '/teste/estado', { data: { srv1: true, srv2: true, servos: true, sonReg: 98, sonEst: 2 } });
  await t.waitForTimeout(900);
  const dois = await t.evaluate(() => {
    const a = document.getElementById('btMotor1'), b = document.getElementById('btMotor2');
    return a && b ? { a: a.className, b: b.className, txt: a.textContent.trim(),
                      visivel: a.offsetParent !== null } : null;
  });
  checar(!!dois && dois.visivel && / on\b/.test(dois.a) && / on\b/.test(dois.b),
         'Motor: ha um botao por eixo no cabecalho, verdes com torque',
         dois ? dois.a + ' | ' + dois.b : 'sem botoes');

  rotas = [];
  await t.locator('#btMotor1').click();
  await t.waitForTimeout(600);
  const so1 = rotas.find(x => x.split('?')[0] === '/api/servos');
  checar(!!so1 && /v=0/.test(so1) && /j=1/.test(so1),
         'Motor: o botao do eixo 1 manda a junta 1, e so ela',
         so1 || rotas.join(' '));

  // O caso da bancada dele: um driver so. O eixo 1 tem torque, o 2 nao.
  await t.request.post(BASE + '/teste/estado', { data: { srv1: true, srv2: false, servos: false } });
  await t.waitForTimeout(900);
  const umSo = await t.evaluate(() => ({
    a: document.getElementById('btMotor1').className,
    b: document.getElementById('btMotor2').className,
  }));
  checar(/ on\b/.test(umSo.a) && !/ on\b/.test(umSo.b),
         'Motor: com um driver so, o eixo que tem torque aparece ligado e o outro nao',
         umSo.a + ' | ' + umSo.b);

  rotas = [];
  await t.locator('#btMotor2').click();
  await t.waitForTimeout(600);
  const liga2 = rotas.find(x => x.split('?')[0] === '/api/servos');
  checar(!!liga2 && /v=1/.test(liga2) && /j=2/.test(liga2),
         'Motor: e o eixo sem torque oferece LIGAR, nao desligar',
         liga2 || rotas.join(' '));

  await t.request.post(BASE + '/teste/estado', { data: { sonEst: 1 } });
  await t.waitForTimeout(900);
  const indo = await t.evaluate(() => document.getElementById('btMotor1').className);
  checar(/indo/.test(indo),
         'Motor: enquanto o barramento nao confirma, o botao nao diz que ligou', indo);

  await t.request.post(BASE + '/teste/estado', { data: { sonEst: 2, srv1: true, srv2: true, servos: true } });
  await t.waitForTimeout(700);

  // ------------------------------------------------------------------
  // O habilita (SON) pelo barramento. O fio do GPIO 23 saiu, e o que
  // ficou no lugar tem de estar alcancavel pela tela -- registrador
  // errado aqui e maquina que nao energiza, ou pior, energiza escrevendo
  // em parametro que ninguem queria.
  // ------------------------------------------------------------------
  await abrirGaveta(t, 'encoder');
  await t.waitForTimeout(400);
  await t.evaluate(() => {
    document.querySelectorAll('#cfgEncoder .et').forEach(x => x.classList.add('aberta'));
    document.querySelectorAll('#cfgEncoder h4.dobra').forEach(h => h.classList.add('aberta'));
    document.querySelectorAll('#cfgEncoder .sub').forEach(d => { d.style.display = 'block'; });
  });
  await t.waitForTimeout(300);

  const campos = await t.evaluate(() => ({
    reg: document.getElementById('sonReg') ? document.getElementById('sonReg').value : null,
    liga: document.getElementById('sonL') ? document.getElementById('sonL').value : null,
    desl: document.getElementById('sonD') ? document.getElementById('sonD').value : null,
    temF16: !!document.getElementById('sonF16'),
    temBotao: !!document.getElementById('btSonSalvar'),
  }));
  checar(campos.temBotao && campos.temF16,
         'Habilita: o bloco do SON existe na tela, com registrador e funcao',
         JSON.stringify(campos));
  checar(campos.reg === '98' && campos.liga === '1' && campos.desl === '0',
         'Habilita: os campos vem preenchidos com o que a maquina respondeu',
         'reg=' + campos.reg + ' liga=' + campos.liga + ' desl=' + campos.desl);

  rotas = [];
  await t.locator('#btSonSalvar').click();
  await t.waitForTimeout(600);
  const salvouSon = rotas.find(x => x.split('?')[0] === '/api/son/config');
  checar(!!salvouSon && /reg=98/.test(salvouSon) &&
         /liga=1/.test(salvouSon) && /desl=0/.test(salvouSon),
         'Habilita: salvar manda registrador e os dois valores para a maquina',
         salvouSon || rotas.join(' '));

  // Registrador 0 = nao configurado. A tela tem de dizer isso, senao o
  // operador aperta "habilitar servos" e nao entende por que nada acontece.
  await t.request.post(BASE + '/teste/estado', { data: { sonReg: 0 } });
  await t.waitForTimeout(900);
  const semReg = await t.evaluate(() => {
    const q = document.getElementById('qSon');
    return { txt: q ? q.textContent.trim() : '', cls: q ? q.className : '' };
  });
  checar(/registrador/i.test(semReg.txt) && /ruim/.test(semReg.cls),
         'Habilita: sem registrador a tela avisa, em vez de ficar muda',
         semReg.txt);

  // O caso grave: o barramento nao confirmou. Nao da para deixar isso
  // so no log -- e o unico caminho que existe para tirar torque.
  await t.request.post(BASE + '/teste/estado', { data: { sonReg: 98, sonEst: 3 } });
  await t.waitForTimeout(900);
  const naoConfirmou = await t.evaluate(() => {
    const q = document.getElementById('qSon');
    return { txt: q ? q.textContent.trim() : '', cls: q ? q.className : '' };
  });
  checar(/confirm/i.test(naoConfirmou.txt) && /ruim/.test(naoConfirmou.cls),
         'Habilita: barramento que nao confirmou aparece na tela',
         naoConfirmou.txt);

  await t.request.post(BASE + '/teste/estado', { data: { sonReg: 98, sonEst: 2 } });
  await t.waitForTimeout(700);

  // ------------------------------------------------------------------
  // Idiomas. O que interessa e a ida E a volta: uma traducao que nao
  // desfaz deixa a maquina em ingles para sempre.
  // ------------------------------------------------------------------
  await abrirGaveta(t, 'sistema');
  await t.waitForTimeout(500);
  await t.evaluate(() => {
    const alvo = document.getElementById('btIdioma').closest('.et');
    document.querySelectorAll('#cfgSistema .et').forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(300);
  await t.locator('#btIdioma').click();
  await t.waitForTimeout(1200);
  const ingles = await t.evaluate(() => ({
    abas: [...document.querySelectorAll('#abas button span')].map(e => e.textContent.trim()),
    eixo: document.getElementById('btMotor1').textContent.trim(),
    idioma: (() => { try { return localStorage.getItem('idioma'); } catch (e) { return null; } })(),
  }));
  checar(ingles.abas.includes('Program') && ingles.abas.includes('Table'),
         'Idioma: as abas ficam em ingles', ingles.abas.join(' '));
  checar(/AXIS/.test(ingles.eixo),
         'Idioma: os botoes principais tambem', ingles.eixo);
  checar(ingles.idioma === 'en', 'Idioma: a escolha fica gravada no navegador');

  // As notas longas NAO sao traduzidas -- e proposital, e a tela nao
  // pode ficar meio traduzida por acidente.
  const nota1 = await t.evaluate(() => {
    const n = document.querySelector('#pnProg .nt');
    return n ? n.textContent.slice(0, 60) : '';
  });
  // A propriedade: as notas NAO sao traduzidas -- seguem em portugues
  // mesmo com a tela em ingles. Amarrar em palavras especificas fez este
  // guarda reprovar quando as notas foram enxugadas; o que vale e nao
  // haver ingles nelas.
  checar(!/\b(the|with|only|arm|axis|point)\b/i.test(nota1) && nota1.length > 0,
         'Idioma: as notas longas seguem em portugues, como documentado',
         nota1.slice(0, 50));

  await abrirGaveta(t, 'sistema');
  await t.waitForTimeout(400);
  await t.evaluate(() => {
    const alvo = document.getElementById('btIdioma').closest('.et');
    document.querySelectorAll('#cfgSistema .et').forEach(x => x.classList.toggle('aberta', x === alvo));
  });
  await t.waitForTimeout(300);
  await t.locator('#btIdioma').click();
  await t.waitForTimeout(1200);
  const voltou = await t.evaluate(() =>
    [...document.querySelectorAll('#abas button span')].map(e => e.textContent.trim()));
  checar(voltou.includes('Programa') && voltou.includes('Mesa'),
         'Idioma: e a volta para o portugues funciona', voltou.join(' '));

  checar(errosT.length === 0, 'nenhum erro de JavaScript em toda a varredura',
         errosT.length ? errosT.slice(0, 3).join(' | ') : 'console limpo');

  await browser.close();
  console.log('\n\x1b[1mINTERFACE: ' + passa + ' passaram, ' + falha + ' falharam\x1b[0m\n');
  process.exit(falha ? 1 : 0);
})();
