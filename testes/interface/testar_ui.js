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

  // Barra de abas presente e com as cinco abas
  const nAbas = await p.locator('#abas button').count();
  checar(nAbas === 5, 'barra de abas inferior com 5 abas', nAbas + ' abas encontradas');

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

  await q.locator('#zTema').click();
  await q.waitForTimeout(300);
  await q.screenshot({ path: SAIDA + '/computador-3-tema-escuro.png' });
  const tema = await q.evaluate(() => document.documentElement.getAttribute('data-tema'));
  checar(tema === 'escuro', 'o botao TEMA continua alternando prancheta e oficina');

  checar(errosPc.length === 0, 'nenhum erro de JavaScript no computador',
         errosPc.length ? errosPc.slice(0, 3).join(' | ') : 'console limpo');

  await browser.close();
  console.log('\n\x1b[1mINTERFACE: ' + passa + ' passaram, ' + falha + ' falharam\x1b[0m\n');
  process.exit(falha ? 1 : 0);
})();
