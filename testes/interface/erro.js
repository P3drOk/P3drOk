const { chromium } = require('playwright');
(async () => {
  const b = await chromium.launch();
  const p = await b.newPage();
  const errs = [];
  p.on('pageerror', e => errs.push(String(e)));
  p.on('console', m => { if (m.type() === 'error') errs.push('console: ' + m.text()); });
  await p.goto('http://127.0.0.1:8137/', { waitUntil: 'networkidle' });
  await p.waitForTimeout(2500);
  console.log(errs.length ? errs.slice(0, 5).join('\n---\n') : 'console limpo');
  await b.close();
})();
