// Loads the Blocks editor under the site's real Content-Security-Policy (from
// _headers, which forbids eval) and runs four demos in the simulator, the slot
// export, the Code tab and STOP. Run: node scripts/check-mecanum-blocks.js
const http = require('http');
const fs = require('fs');
const path = require('path');
const puppeteer = require('puppeteer');

const root = require('path').resolve(__dirname, '..');
const csp = fs.readFileSync(path.join(root, '_headers'), 'utf8').split('\n').find((l) => /^\s*Content-Security-Policy:/.test(l)).replace(/^\s*Content-Security-Policy:\s*/, '');
const types = { '.html': 'text/html', '.js': 'application/javascript', '.png': 'image/png', '.svg': 'image/svg+xml', '.css': 'text/css', '.cur': 'image/x-win-bitmap', '.mp3': 'audio/mpeg', '.gif': 'image/gif' };
const server = http.createServer((req, res) => {
  const file = path.join(root, decodeURIComponent(req.url.split('?')[0]));
  fs.readFile(file, (err, data) => {
    if (err) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Content-Security-Policy': csp });
    res.end(data);
  });
});
const failures = [];
function check(c, m) { console.log((c ? 'ok   ' : 'FAIL ') + m); if (!c) failures.push(m); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  await new Promise((r) => server.listen(8198, '127.0.0.1', r));
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox', '--disable-gpu'] });
  const page = await browser.newPage();
  const errors = [];
  page.on('pageerror', (e) => errors.push(e.message));
  page.on('requestfailed', (r) => errors.push('request failed ' + r.url()));
  page.on('console', (m) => { if (m.type() === 'error') errors.push('console: ' + m.text()); });
  let documentCsp = '';
  page.on('response', (r) => { if (/blocks\.html$/.test(r.url())) documentCsp = r.headers()['content-security-policy'] || ''; });
  await page.setViewport({ width: 1280, height: 800 });
  await page.goto('http://127.0.0.1:8198/mecanum/blocks.html', { waitUntil: 'load' });
  await sleep(2000);
  check(await page.evaluate(() => typeof Interpreter === 'function'), 'JS-Interpreter loaded under the CSP');
  check(/script-src 'self'/.test(documentCsp) && !/unsafe-eval/.test(documentCsp), 'the page was served with the site CSP (no unsafe-eval): ' + documentCsp.slice(0, 60));

  const consoleText = () => page.evaluate(() => document.querySelector('#consoleLines') ? (document.querySelector('#consoleLines').textContent) : document.body.textContent);
  async function runTab(index, label, timeoutMs) {
    await page.evaluate((i) => { document.querySelectorAll('#programTabs .program-tab')[i].click(); }, index);
    await sleep(400);
    await page.evaluate(() => { var s = document.querySelector('#simulatorSpeed, select[name=simulatorSpeed]'); });
    const before = (await consoleText()).length;
    await page.click('#runButton');
    const started = Date.now();
    let text = '';
    while (Date.now() - started < timeoutMs) {
      text = (await consoleText()).slice(before);
      if (/Program finished\.|hit a problem|could not be turned|Program stopped/.test(text)) break;
      await sleep(200);
    }
    check(/Program finished\./.test(text), label + ' runs in the simulator to "Program finished"' + (/Program finished\./.test(text) ? '' : ': ' + text.slice(-300)));
  }
  // Faster simulator so the demos finish quickly
  await page.evaluate(() => { var sel = document.querySelector('select'); });
  await page.evaluate(() => { try { editorSettings.simulatorSpeed = '10'; } catch (e) {} });
  await runTab(0, 'Square (repeat loop)', 60000);
  await runTab(5, 'Spiral (variable + counting loop)', 90000);
  await runTab(7, 'Figure 8 (two subroutines)', 90000);
  await runTab(8, 'Dance', 60000);

  const exported = await page.evaluate(async () => (await buildAllSlotsExport()));
  check(exported.lines.length === 9 && /^p1=f50 t90 f50 t90 f50 t90 f50 t90;$/.test(exported.lines[0]) && /^p8=f40 t90/.test(exported.lines[7]), 'slot export (the recorder) still builds all nine lines: ' + exported.lines[0] + ' ... ' + exported.lines[7]);
  const codeShown = await page.evaluate(() => { document.querySelectorAll('#programTabs .program-tab')[7].click(); return refreshWillDoAndCode().then(function () { return document.getElementById('codeView').textContent; }); });
  check(!/await|async/.test(codeShown) && /function /.test(codeShown), 'Code tab shows plain JavaScript with subroutines, no async');

  // The sensor examples (light, line) and the sensor blocks in the simulator
  async function runExample(name, timeoutMs, mustSay) {
    const loaded = await page.evaluate((n) => {
      const example = listLiveExamplePrograms().find((e) => e.name === n);
      if (!example) return false;
      Blockly.serialization.workspaces.load(example.state, blockWorkspace);
      return true;
    }, name);
    check(loaded, 'example "' + name + '" exists');
    if (!loaded) return;
    await sleep(300);
    const before = (await consoleText()).length;
    await page.click('#runButton');
    const started = Date.now();
    let text = '';
    while (Date.now() - started < timeoutMs) {
      text = (await consoleText()).slice(before);
      if (/Program finished\.|hit a problem|could not be turned|Program stopped/.test(text)) break;
      await sleep(200);
    }
    check(/Program finished\./.test(text) && mustSay.test(text), name + ' runs in the simulator: ' + (mustSay.test(text) ? 'says what it should' : text.slice(-300)));
  }
  await page.evaluate(() => { editorSettings.simulatorSpeed = '10'; });
  await runExample('Moth', 60000, /moth: chasing the light[\s\S]*moth: at the light/);
  await runExample('Cockroach', 60000, /cockroach: running from the light/);
  await runExample('Line loop', 90000, /follow the line for 20 s/);
  const sensed = await page.evaluate(async () => {
    resetSimulatorWorld();
    const control = createRunControl();
    const backend = createSimulatorBackend(control);
    const out = {
      lightLevel: await backend.readLight('level', 0), lightDirection: await backend.readLight('direction', 0), ldr1: await backend.readLight('sensor', 1),
      lineSeen: await backend.readLine('seen', 0), linePosition: await backend.readLine('position', 0), ch4: await backend.readLine('sensor', 4), ch1: await backend.readLine('sensor', 1),
      heading: await backend.imu('heading'), pitch: await backend.imu('pitch'), up: await backend.imu('accelUp'),
    };
    backend.finish();
    return out;
  });
  check(sensed.lightLevel > 40 && sensed.lightLevel < 80 && Math.abs(sensed.lightDirection - 45) <= 1 && sensed.ldr1 > 0, 'light from the start: level ' + sensed.lightLevel + ', direction ' + sensed.lightDirection + ', LDR 1 ' + sensed.ldr1);
  check(sensed.lineSeen === true && Math.abs(sensed.linePosition) <= 1 && sensed.ch4 > 50 && sensed.ch1 === 0, 'line at the start: seen, centred (' + sensed.linePosition + ' mm), channel 4 ' + sensed.ch4 + ', channel 1 ' + sensed.ch1);
  check(sensed.heading === 0 && sensed.pitch === 0 && sensed.up === 1, 'IMU in the simulator: heading 0, pitch 0, up 1 g');
  const toolbox = await page.evaluate(() => Array.from(document.querySelectorAll('.blocklyToolboxCategory, .blocklyTreeRow')).map((e) => e.textContent.trim()).filter(Boolean));
  check(toolbox.some((t) => /Someday/.test(t)), 'the Someday category is in the toolbox');

  // STOP mid-run
  await page.evaluate(() => { document.querySelectorAll('#programTabs .program-tab')[0].click(); });
  await sleep(300);
  await page.click('#runButton');
  await sleep(800);
  await page.evaluate(() => pressStop());
  await sleep(600);
  check(/Program stopped\./.test(await consoleText()), 'STOP ends a running program');

  console.log('errors:', errors);
  check(errors.filter((e) => !/favicon/.test(e)).length === 0, 'no page errors, CSP violations or failed requests');
  await browser.close();
  server.close();
  console.log(failures.length ? failures.length + ' FAILED' : 'all passed');
  process.exit(failures.length ? 1 : 0);
})();
