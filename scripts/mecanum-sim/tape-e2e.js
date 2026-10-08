// End-to-end: the Calibrate tab's tape-measure steps (no IMU needed) against the calibration sketch
// in simavr: first a robot with no IMU at all, then one with an MPU-6050 where the person picks the
// tape anyway (the hold goes off while the tape steps drive and back on after).
// Run: node scripts/mecanum-sim/tape-e2e.js [bt]
const { spawn, execSync } = require('child_process');
const os = require('os');
const path = require('path');
const puppeteer = require('puppeteer');

const BT = process.argv[2] === 'bt';
const PORT = 8195 + (BT ? 10 : 0);
const root = path.resolve(__dirname, '..', '..');
const build = path.join(root, 'temp', 'mecanum-sim', path.basename(__filename, '.js') + (BT ? '-bt' : ''));
const simBinary = path.join(build, 'uno-sim');
const elf = path.join(build, 'out-cal', 'calibration.ino.elf');
const robotElf = path.join(build, 'out', 'mecanum_holonomic.ino.elf');   // the bump stop lives in the robot sketch
function buildEverything() {
  const fs = require('fs');
  const cli = path.join(os.homedir(), '.local', 'bin', 'arduino-cli');
  fs.mkdirSync(build, { recursive: true });
  execSync(`gcc -O2 -o "${simBinary}" "${path.join(__dirname, 'uno-sim.c')}" -lsimavr -lelf -lm`, { stdio: 'inherit' });
  require('fs').mkdirSync(path.join(build, 'calibration'), { recursive: true });
  require('fs').copyFileSync(path.join(root, 'calibration', 'calibration.ino'), path.join(build, 'calibration', 'calibration.ino'));
  execSync(`"${cli}" compile --fqbn arduino:avr:uno --output-dir "${path.join(build, 'out-cal')}" "${path.join(build, 'calibration')}"`, { stdio: 'ignore' });
  require('fs').mkdirSync(path.join(build, 'mecanum_holonomic'), { recursive: true });
  require('fs').copyFileSync(path.join(root, 'mecanum_holonomic', 'mecanum_holonomic.ino'), path.join(build, 'mecanum_holonomic', 'mecanum_holonomic.ino'));
  execSync(`"${cli}" compile --fqbn arduino:avr:uno --output-dir "${path.join(build, 'out')}" "${path.join(build, 'mecanum_holonomic')}"`, { stdio: 'ignore' });
}
const failures = [];
function check(condition, message) { console.log((condition ? 'ok   ' : 'FAIL ') + message); if (!condition) failures.push(message); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  buildEverything();
  const server = spawn('python3', ['-m', 'http.server', String(PORT), '--bind', '0.0.0.0'], { cwd: root, stdio: 'ignore' });
  const sim = spawn(simBinary, [elf, 'live'].concat(BT ? ['bt'] : []));
  let simLog = '';
  await sleep(800);
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox', '--disable-gpu'] });
  const page = await browser.newPage();
  await page.setViewport({ width: 390, height: 844, deviceScaleFactor: 1, isMobile: true, hasTouch: true });
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(e.message));
  await page.goto(`http://127.0.0.1:${PORT}/mecanum/`, { waitUntil: 'load' });
  await page.evaluate(() => { try { localStorage.clear(); } catch (e) { /* none */ } });
  await page.exposeFunction('simWrite', (bytes) => sim.stdin.write(Buffer.from(bytes)));
  sim.stdout.on('data', (chunk) => {
    simLog += chunk.toString();
    page.evaluate((bytes) => handleIncomingBytes(new Uint8Array(bytes)), [...chunk]).catch(() => {});
  });
  const control = (text) => sim.stdin.write('\x01' + text + '\n');
  const waitFor = async (fn, label, timeoutMs = 20000, arg) => {
    const started = Date.now();
    while (Date.now() - started < timeoutMs) { if (await page.evaluate(fn, arg)) return true; await sleep(100); }
    check(false, 'timed out waiting for ' + label);
    return false;
  };
  // Shown = on the page (its step may be folded); open = its step unfolded too.
  const shown = (id) => page.evaluate((i) => { const e = document.getElementById(i); return !!e && getComputedStyle(e.closest('.panel')).display !== 'none' &&
    getComputedStyle(e).display !== 'none' && !(e.closest('label') && getComputedStyle(e.closest('label')).display === 'none'); }, id);
  const openSteps = () => page.evaluate(() => [...document.querySelectorAll('#calibratePage .cal-step')]
    .filter((d) => d.open && getComputedStyle(d).display !== 'none').map((d) => d.getAttribute('data-step')).join(','));

  // ---- A robot with no IMU -------------------------------------------------
  control('dev clear');
  await page.evaluate(() => { link = { kind: 'usb', label: 'simavr', write: (b) => window.simWrite(Array.from(b)), close: () => Promise.resolve() }; setConnectedState(); });
  control('reboot');
  await waitFor(() => sensorReadDone, 'first settings read');
  await page.evaluate(() => showTab('calibrate'));
  await waitFor(() => robotCal && robotCal.live && robotRole === 'calibration', 'calibration sketch and its numbers', 20000);
  check(await page.evaluate(() => robotCal.source === 'd' && Object.keys(FLEET_DEFAULTS).every((k) => Math.abs(robotCal[k] - FLEET_DEFAULTS[k]) < 0.01)),
    "the app's fleet defaults match the sketch's (a robot never calibrated)");
  check(await openSteps() === '1', 'only step 1 open at the start: ' + await openSteps());
  check(await page.evaluate(() => !imuFound && calMode() === 'tape' && document.querySelector('#calModeButtons button[data-mode="gyro"]').disabled),
    'no IMU: tape measure, the gyro choice greyed out');
  check(await shown('tapeStartButton') && await shown('tapeForwardButton') && !await shown('autocalButton') && !await shown('straightenButton') &&
    !await shown('distanceSideInput'), 'tape steps shown, gyro steps and the sideways box hidden');

  // (over Bluetooth the settings read takes a while: the robot is busy until it is done)
  await waitFor(() => !sensorQueue.length && !sensorInFlight && busyReason === null && calRun === null, 'settings read finished', 60000);
  await page.click('#surfaceButtons button[data-surface="3"]');
  await waitFor(() => robotCal.tag === 3 && robotCal.saved === true && busyReason === null, 'surface hardwood on the robot');

  // Step 1a: tap when the spin reaches PWM 64 or more, then when the strafe reaches 110 or more
  const before = await page.evaluate(() => copyCal(robotCal));
  await page.click('#tapeStartButton');
  await waitFor(() => /^spin PWM/.test(document.getElementById('tapeStartStatus').textContent), 'nudges started', 10000);
  check(await shown('tapeMovedButton'), 'the green "It moved!" button is up');
  await waitFor(() => { const m = /^spin PWM (\d+)/.exec(document.getElementById('tapeStartStatus').textContent); return m && Number(m[1]) >= 64; }, 'spin reaches PWM 64', 40000);
  const spinTapped = await page.evaluate(() => Number(/(\d+)/.exec(document.getElementById('tapeStartStatus').textContent)[1]));
  await page.click('#tapeMovedButton');
  await waitFor(() => { const m = /^strafe PWM (\d+)/.exec(document.getElementById('tapeStartStatus').textContent); return m && Number(m[1]) >= 110; }, 'strafe reaches PWM 110', 60000);
  const strafeTapped = await page.evaluate(() => Number(/(\d+)/.exec(document.getElementById('tapeStartStatus').textContent)[1]));
  await page.click('#tapeMovedButton');
  await waitFor(() => /^(done|stopped)$/.test(document.getElementById('tapeStartStatus').textContent) && (busyReason === null && calRun === null), 'start search finished', 30000);
  let cal = await page.evaluate(() => robotCal);
  console.log('     ' + await page.evaluate(() => document.getElementById('tapeStartResult').textContent));
  check(cal.lowPwm === spinTapped && cal.deadband === spinTapped - 1, 'starts where it was tapped: low PWM ' + cal.lowPwm + ' (tapped ' + spinTapped + '), dead band ' + cal.deadband);
  const strafePwmNow = await page.evaluate(() => Math.round(firmwarePwm(robotCal.strafeStart, robotCal, robotCal.strafeStart)));
  check(Math.abs(strafePwmNow - strafeTapped) <= 1 && cal.strafeStart > cal.sLow, 'strafing starts at the tapped PWM ' + strafeTapped + ': ' + Math.round(cal.strafeStart) + ' mm/s (forward ' + Math.round(cal.sLow) + ')');
  check(cal.max === before.max && cal.gain0 === before.gain0 && cal.saved && cal.tag === 3, 'the rest untouched, saved, still hardwood');
  check(!await shown('tapeMovedButton'), 'tap button gone afterwards');

  // Step 1b: half and full power runs (ch / cf), measured 900 and 1500
  await page.click('#tapeHalfButton');
  await waitFor(() => (busyReason === null && calRun === null), 'half-power run', 15000);
  check(/calibration h: half power for 2000 ms[\s\S]*calibration: done/.test(simLog), 'half power ran for 2 s (ch)');
  await page.click('#tapeFullButton');
  await waitFor(() => (busyReason === null && calRun === null), 'full-power run', 15000);
  check(/calibration f: full power for 2000 ms[\s\S]*calibration: done/.test(simLog), 'full power ran for 2 s (cf)');
  await page.evaluate(() => { document.getElementById('tapeHalfInput').value = '900'; document.getElementById('tapeFullInput').value = '1500'; });
  await page.click('#tapeSpeedApplyButton');
  await waitFor(() => (busyReason === null && calRun === null) && robotCal.saved && /Top speed/.test(document.getElementById('tapeSpeedResult').textContent), 'speed curve applied', 40000);
  check(await openSteps() === '2', 'step 1 done: it folds, step 2 opens: ' + await openSteps());
  cal = await page.evaluate(() => robotCal);
  const seconds = 2 + before.coastMs / 2000;
  check(Math.abs(cal.max - 1500 / seconds) < 1 && Math.abs(cal.half - 900 / seconds) < 1, 'top and half speed from the tape: ' + cal.max + ' / ' + cal.half);
  check(cal.sLow > 0 && cal.sLow < cal.half && cal.half < cal.s192 && cal.s192 < cal.max, 'curve rises: ' + [cal.sLow, cal.half, cal.s192, cal.max].map(Math.round).join(' < '));
  check(Math.abs(cal.spinEff * cal.max - before.spinEff * before.max) < 1, 'spinning unchanged: spin efficiency follows the new top speed (' + cal.spinEff + ')');
  const strafePwmAfter = await page.evaluate(() => Math.round(firmwarePwm(robotCal.strafeStart, robotCal, robotCal.strafeStart)));
  check(Math.abs(strafePwmAfter - strafeTapped) <= 1, 'strafing still starts at PWM ' + strafeTapped + ' on the new curve: ' + strafePwmAfter);

  // Step 2: forward 1 m ended 40 mm right, strafe right 1 m ended 30 mm behind
  let logLength = simLog.length;
  await page.click('#tapeForwardButton');
  await waitFor(() => (busyReason === null && calRun === null), 'forward 1 m', 30000);
  check(/move: start[\s\S]*move: done/.test(simLog.slice(logLength)), 'forward 1 m driven (m0,1000,0)');
  logLength = simLog.length;
  await page.click('#tapeStrafeButton');
  await waitFor(() => (busyReason === null && calRun === null), 'strafe right 1 m', 30000);
  check(/move: start[\s\S]*move: done/.test(simLog.slice(logLength)), 'strafe right 1 m driven (m90,1000,0)');
  await page.evaluate(() => { document.getElementById('tapeForwardInput').value = '40'; document.getElementById('tapeStrafeInput').value = '-30'; });
  await page.click('#tapeStraightApplyButton');
  await waitFor(() => (busyReason === null && calRun === null) && /Wheel gains now/.test(document.getElementById('tapeStraightResult').textContent), 'trims applied', 40000);
  cal = await page.evaluate(() => robotCal);
  console.log('     ' + await page.evaluate(() => document.getElementById('tapeStraightResult').textContent));
  // Drifted right: left side strong (FL, RL down). Ended behind strafing right: front strong (FL, FR down).
  check(cal.gain0 < cal.gain1 && cal.gain0 < cal.gain2 && cal.gain3 > cal.gain1 && cal.gain3 > cal.gain2 &&
    Math.abs((cal.gain0 + cal.gain1 + cal.gain2 + cal.gain3) / 4 - 1) < 0.001,
    'FL (left and front) turned down most, RR up most, average 1: ' + [0, 1, 2, 3].map((w) => cal['gain' + w].toFixed(3)).join(' '));

  // Step 3 (opened by hand: step 2 can be run again until it is straight). It hardly moved: not applied
  await page.evaluate(() => { document.querySelector('.cal-step[data-step="3"]').open = true; });
  await page.evaluate(() => { document.getElementById('distanceMeasuredInput').value = '2'; });
  const maxNow = await page.evaluate(() => robotCal.max);
  await page.click('#distanceApplyButton');
  await sleep(500);
  check(await page.evaluate((m) => robotCal.max === m && /Not applied: 2 mm for 1000/.test(document.getElementById('distanceResult').textContent) && calRun === null, maxNow),
    '2 mm for 1000: refused, not a speed error');
  // Step 3: asked 1000, went 1100 (sideways box hidden: ignored)
  await page.evaluate(() => { document.getElementById('distanceMeasuredInput').value = '1100'; document.getElementById('distanceSideInput').value = '50'; });
  const gainsBefore = [0, 1, 2, 3].map((w) => cal['gain' + w]);
  const maxBefore = cal.max;
  await page.click('#distanceApplyButton');
  await waitFor((m) => (busyReason === null && calRun === null) && robotCal.saved && Math.abs(robotCal.max - m * 1.1) < 1, 'distance applied', 40000, maxBefore);
  cal = await page.evaluate(() => robotCal);
  check(Math.abs(cal.max - maxBefore * 1.1) < 1 && [0, 1, 2, 3].every((w) => Math.abs(cal['gain' + w] - gainsBefore[w]) < 0.002),
    'speeds x1.1, wheel gains untouched (no sideways in tape mode)');

  // Start over: two taps, the fleet defaults back, the surface kept, step 1 open
  await page.click('#calResetButton');
  check(await page.evaluate(() => /Tap again/.test(document.getElementById('calResetButton').textContent) && robotCal.max !== FLEET_DEFAULTS.max), 'first tap only arms Start over');
  await page.click('#calResetButton');
  await waitFor(() => busyReason === null && calRun === null && robotCal.saved && Object.keys(FLEET_DEFAULTS).every((k) => Math.abs(robotCal[k] - FLEET_DEFAULTS[k]) < 0.01),
    'fleet defaults back on the robot', 60000);
  check(await page.evaluate(() => robotCal.tag === 3) && await openSteps() === '1', 'Start over keeps hardwood and opens step 1');

  // ---- Same calibration sketch, now with an MPU-6050: the person picks the tape anyway ----
  await page.evaluate(() => { robotCal = null; robotRole = null; sensorReadDone = false; });
  control('dev mpu');
  control('reboot');
  await waitFor(() => sensorReadDone && robotCal && robotRole === 'calibration' && imuFound && imuHoldOn, 'robot back with an IMU, hold on', 30000);
  check(await page.evaluate(() => calMode() === 'gyro') && await shown('autocalButton') && !await shown('tapeStartButton'), 'IMU found: the gyro steps by default');
  await page.click('#calModeButtons button[data-mode="tape"]');
  await page.evaluate(() => focusStep(2));
  check(await page.evaluate(() => calMode() === 'tape') && await shown('tapeForwardButton') && !await shown('straightenButton'), 'tape picked with an IMU on board');
  const consoleBefore = await page.evaluate(() => document.getElementById('console').textContent.length);
  logLength = simLog.length;
  await page.click('#tapeForwardButton');
  await waitFor(() => (busyReason === null && calRun === null), 'forward 1 m with the IMU off', 30000);
  await waitFor(() => imuHoldOn, 'hold back on', 5000);
  const sent = await page.evaluate((n) => document.getElementById('console').textContent.slice(n), consoleBefore);
  const after = simLog.slice(logLength);
  check(/> if[\s\S]*> m0,1000,0;[\s\S]*> in/.test(sent) && /imu: present[\s\S]*move: done[\s\S]*imu: on/.test(after), 'hold off for the run (if), back on after (in)');
  if (!/move: done/.test(after)) console.log('--- console:\n' + await page.evaluate(() => document.getElementById('console').textContent.slice(-1500)) + '\n--- sim:\n' + after.slice(-800));
  check(await page.evaluate(() => imuHoldOn && imuFound), 'the robot keeps its IMU and hold afterwards');

  // ---- The robot sketch (bump stop, jolt): board upside down, step 3 in tape mode ----
  await page.evaluate(() => { robotCal = null; robotRole = null; sensorReadDone = false; });
  control('gravity -1');   // the board mounted upside down
  control('flash ' + robotElf);
  await waitFor(() => sensorReadDone && robotCal && robotRole === 'mecanum_holonomic' && imuFound && imuHoldOn, 'robot sketch running, IMU on', 30000);
  await page.evaluate(() => focusStep(3));
  logLength = simLog.length;
  await page.click('#distanceDriveButton');
  await waitFor(() => (busyReason === null && calRun === null), 'distance drive on the robot sketch', 30000);
  await waitFor(() => imuHoldOn, 'hold back on', 5000);
  const quietRun = simLog.slice(logLength);
  check(/move: done/.test(quietRun) && !/bump: stopped/.test(quietRun), 'board upside down: no false bump, the move drives to the end');
  const quiet = await page.evaluate(() => ({ chip: document.getElementById('joltChip').textContent, cls: document.getElementById('joltChip').className }));
  check(/move: done jolt \d+/.test(quietRun) && /^jolt \d+ %$/.test(quiet.chip) && !/close|fired/.test(quiet.cls), 'a quiet move: the header shows its jolt (' + quiet.chip + ')');
  check(await page.evaluate(() => /^robot \d+\.\d+\.\d+/.test(document.getElementById('firmwareCompatLabel').textContent)), 'header shows the robot\'s firmware: ' +
    await page.evaluate(() => document.getElementById('firmwareCompatLabel').textContent));

  // A rattle (one short spike) does not stop a move; a real knock does, and step 3 says so
  logLength = simLog.length;
  await page.evaluate(() => focusStep(3));
  await page.click('#distanceDriveButton');
  await waitFor(() => /move: start/.test(document.getElementById('console').textContent.slice(-300)), 'distance drive started', 5000);
  await sleep(800);
  control('spike 2');
  await waitFor(() => busyReason === null && calRun === null, 'distance drive with a rattle', 40000);
  check(/move: done/.test(simLog.slice(logLength)) && !/bump: stopped/.test(simLog.slice(logLength)), 'a 20 ms rattle is not a bump');
  check(await page.evaluate(() => /close/.test(document.getElementById('joltChip').className) && /Careful: the biggest jolt/.test(document.getElementById('distanceResult').textContent)),
    'but it came close, and the app says so: ' + await page.evaluate(() => document.getElementById('joltChip').textContent));
  logLength = simLog.length;
  await page.click('#distanceDriveButton');
  await sleep(1500);
  control('jolt 2');
  await waitFor(() => busyReason === null && calRun === null, 'distance drive with a knock', 40000);
  check(/bump: stopped[\s\S]*move: ABORTED/.test(simLog.slice(logLength)) &&
    await page.evaluate(() => document.getElementById('distanceStatus').textContent === 'stopped early' && /felt a bump/.test(document.getElementById('distanceResult').textContent)),
    'a 60 ms knock stops it, and step 3 says why');
  check(await page.evaluate(() => document.getElementById('joltChip').textContent === 'BUMP: stopped' && /fired/.test(document.getElementById('joltChip').className)),
    'header chip: BUMP: stopped');
  await page.click('#calModeButtons button[data-mode="gyro"]');
  check(await page.evaluate(() => calMode() === 'gyro'), 'and back to the gyro');

  await page.screenshot({ path: path.join(build, 'tape.png'), fullPage: false });
  check(!(await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)), 'no horizontal scroll at 390 px');
  check(pageErrors.length === 0, 'no page errors' + (pageErrors.length ? ': ' + pageErrors.join(' | ') : ''));
  await browser.close();
  sim.kill();
  server.kill();
  console.log(failures.length ? `\n${failures.length} FAILED` : '\nall passed');
  process.exit(failures.length ? 1 : 0);
})().catch((error) => { console.error(error); process.exit(1); });
