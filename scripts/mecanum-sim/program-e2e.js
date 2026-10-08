// End-to-end: the Workshop app's Program tab (Blockly) against the firmware in
// simavr. Run: node scripts/mecanum-sim/program-e2e.js [bt]
const { spawn, execSync } = require('child_process');
const os = require('os');
const path = require('path');
const puppeteer = require('puppeteer');

const PORT = 8192 + (process.argv[2] === 'bt' ? 10 : 0);
const root = path.resolve(__dirname, '..', '..');
// Each suite (and its Bluetooth run) builds in its own folder, so they can all run at once.
const build = path.join(root, 'temp', 'mecanum-sim', path.basename(__filename, '.js') + (process.argv[2] === 'bt' ? '-bt' : ''));
const simBinary = path.join(build, 'uno-sim');
const sketchCopy = path.join(build, 'mecanum_holonomic');
const elf = path.join(build, 'out', 'mecanum_holonomic.ino.elf');
function buildEverything() {
  require('fs').mkdirSync(sketchCopy, { recursive: true });
  execSync(`gcc -O2 -o "${simBinary}" "${path.join(__dirname, 'uno-sim.c')}" -lsimavr -lelf -lm`, { stdio: 'inherit' });
  require('fs').copyFileSync(path.join(root, 'mecanum_holonomic', 'mecanum_holonomic.ino'), path.join(sketchCopy, 'mecanum_holonomic.ino'));
  execSync(`"${path.join(os.homedir(), '.local', 'bin', 'arduino-cli')}" compile --fqbn arduino:avr:uno --output-dir "${path.join(build, 'out')}" "${sketchCopy}"`, { stdio: 'ignore' });
}
const failures = [];
function check(condition, message) { console.log((condition ? 'ok   ' : 'FAIL ') + message); if (!condition) failures.push(message); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  buildEverything();
  const server = spawn('python3', ['-m', 'http.server', String(PORT), '--bind', '0.0.0.0'], { cwd: root, stdio: 'ignore' });
  const sim = spawn(simBinary, [elf, 'live'].concat(process.argv[2] === 'bt' ? ['bt'] : []));
  let simLog = '';
  await sleep(800);
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox', '--disable-gpu'] });
  const page = await browser.newPage();
  await page.setViewport({ width: 390, height: 844, deviceScaleFactor: 1, isMobile: true, hasTouch: true });
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(e.message));
  await page.goto(`http://127.0.0.1:${PORT}/mecanum/`, { waitUntil: 'load' });
  await page.exposeFunction('simWrite', (bytes) => sim.stdin.write(Buffer.from(bytes)));
  sim.stdout.on('data', (chunk) => {
    simLog += chunk.toString();
    page.evaluate((bytes) => handleIncomingBytes(new Uint8Array(bytes)), [...chunk]).catch(() => {});
  });
  const control = (text) => sim.stdin.write('\x01' + text + '\n');
  const waitFor = async (fn, label, timeoutMs = 20000) => {
    const started = Date.now();
    while (Date.now() - started < timeoutMs) { if (await page.evaluate(fn)) return true; await sleep(100); }
    check(false, 'timed out waiting for ' + label);
    return false;
  };
  control('dev mpu');
  await page.evaluate(() => { link = { kind: 'usb', label: 'simavr', write: (b) => window.simWrite(Array.from(b)), close: () => Promise.resolve() }; setConnectedState(); });
  control('reboot');
  await waitFor(() => sensorReadDone, 'first settings read');

  // The tab, Blockly and the toolbox
  await page.click('#programTabButton');
  await waitFor(() => !!programWorkspace && /steps|empty/.test(programStatus.textContent), 'Blockly loaded');
  check(await page.evaluate(() => document.querySelectorAll('#programArea .blocklyToolboxCategoryLabel, #programArea .blocklyTreeLabel').length >= 3 || document.querySelectorAll('.blocklyToolboxCategory').length >= 3), 'toolbox shows Move / Control / IMU');
  check(await page.evaluate(() => /forward 1000 mm/.test(document.getElementById('programSteps').textContent) && /turn right 90 deg/.test(document.getElementById('programSteps').textContent)), 'default program listed: forward 1000 mm, turn right 90');
  await page.screenshot({ path: path.join(build, 'program-tab.png'), fullPage: false });

  // Run it
  const movesBefore = (simLog.match(/move: done/g) || []).length;
  await page.click('#programRunButton');
  await waitFor(() => /^step 1 of 2/.test(programStatus.textContent) && busyReason === 'program', 'program running');
  check(await page.evaluate(() => /STOP PROGRAM/.test(document.getElementById('floatingAbort').textContent)), 'STOP PROGRAM bar while it runs');
  await waitFor(() => programStatus.textContent === 'done', 'program done', 40000);
  check((simLog.match(/move: done/g) || []).length - movesBefore === 2 && /m0,1000,0;|move: start/.test(simLog), 'robot ran both moves (2x move: done)');
  check(await page.evaluate(() => busyReason === null), 'robot not busy afterwards');

  // An example with repeat + heading hold, stopped part-way
  await page.evaluate(() => { document.getElementById('programPicker').value = 'Example: IMU showdown (square without, then with)'; });
  await page.click('#programOpenButton');
  await waitFor(() => /heading hold off/.test(document.getElementById('programSteps').textContent), 'showdown listed');
  check(await page.evaluate(() => document.querySelectorAll('#programSteps li').length === 19), 'showdown compiles to 19 steps (repeat unrolled)');
  await page.click('#programRunButton');
  await waitFor(() => /^step [3-9]/.test(programStatus.textContent), 'showdown past step 2', 30000);
  check(/imu: present, off/.test(simLog), 'heading hold off reached the robot');
  await page.click('#floatingAbort');
  await waitFor(() => programStatus.textContent === 'stopped' && busyReason === null, 'program stopped', 15000);
  check(/move: ABORTED/.test(simLog), 'robot aborted the move');

  // Save and reopen
  await page.evaluate(() => { document.getElementById('programNameInput').value = 'my square'; });
  await page.click('#programSaveButton');
  await page.evaluate(() => { document.getElementById('programPicker').value = 'Example: crab box'; });
  await page.click('#programOpenButton');
  await page.evaluate(() => { document.getElementById('programPicker').value = 'my square'; });
  await page.click('#programOpenButton');
  check(await page.evaluate(() => document.querySelectorAll('#programSteps li').length === 19), 'saved program reopens');

  check(!(await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)), 'no horizontal scroll at 390 px');
  check(pageErrors.length === 0, 'no page errors' + (pageErrors.length ? ': ' + pageErrors.join(' | ') : ''));
  await browser.close();
  sim.kill();
  server.kill();
  console.log(failures.length ? `\n${failures.length} FAILED` : '\nall passed');
  process.exit(failures.length ? 1 : 0);
})().catch((error) => { console.error(error); process.exit(1); });
