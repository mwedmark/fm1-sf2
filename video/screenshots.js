// Screenshots of the converter (index.html, with soundfonts/1mgm.sf2 converted to BFP8) and of the install page's steps, for the
// walk-through video. Needs a server for the repo on 127.0.0.1:8765 (make_video.sh starts one) and Playwright.
//   node video/screenshots.js <out_dir>
const { chromium } = require('playwright');
const path = require('path');
(async () => {
  const out = process.argv[2];
  const root = path.join(__dirname, '..');
  const browser = await chromium.launch();
  const page = await browser.newPage({ viewport: { width: 820, height: 1000 }, deviceScaleFactor: 2, colorScheme: 'dark' });
  await page.goto('http://127.0.0.1:8765/index.html');
  await page.setInputFiles('#file', path.join(root, 'soundfonts/1mgm.sf2'));
  await page.waitForTimeout(1500);
  await page.selectOption('#codec', 'bfp8');
  await page.click('#go');
  await page.waitForTimeout(4000);
  await page.screenshot({ path: path.join(out, 'converter.png'), fullPage: true });

  // The install page without a firmware build says its manifest is missing: show only its steps.
  const install = await browser.newPage({ viewport: { width: 820, height: 1000 }, deviceScaleFactor: 2, colorScheme: 'dark' });
  await install.goto('http://127.0.0.1:8765/install.html');
  await install.waitForTimeout(1200);
  await install.evaluate(() => {
    document.getElementById('message').remove();
    const steps = [...document.querySelectorAll('h2')].find(h => h.textContent.trim() === 'Steps').parentElement;
    let past = false;
    for (const el of [...steps.parentElement.children]) {
      if (el === steps) { past = true; continue; }
      if (past || el.querySelector('#bankFile')) el.remove();
    }
  });
  await install.screenshot({ path: path.join(out, 'install.png'), clip: { x: 0, y: 0, width: 820, height: 560 } });
  await browser.close();
})();
