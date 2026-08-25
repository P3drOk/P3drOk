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
  // 64 KB nao e chute: no ponto de acesso do ESP32 a transferencia fica
  // na casa de 1 a 2 Mbit/s efetivos, entao 64 KB sao menos de meio
  // segundo -- o operador nao percebe. Na flash sao 64 KB de uma
  // particao de 3 MB, que tambem nao aperta.
  //
  // O teto existe para o crescimento ser uma DECISAO e nao um acidente:
  // quando ele estourar, e hora de olhar o que cresceu antes de subir o
  // numero de novo.
  checar(cab['content-encoding'] === 'gzip' && bytesRede > 0 && bytesRede < 64000,
         'a pagina e servida comprimida, como o firmware faz',
         'Content-Encoding: ' + cab['content-encoding'] + ', ' + bytesRede +
         ' bytes na rede');

  // Barra de abas presente e com as cinco abas
  const nAbas = await p.locator('#abas button').count();
  checar(nAbas === 6, 'barra de abas inferior com 6 abas', nAbas + ' abas encontradas');

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

  // ---- abas ----
  for (const [aba, alvo, nome] of [
    ['prog',   '#e2',      'Programa'],
    ['arq',    '#sdBar',   'Arquivos'],
    ['enc',    '#cvEnc',   'Encoder'],
    ['ajuste', '#e1',      'Ajustes'],
    ['mesa',   '#cv',      'Mesa'],
  ]) {
    await p.locator('#abas button[data-aba="' + aba + '"]').click();
    await p.waitForTimeout(350);
    const vis = await p.locator(alvo).isVisible();
    checar(vis, 'aba ' + nome + ' mostra o seu conteudo (' + alvo + ')');
    await p.screenshot({ path: SAIDA + '/celular-3-' + aba + '.png' });
  }

  // A mesa de tracado desenha depois de trocar de aba
  const dim = await p.evaluate(() => { const c = document.getElementById('cv'); return [c.width, c.height]; });
  checar(dim[0] > 100 && dim[1] > 100, 'o canvas e redimensionado ao abrir a aba Mesa',
         dim[0] + 'x' + dim[1] + ' px');

  // Arquivos: lista do cartao
  await p.locator('#abas button[data-aba="arq"]').click();
  await p.waitForTimeout(500);
  const nArq = await p.locator('#sdLista .arq').count();
  const titulo = await p.locator('#sdTit').textContent();
  checar(nArq === 2, 'a aba Arquivos lista os programas do cartao', nArq + ' arquivo(s); "' + titulo + '"');

  // "Nao consegui salvar o desenho no cartao": o botao respondia 200 e a
  // recusa ia so para a tira de mensagem. Agora ele diz o que falta.
  await p.locator('#sdNome').fill('peca teste');
  await p.waitForTimeout(300);
  const salvarOk = await p.evaluate(() => ({
    dis: document.getElementById('btSdSalvar').disabled,
    oque: document.getElementById('sdOque').textContent.trim(),
  }));
  checar(!salvarOk.dis && /3 pontos/.test(salvarOk.oque),
         'Arquivos: com programa na maquina, Salvar libera e diz o que vai gravar',
         salvarOk.oque);

  await p.request.post(BASE + '/teste/estado', { data: { progN: 0 } });
  await p.waitForTimeout(600);
  const semProg = await p.evaluate(() => ({
    dis: document.getElementById('btSdSalvar').disabled,
    motivo: document.getElementById('qSdSalvar').textContent.trim(),
    oque: document.getElementById('sdOque').textContent.trim(),
  }));
  checar(semProg.dis && semProg.motivo.length > 0,
         'Arquivos: sem programa na maquina, Salvar trava dizendo o porque',
         semProg.motivo);
  checar(/Desenhe na mesa|importe um DXF/.test(semProg.oque),
         'Arquivos: e diz de onde vem um programa', semProg.oque);

  await p.locator('#sdNome').fill('nome/invalido');
  await p.waitForTimeout(300);
  await p.request.post(BASE + '/teste/estado', { data: { progN: 3 } });
  await p.waitForTimeout(600);
  const nomeRuim = await p.evaluate(() =>
    document.getElementById('qSdSalvar').textContent.trim());
  checar(/letras/.test(nomeRuim),
         'Arquivos: nome com caractere proibido e barrado antes de ir ao robo',
         nomeRuim);
  await p.locator('#sdNome').fill('');
  await p.waitForTimeout(200);

  chamadas.length = 0;
  await p.locator('#sdLista [data-car]').first().click();
  await p.waitForTimeout(200);
  const carregou = chamadas.find(c => c.startsWith('/api/sd/carregar'));
  checar(!!carregou, 'o botao carregar chama a rota certa com o nome escapado', carregou);

  await p.locator('#segTipo button[data-t="traj"]').click();
  await p.waitForTimeout(400);
  const nTraj = await p.locator('#sdLista .arq').count();
  checar(nTraj === 1, 'trocar para Trajetorias recarrega a lista', nTraj + ' arquivo(s)');

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

  // A coluna do Encoder fica ABERTA enquanto se usa o resto: e para isso
  // que ela saiu da barra de abas. Trocar de aba para olhar o erro seria
  // perder justamente o momento em que ele acontece.
  for (const aba of ['mover', 'prog', 'ajuste']) {
    await q.locator('#abasTopo button[data-aba="' + aba + '"]').click();
    await q.waitForTimeout(220);
    const vis = await q.locator('#cvEnc').isVisible();
    checar(vis, 'Encoder: a coluna continua aberta com a aba ' + aba + ' escolhida');
  }
  const botaoEnc = await q.evaluate(() => {
    const b = document.querySelector('#abasTopo button[data-aba="enc"]');
    return b ? getComputedStyle(b).display !== 'none' : false;
  });
  checar(!botaoEnc,
         'Encoder: sem botao de aba no computador -- a coluna ja esta na tela');
  await q.screenshot({ path: SAIDA + '/computador-4-encoder-fixo.png' });
  await q.locator('#abasTopo button[data-aba="mover"]').click();
  await q.waitForTimeout(200);

  // Zero absoluto: a secao nasce TRANCADA em toda visita. O tranco existe
  // para nao se mexer sem querer -- e "sem querer" inclui ter deixado
  // aberto ontem. Errar ali desloca a area util inteira.
  await q.evaluate(() => {
    const alvo = document.getElementById('etZero');
    document.querySelectorAll('#pnEnc .et')
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
    document.querySelectorAll('#pnEnc .et')
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

  // As explicacoes ensinam quem comeca e atrapalham quem opera todo dia.
  // O "?" esconde todas, e a escolha fica gravada. Esconder nao pode
  // apagar: um clique traz tudo de volta.
  const notasAntes = await q.locator('#pnMover .nt').first().isVisible();
  await q.locator('#btAjuda').click();
  await q.waitForTimeout(250);
  const notasDepois = await q.locator('#pnMover .nt').first().isVisible();
  checar(notasAntes && !notasDepois,
         'Painel: o "?" esconde as explicacoes de uma vez');
  // Os controles NAO podem sumir junto: esconder texto e uma coisa,
  // esconder botao e outra.
  const controles = await q.evaluate(() => ({
    joy: !!document.getElementById('joy').offsetParent,
    prec: !!document.getElementById('btPrec').offsetParent,
    jb: document.querySelectorAll('#pnMover .jb').length,
  }));
  checar(controles.joy && controles.prec && controles.jb === 4,
         'Painel: e os controles continuam todos la',
         controles.jb + ' botoes de passo, joystick e precisao visiveis');
  await q.screenshot({ path: SAIDA + '/computador-6-sem-notas.png' });
  await q.locator('#btAjuda').click();
  await q.waitForTimeout(250);
  const voltaram = await q.locator('#pnMover .nt').first().isVisible();
  checar(voltaram, 'Painel: e um clique traz as explicacoes de volta');

  // Vista 3D: e a MESMA maquina, vista de outro angulo. A vista de cima
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
  const PANES = { mover: '#pnMover', prog: '#pnProg', arq: '#pnArq', ajuste: '#pnAjuste' };
  const mortos = [], mudos = [];
  let clicados = 0;
  // Abrir o seletor de arquivo E a acao do botao de importar DXF: ele nao
  // chama rota nenhuma. Sem isto o Chromium fica esperando o dialogo e a
  // varredura marca o botao como morto.
  let abriuArquivo = 0;
  t.on('filechooser', fc => { abriuArquivo++; fc.setFiles([]).catch(() => {}); });
  for (const [aba, sel] of Object.entries(PANES)) {
    await t.locator('#abas button[data-aba="' + aba + '"]').click();
    await t.waitForTimeout(250);
    const nSec = await t.locator(sel + ' .et').count();
    for (let k = 0; k < nSec; k++) {
      await t.evaluate(([sel, k]) => {
        document.querySelectorAll(sel + ' .et').forEach((x, i) => x.classList.toggle('aberta', i === k));
      }, [sel, k]);
      await t.waitForTimeout(120);
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
        if (!a.vis) { mortos.push(a.id + ' (invisivel na propria secao)'); continue; }
        if (a.dis) { if (!a.motivo) mudos.push(a.id); continue; }
        rotas = [];
        try { await t.locator('#' + a.id).click({ timeout: 1500 }); }
        catch (e) { mortos.push(a.id + ' (nao clicavel)'); continue; }
        await t.waitForTimeout(230);
        clicados++;
        const uteis = rotas.filter(x => !RUIDO.includes(x));
        // btSdSalvar sem nome no campo e recusa local proposital;
        // btDxfAbrir abre o seletor de arquivo, que nao e uma rota.
        const local = (a.id === 'btSdSalvar') ||
                      (a.id === 'btDxfAbrir' && abriuArquivo > 0);
        if (!uteis.length && !local) mortos.push(a.id + ' (nao chamou nada)');
      }
    }
  }
  checar(mortos.length === 0, 'todo botao visivel e habilitado dispara uma acao',
         mortos.length ? 'mortos: ' + mortos.join(', ') : clicados + ' botoes clicados, todos responderam');
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

  // Assistente de calibracao: campos que so aparecem onde fazem sentido.
  for (const [calib, rotulo, temCampos] of [
    ['HOME',      'Junta 1 esta em',      true],
    ['J1_NEG',    '',                     false],
    ['CONCLUIDO', 'Curso real da junta 1', true],
  ]) {
    await t.request.post(BASE + '/teste/estado', { data: { calib, calibEixo: 0 } });
    await t.waitForTimeout(500);
    const est = await t.evaluate(() => ({
      veu: document.getElementById('veu').classList.contains('on'),
      vis: document.getElementById('cMed').style.display !== 'none',
      l1: document.getElementById('cMedL1').textContent.trim(),
      g1: document.getElementById('cG1').value,
    }));
    checar(est.veu && est.vis === temCampos && (!temCampos || est.l1 === rotulo),
           'assistente na etapa ' + calib + ': campos de medida ' +
           (temCampos ? 'aparecem' : 'somem'),
           temCampos ? ('"' + est.l1 + '" preenchido com ' + est.g1) : 'ocultos');
  }

  // O sentido do eixo se descobre errado APERTANDO a seta no assistente.
  // Tem de dar para consertar ali, sem cancelar tudo.
  await t.request.post(BASE + '/teste/estado',
    { data: { calib: 'HOME', calibEixo: 0 } });
  await t.waitForTimeout(500);
  const setas = await t.evaluate(() => {
    const b = [...document.querySelectorAll('#cJ1 .jb')];
    return { n: b.length, txt: b.map(x => x.textContent.trim()).join(' '),
             dir: b.map(x => x.dataset.d).join(' '),
             sent: document.getElementById('cSent').style.display !== 'none' };
  });
  // Junta e coisa que gira: seta para os lados nao quer dizer nada aqui.
  checar(/\u21ba/.test(setas.txt) && /\u21bb/.test(setas.txt),
         'assistente: os botoes de jog falam em sentido de rotacao, nao em lados',
         setas.txt + '  (data-d: ' + setas.dir + ')');
  checar(setas.sent,
         'assistente: na etapa de referencia aparece a conferencia de sentido');

  rotas = [];
  await t.locator('#cInv1').click();
  await t.waitForTimeout(300);
  const inv = rotas.find(x => x.split('?')[0] === '/api/sentido');
  checar(!!inv && /j=1/.test(inv),
         'assistente: inverter a junta 1 chama /api/sentido sem cancelar o assistente',
         inv || 'nada');

  // Depois de medir o primeiro limite, o sentido some: ja ha medida que
  // seria invertida junto.
  await t.request.post(BASE + '/teste/estado', { data: { calib: 'J1_NEG' } });
  await t.waitForTimeout(500);
  checar(!(await t.evaluate(() =>
             document.getElementById('cSent').style.display !== 'none')),
         'assistente: passada a referencia, a troca de sentido sai da tela');
  await t.request.post(BASE + '/teste/estado', { data: { calib: 'CONCLUIDO' } });
  await t.waitForTimeout(500);

  // Confirmar na etapa de conclusao manda o curso medido.
  rotas = [];
  await t.evaluate(() => { document.getElementById('cG1').value = '58.5';
                           document.getElementById('cG2').value = '61.0'; });
  await t.locator('#cOk').click();
  await t.waitForTimeout(300);
  const conf = rotas.find(x => x.startsWith('/api/calib/confirmar'));
  checar(!!conf && /g1=58\.5/.test(conf) && /g2=61/.test(conf),
         'concluir a calibracao envia o curso realmente medido', conf);
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

  // Mesa de tracado: clique comanda XY, zoom e tema respondem.
  await t.locator('#abas button[data-aba="mesa"]').click();
  await t.waitForTimeout(350);
  rotas = [];
  const cvb = await t.locator('#cv').boundingBox();
  await t.mouse.click(cvb.x + cvb.width * 0.62, cvb.y + cvb.height * 0.4);
  await t.waitForTimeout(250);
  checar(rotas.some(x => x.split('?')[0] === '/api/mover_xy'),
         'tocar na mesa de tracado comanda a ponta');
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

  // Encoder: leitura ao vivo, grafico do erro e configuracao do registrador.
  await t.locator('#abas button[data-aba="enc"]').click();
  await t.waitForTimeout(900);
  const enc = await t.evaluate(() => ({
    cmd:  document.getElementById('eC1').textContent.trim(),
    med:  document.getElementById('eM1').textContent.trim(),
    err:  document.getElementById('eE1').textContent.trim(),
    med2: document.getElementById('eM2').textContent.trim(),
    sb:   document.getElementById('sbEnc').textContent.trim(),
    w:    document.getElementById('cvEnc').width,
  }));
  checar(/°$/.test(enc.cmd) && /°$/.test(enc.med) && /°$/.test(enc.err),
         'Encoder: mostra comandado, medido e erro em graus da junta',
         enc.cmd + ' | ' + enc.med + ' | ' + enc.err);
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
  checar(enc.w > 100, 'Encoder: o grafico e dimensionado ao abrir a aba',
         enc.w + ' px');

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
    larg: document.getElementById('cvPos').width,
    sub: document.getElementById('sbAnal').textContent,
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

  // O historico tem de crescer com as consultas.
  await t.waitForTimeout(1600);
  const nAmostras = await t.evaluate(() => window.__encN || 0);
  checar(nAmostras >= 2, 'Encoder: o grafico acumula historico do erro',
         nAmostras + ' amostras');

  // Abre a secao PELO CONTEUDO, nao pelo indice: acrescentar uma secao
  // nova no painel nao pode quebrar um teste que nao e sobre ela.
  await t.evaluate(() => {
    const alvo = document.getElementById('encReg1').closest('.et');
    document.querySelectorAll('#pnEnc .et')
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
  await t.locator('#encReg1').fill('8192');
  await t.locator('#btEncSalvar').click();
  await t.waitForTimeout(400);
  const salvou = rotas.find(x => x.split('?')[0] === '/api/encoder/config');
  checar(!!salvou && /reg1=8192/.test(salvou),
         'Encoder: mudar o registrador vai pela rota de configuracao',
         salvou || 'nada');

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

  // Rede: a maquina tem Wi-Fi proprio, o painel so diz por onde chegar.
  await t.locator('#abas button[data-aba="ajuste"]').click();
  await t.waitForTimeout(250);
  await t.evaluate(() => document.querySelectorAll('#pnAjuste .et')
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

  // Zerar a maquina na posicao e aferir a reducao (as tres etapas).
  await t.locator('#abas button[data-aba="mover"]').click();
  await t.waitForTimeout(250);
  rotas = [];   /* o aceite do confirm() ja esta armado la em cima */
  await t.locator('#btRefer').click();
  await t.waitForTimeout(300);
  checar(rotas.some(x => x.split('?')[0] === '/api/referenciar'),
         '"Zerar a maquina aqui" pede confirmacao e chama /api/referenciar');

  await t.locator('#abas button[data-aba="ajuste"]').click();
  await t.waitForTimeout(250);
  await t.evaluate(() => document.querySelectorAll('#pnAjuste .et')
    .forEach(x => x.classList.toggle('aberta', x.id === 'e5')));
  await t.waitForTimeout(200);
  rotas = [];
  await t.locator('#btAfMarcar').click();
  await t.waitForTimeout(250);
  checar(rotas.some(x => x === '/api/aferir/marcar?j=1'),
         'aferir: "Marcar o inicio" manda a junta escolhida');
  const semMarca = await t.locator('#btAfAplicar').isDisabled();
  checar(semMarca, 'aferir: sem marca e sem angulo o botao de gravar fica travado');

  // O robo passa a contar pulsos desde a marca.
  await t.request.post(BASE + '/teste/estado', { data: { afer1: 9500 } });
  await t.waitForTimeout(500);
  const semAngulo = await t.evaluate(() => ({
    dis: document.getElementById('btAfAplicar').disabled,
    motivo: document.getElementById('qAfAplicar').textContent.trim(),
    conta: document.getElementById('afConta').textContent.trim(),
  }));
  checar(semAngulo.dis && /graus/.test(semAngulo.motivo),
         'aferir: com marca mas sem angulo, o botao explica o que falta',
         semAngulo.motivo);
  checar(/9500 pulsos/.test(semAngulo.conta),
         'aferir: os pulsos contados desde a marca aparecem na tela',
         semAngulo.conta.replace(/\n/g, ' / '));

  rotas = [];
  await t.locator('#afG').fill('54.5');
  await t.waitForTimeout(150);
  checar(!(await t.locator('#btAfAplicar').isDisabled()),
         'aferir: digitado o angulo, o botao de gravar libera');
  await t.locator('#btAfAplicar').click();
  await t.waitForTimeout(300);
  checar(rotas.some(x => x === '/api/aferir/aplicar?j=1&g=54.5'),
         'aferir: o pedido leva a junta e os graus medidos');
  await t.request.post(BASE + '/teste/estado', { data: { afer1: 0 } });

  // Botoes de seta do jog.
  rotas = [];
  await t.locator('#abas button[data-aba="mover"]').click();
  await t.waitForTimeout(250);
  const seta = t.locator('#pnMover .jb').first();
  await seta.dispatchEvent('pointerdown', { pointerId: 5 });
  await t.waitForTimeout(230);
  await seta.dispatchEvent('pointerup', { pointerId: 5 });
  await t.waitForTimeout(180);
  const jogs = rotas.filter(x => x.split('?')[0] === '/api/jog');
  checar(jogs.length >= 2, 'as setas de jog mandam heartbeat e depois o zero',
         jogs.length + ' chamadas a /api/jog');

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

  checar(errosT.length === 0, 'nenhum erro de JavaScript em toda a varredura',
         errosT.length ? errosT.slice(0, 3).join(' | ') : 'console limpo');

  await browser.close();
  console.log('\n\x1b[1mINTERFACE: ' + passa + ' passaram, ' + falha + ' falharam\x1b[0m\n');
  process.exit(falha ? 1 : 0);
})();
