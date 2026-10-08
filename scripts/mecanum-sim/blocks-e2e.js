// End-to-end: the Mecanum Blocks editor against the real firmware in simavr,
// first mecanum_holonomic.ino, then ir.ino (flashed into the same simulated
// robot). The page's serial link is pointed at the simulator; programs run on
// the "robot" target; sensors answer from simulated devices.
// Run: node scripts/mecanum-sim/blocks-e2e.js
const { spawn, execSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');
const puppeteer = require('puppeteer');

const PORT = 8199;
const root = path.resolve(__dirname, '..', '..');
const build = path.join(root, 'temp', 'mecanum-sim', 'blocks-e2e');
const simBinary = path.join(build, 'uno-sim');
const arduinoCli = path.join(os.homedir(), '.local', 'bin', 'arduino-cli');

function compileSketch(name, sourceDirectory) {
  const copy = path.join(build, name);
  fs.mkdirSync(copy, { recursive: true });
  fs.copyFileSync(path.join(sourceDirectory, name + '.ino'), path.join(copy, name + '.ino'));
  execSync(`"${arduinoCli}" compile --fqbn arduino:avr:uno --output-dir "${path.join(build, 'out-' + name)}" "${copy}"`, { stdio: 'ignore' });
  return path.join(build, 'out-' + name, name + '.ino.elf');
}
function buildEverything() {
  fs.mkdirSync(build, { recursive: true });
  execSync(`gcc -O2 -o "${simBinary}" "${path.join(__dirname, 'uno-sim.c')}" -lsimavr -lelf -lm`, { stdio: 'inherit' });
  return {
    robot: compileSketch('mecanum_holonomic', path.join(root, 'mecanum_holonomic')),
    ir: compileSketch('ir', path.join(root, 'ir')),
  };
}
const failures = [];
function check(condition, message) { console.log((condition ? 'ok   ' : 'FAIL ') + message); if (!condition) failures.push(message); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  const elf = buildEverything();
  const server = spawn('python3', ['-m', 'http.server', String(PORT), '--bind', '0.0.0.0'], { cwd: root, stdio: 'ignore' });
  const sim = spawn(simBinary, [elf.robot, 'live']);
  let simLog = '';
  await sleep(800);
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox', '--disable-gpu'] });
  const page = await browser.newPage();
  await page.setViewport({ width: 1280, height: 800 });
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(e.message));
  const dialogs = [];
  page.on('dialog', (d) => { dialogs.push(d.message()); d.dismiss(); });
  await page.goto(`http://127.0.0.1:${PORT}/mecanum/blocks.html`, { waitUntil: 'load' });
  await sleep(1500);
  await page.exposeFunction('simWrite', (bytes) => sim.stdin.write(Buffer.from(bytes)));
  sim.stdout.on('data', (chunk) => {
    simLog += chunk.toString();
    page.evaluate((bytes) => handleIncomingBytes(new Uint8Array(bytes)), [...chunk]).catch(() => {});
  });
  const control = (text) => sim.stdin.write('\x01' + text + '\n');
  const consoleText = () => page.evaluate(() => document.getElementById('consoleLines').textContent);
  const waitFor = async (fn, label, timeoutMs = 20000) => {
    const started = Date.now();
    while (Date.now() - started < timeoutMs) { if (await fn()) return true; await sleep(150); }
    check(false, 'timed out waiting for ' + label);
    return false;
  };
  // The page's serial link, pointed at the simulator
  const connect = () => page.evaluate(() => {
    robotLink.kind = 'serial'; robotLink.isConnected = true;
    robotLink.serialWriter = { write: (b) => window.simWrite(Array.from(b)) };
    afterRobotConnected();
  });
  const chooseSketch = (name) => page.evaluate((n) => { editorSettings.sketchName = n; document.getElementById('sketchSelect').value = n; }, name);
  async function runTab(index, label, timeoutMs, consoleMust) {
    await page.evaluate((i) => { document.querySelectorAll('#programTabs .program-tab')[i].click(); }, index);
    await sleep(300);
    const before = (await consoleText()).length;
    await page.click('#runButton');
    const started = Date.now();
    let text = '';
    while (Date.now() - started < timeoutMs) {
      text = (await consoleText()).slice(before);
      if (/Program finished\.|hit a problem|Program stopped|✖/.test(text)) break;
      await sleep(250);
    }
    check(/Program finished\./.test(text) && (!consoleMust || consoleMust.test(text)), label + ': ' + (/Program finished\./.test(text) ? 'finished' : text.slice(-400)));
    return text;
  }
  async function backendCall(expression) {
    return page.evaluate(async (code) => {
      const control = createRunControl();
      const backend = createRobotBackend(control);
      try { return { value: await eval(code) }; } catch (problem) { return { error: problem.message }; } finally { backend.finish(); }
    }, expression);
  }

  // ---------------- mecanum_holonomic ----------------
  control('dev mpu');
  control('dev tof');
  control('tof 300');
  control('reboot');
  await connect();
  await waitFor(() => /mecanum_holonomic v0\.10\./.test(simLog), 'firmware banner');
  check(await page.evaluate(() => document.getElementById('connectionStatusChip').textContent === 'connected by serial'), 'page shows connected by serial');
  check(/mecanum_holonomic v0\.10\.\d+/.test(await consoleText()), 'the robot banner reaches the console');
  await chooseSketch('mecanum_holonomic');

  let movesBefore = (simLog.match(/move: done/g) || []).length;
  await runTab(0, 'Square on mecanum_holonomic (8 moves)', 60000);
  check((simLog.match(/move: done/g) || []).length - movesBefore === 8, '8 x "move: done" from the firmware');

  // As shipped, mecanum_holonomic 0.10.72 has USE_TOF 0 and USE_SONAR 0 (no flash left): both distance blocks must fail clearly
  let r = await backendCall("backend.readDistance('laser')");
  check(r.error && /No reply starting with "R,"/.test(r.error), 'laser on mecanum_holonomic (USE_TOF 0): a clear fault, not a hang: ' + (r.error || JSON.stringify(r)).slice(0, 60));
  r = await backendCall("backend.readHeading()");
  check(typeof r.value === 'number' && r.value >= 0 && r.value < 360, 'heading via iw/I,: ' + JSON.stringify(r));
  r = await backendCall("backend.readDistance('sonar')");
  check(r.error && /No reply starting with "U,"/.test(r.error), 'sonar on mecanum_holonomic (USE_SONAR 0): a clear fault, not a hang: ' + (r.error || '').slice(0, 60));

  // Light and line need the sensors set up (as the Workshop Sensors tab would): 2 LDRs on A2/A3, 2 line channels on A2/A3
  control('adc 2 3000');
  control('adc 3 1200');
  await page.evaluate(() => sendToRobot('kbn=2;'));
  await sleep(400);
  await page.evaluate(() => sendToRobot('kbp0=a2;'));
  await sleep(400);
  await page.evaluate(() => sendToRobot('kbp1=a3;'));
  await sleep(400);
  r = await backendCall("backend.readLight('level', 0)");
  check(typeof r.value === 'number' && r.value >= 0 && r.value <= 100, 'light level via bs/B,: ' + JSON.stringify(r));
  r = await backendCall("backend.readLight('direction', 0)");
  check(typeof r.value === 'number', 'light direction via bs/B,: ' + JSON.stringify(r));
  await page.evaluate(() => sendToRobot('kln=2;'));
  await sleep(400);
  await page.evaluate(() => sendToRobot('klp0=a2;'));
  await sleep(400);
  await page.evaluate(() => sendToRobot('klp1=a3;'));
  await sleep(400);
  r = await backendCall("backend.readLine('seen', 0)");
  check(typeof r.value === 'boolean', 'line seen via ls/L,: ' + JSON.stringify(r));
  r = await backendCall("backend.readLine('position', 0)");
  check(typeof r.value === 'number', 'line position via ls/L,: ' + JSON.stringify(r));
  r = await backendCall("backend.imu('pitch')");
  check(r.error && /cannot report "tilt forward/.test(r.error), 'IMU pitch on today\'s firmware: says it cannot, clearly');

  // keep driving: the v command drives the motors until x
  let pwmBefore = (simLog.match(/\[pwm/g) || []).length;
  await page.evaluate(async () => {
    window.testBackend = createRobotBackend(createRunControl());
    await window.testBackend.keepDriving(40, 0, 0);
    await new Promise((r) => setTimeout(r, 700));
  });
  control('pwm');
  await waitFor(() => (simLog.match(/\[pwm/g) || []).length > pwmBefore, 'pwm while driving');
  const drivingPwm = simLog.slice(simLog.lastIndexOf('[pwm'), simLog.lastIndexOf('[pwm') + 30);
  await page.evaluate(async () => { await window.testBackend.stopMoving(); window.testBackend.finish(); });
  await sleep(400);
  pwmBefore = (simLog.match(/\[pwm/g) || []).length;
  control('pwm');
  await waitFor(() => (simLog.match(/\[pwm/g) || []).length > pwmBefore, 'pwm after stop');
  const stoppedPwm = simLog.slice(simLog.lastIndexOf('[pwm'), simLog.lastIndexOf('[pwm') + 30);
  check(/\[pwm [1-9]/.test(drivingPwm) && /\[pwm 0 0 0 0\]/.test(stoppedPwm), 'keep driving (v40,0,0;) runs the motors, stop (x) halts them: ' + drivingPwm.trim() + ' then ' + stoppedPwm.trim());

  // ---------------- ir.ino ----------------
  control('flash ' + elf.ir);
  control('irpin B 2');
  await waitFor(() => /ir v0\.10\./.test(simLog), 'ir.ino banner');
  await page.evaluate(() => { robotLink.incomingText = ''; });
  await chooseSketch('ir');
  await page.evaluate(() => { document.querySelectorAll('#programTabs .program-tab')[0].click(); });
  await sleep(300);
  movesBefore = (simLog.match(/move: done/g) || []).length;
  await runTab(0, 'Square on ir.ino (8 moves)', 60000);
  check((simLog.match(/move: done/g) || []).length - movesBefore === 8, '8 x "move: done" from ir.ino');
  control('sonar 400');
  r = await backendCall("backend.readDistance('sonar')");
  check(r.value >= 380 && r.value <= 420, 'sonar via u/U, on ir.ino: ' + JSON.stringify(r));
  const exported = await page.evaluate(async () => (await buildAllSlotsExport()).lines);
  const logBefore = simLog.length;
  await page.evaluate(async (lines) => { await sendLinesToRobot(lines, document.getElementById('exportAllFeedback')); }, exported);
  await sleep(600);
  await page.evaluate(() => sendToRobot('p?;'));
  await waitFor(() => /P,end/.test(simLog.slice(logBefore)), 'p? listing', 10000);
  const listing = simLog.slice(logBefore);
  check(/program: slot 1 = 8 steps/.test(listing) && /P,1,f50,t90,f50,t90,f50,t90,f50,t90/.test(listing) && /P,8,f40,t90,f40,t90,f40,t90,f40,t90,f40,t-90,f40,t-90,f40,t-90,f40,t-90/.test(listing) && /P,9,t-30,t60,t-30,l15,r30,l15,f15,b15,t360/.test(listing),
    'Send all nine slots: ir.ino stored and lists them back exactly');
  r = await backendCall("backend.readLight('level', 0)");
  check(r.error && /No reply starting with "B,"/.test(r.error), 'light on ir.ino: a clear fault (ir.ino has no bs)');
  pwmBefore = (simLog.match(/\[pwm/g) || []).length;
  await page.evaluate(async () => {
    window.testBackend = createRobotBackend(createRunControl());
    await window.testBackend.keepDriving(0, 40, 0);
    await new Promise((r) => setTimeout(r, 700));
  });
  control('pwm');
  await waitFor(() => (simLog.match(/\[pwm/g) || []).length > pwmBefore, 'pwm while ir.ino drives');
  const irDrivingPwm = simLog.slice(simLog.lastIndexOf('[pwm'), simLog.lastIndexOf('[pwm') + 30);
  await page.evaluate(async () => { await window.testBackend.stopMoving(); window.testBackend.finish(); });
  await sleep(400);
  pwmBefore = (simLog.match(/\[pwm/g) || []).length;
  control('pwm');
  await waitFor(() => (simLog.match(/\[pwm/g) || []).length > pwmBefore, 'pwm after ir.ino stop');
  const irStoppedPwm = simLog.slice(simLog.lastIndexOf('[pwm'), simLog.lastIndexOf('[pwm') + 30);
  check(/\[pwm [1-9]/.test(irDrivingPwm) && /\[pwm 0 0 0 0\]/.test(irStoppedPwm), 'keep driving (v0,40,0;) on ir.ino runs the motors, stop halts them: ' + irDrivingPwm.trim() + ' then ' + irStoppedPwm.trim());

  // The slots read back as blocks: a round trip
  const readBack = await page.evaluate(async () => {
    const slots = await readRobotSlots();
    switchToRobot('RoundTrip', 'test');
    replaceTabsWithRobotSlots(slots, true);
    const again = await buildAllSlotsExport();
    return { count: slots.length, firstSteps: slots[0].steps.length, lines: again.lines, tabName: programStore.programs[0].name, robot: editorSettings.robotName };
  });
  check(readBack.count === 9 && readBack.firstSteps === 8, 'readRobotSlots: nine slots, slot 1 has 8 steps');
  check(JSON.stringify(readBack.lines) === JSON.stringify(exported), 'slots -> blocks -> slots gives the same nine lines');
  check(readBack.robot === 'RoundTrip' && /^Slot 1 from RoundTrip/.test(readBack.tabName), 'tabs belong to robot "RoundTrip" and are named after the slots');
  check(await page.evaluate(() => { switchToRobot('', 'test'); return editorSettings.robotName === '' && document.getElementById('robotChipButton').textContent === 'Robot: shared ▾'; }), 'back to the shared tabs');

  // The robot's name: kN saves it, the next boot announces it, the page switches to that robot's tabs
  await page.evaluate(() => sendToRobot('kNTesty;'));
  await sleep(500);
  const bannerCount = (simLog.match(/ir v0\.10\./g) || []).length;
  control('reboot');
  await waitFor(() => (simLog.match(/ir v0\.10\./g) || []).length > bannerCount, 'ir.ino reboot banner');
  await waitFor(() => /ir: name Testy/.test(simLog), 'ir: name Testy at boot', 8000);
  await sleep(400);
  check(await page.evaluate(() => editorSettings.robotName === 'Testy' && document.getElementById('robotChipButton').textContent === 'Robot: Testy ▾' && knownRobots().indexOf('Testy') !== -1), 'the page switched to robot "Testy" on its own and remembers it');
  await page.evaluate(() => { switchToRobot('', 'test'); });
  check(dialogs.every((d) => /Which robot/.test(d)) , 'nameless robots only ever prompt "which robot" (' + dialogs.length + ' prompts)');

  check(pageErrors.length === 0, 'no page errors' + (pageErrors.length ? ': ' + pageErrors.join(' | ') : ''));
  fs.writeFileSync(path.join(build, 'sim-log.txt'), simLog);
  await browser.close();
  sim.kill();
  server.kill();
  console.log(failures.length ? `\n${failures.length} FAILED (sim log: ${path.join(build, 'sim-log.txt')})` : '\nall passed');
  process.exit(failures.length ? 1 : 0);
})().catch((error) => { console.error(error); process.exit(1); });
