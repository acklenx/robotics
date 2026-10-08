// End-to-end: the Calibrate tab's surfaces, speed curve, drive-straight and distance steps, against
// the firmware in simavr with a modelled robot. Run: node scripts/mecanum-sim/autocal-e2e.js [bt]
const { spawn, execSync } = require('child_process');
const os = require('os');
const path = require('path');
const puppeteer = require('puppeteer');

const PORT = 8191 + (process.argv[2] === 'bt' ? 10 : 0);
const root = path.resolve(__dirname, '..', '..');
// Each suite (and its Bluetooth run) builds in its own folder, so they can all run at once.
const build = path.join(root, 'temp', 'mecanum-sim', path.basename(__filename, '.js') + (process.argv[2] === 'bt' ? '-bt' : ''));
const simBinary = path.join(build, 'uno-sim');
// Both programs: the calibration sketch (generated from the robot sketch) runs
// the calibration, then the robot sketch is "uploaded" over it, EEPROM kept.
const elf = path.join(build, 'out-cal', 'calibration.ino.elf');
const robotElf = path.join(build, 'out', 'mecanum_holonomic.ino.elf');
function buildEverything() {
  const fs = require('fs');
  const cli = path.join(os.homedir(), '.local', 'bin', 'arduino-cli');
  fs.mkdirSync(build, { recursive: true });
  execSync(`gcc -O2 -o "${simBinary}" "${path.join(__dirname, 'uno-sim.c')}" -lsimavr -lelf -lm`, { stdio: 'inherit' });
  // The calibration sketch generated straight from the robot sketch, as it is now
  require('fs').mkdirSync(path.join(build, 'calibration'), { recursive: true });
  require('fs').copyFileSync(path.join(root, 'calibration', 'calibration.ino'), path.join(build, 'calibration', 'calibration.ino'));
  fs.mkdirSync(path.join(build, 'mecanum_holonomic'), { recursive: true });
  fs.copyFileSync(path.join(root, 'mecanum_holonomic', 'mecanum_holonomic.ino'), path.join(build, 'mecanum_holonomic', 'mecanum_holonomic.ino'));
  for (const [name, out] of [['calibration', 'out-cal'], ['mecanum_holonomic', 'out']]) {
    const copy = path.join(build, name);
    execSync(`"${cli}" compile --fqbn arduino:avr:uno --output-dir "${path.join(build, out)}" "${copy}"`, { stdio: 'ignore' });
  }
}
const failures = [];
function check(condition, message) { console.log((condition ? 'ok   ' : 'FAIL ') + message); if (!condition) failures.push(message); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// The sketch's speed curve (wheelSpeedToPwm before the trim), for a share of top speed.
function firmwarePwmFor(share, cal) {
  const pts = [[cal.deadband, 0], [cal.lowPwm, cal.sLow], [128, cal.half], [192, cal.s192], [255, cal.max]];
  const speed = share * cal.max;
  let i = 1;
  while (i < 4 && speed > pts[i][1]) i++;
  return pts[i - 1][0] + (speed - pts[i - 1][1]) / (pts[i][1] - pts[i - 1][1]) * (pts[i][0] - pts[i - 1][0]);
}

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

  // Calibrate tab: the robot reports the numbers it drives with (K,cal, live since 0.10.19)
  await page.evaluate(() => showTab('calibrate'));
  await waitFor(() => robotCal && robotCal.live && robotCal.max === 788.1 && robotCal.deadband === 71 && robotCal.source === 'd' && robotRole === 'calibration' && imuFound,
    'calibration sketch, fleet defaults (K,cal, K,role) and the IMU known');
  check(true, 'robot numbers: ' + await page.evaluate(() => JSON.stringify(robotCal)));
  check(await page.evaluate(() => /fleet defaults/.test(document.getElementById('robotCalStatus').textContent) &&
    getComputedStyle(document.getElementById('autocalButton')).display !== 'none'), 'status says fleet defaults; calibration steps shown');
  await page.click('#autocalButton');
  check(await page.evaluate(() => /Pick the surface/.test(document.getElementById('autocalButton').textContent)), 'steps ask for a surface first');
  await sleep(1600);

  // Pick hardwood: no numbers for it yet, the robot keeps its own, labelled hardwood, saved
  await page.click('#surfaceButtons button[data-surface="3"]');
  await waitFor(() => robotCal.tag === 3 && robotCal.saved === true && robotCal.source === 'e' && busyReason === null, 'surface hardwood on the robot, in EEPROM');
  check(/config: saved/.test(simLog), 'kw saved it on the robot');
  check(await page.evaluate(() => /hardwood numbers/.test(document.getElementById('robotCalStatus').textContent) &&
    document.querySelector('#surfaceButtons button[data-surface="3"]').classList.contains('active')), 'status and button show hardwood');

  // Step 1: a robot whose gyro answers its motors: 240 deg/s at full power, 150 ms coast, curved like
  // a gear motor, and sticky like the real one: below PWM 66 it does not start at all
  control('spinmodel 240 150 66');
  control('strafemodel 110 400 120');   // strafing needs duty 110 on every wheel (the rug), forward 66
  let t0 = Date.now();
  await page.click('#autocalButton');
  await waitFor(() => /^PWM/.test(document.getElementById('autocalStatus').textContent), 'speed curve started', 8000);
  check(await page.evaluate(() => /STOP CALIBRATION RUN|STOP/.test(document.getElementById('floatingAbort').textContent)), 'STOP bar during the speed curve');
  await waitFor(() => /^(done|stopped)$/.test(document.getElementById('autocalStatus').textContent) && busyReason === null, 'speed curve finished', 150000);
  const table = await page.evaluate(() => document.getElementById('autocalTable').textContent);
  const spins = table.split('\n').length;
  console.log('     took ' + Math.round((Date.now() - t0) / 1000) + ' s, ' + spins + ' spins: ' + table.replace(/\n/g, ' | '));
  console.log('     ' + await page.evaluate(() => document.getElementById('autocalResult').textContent));
  let cal = await page.evaluate(() => robotCal);
  check(spins <= 8, 'at most 8 spins: ' + spins);
  check(cal.lowPwm >= 66 && cal.lowPwm <= 71 && cal.deadband < cal.lowPwm, 'robot now starts at the model stall (66): low PWM ' + cal.lowPwm + ', dead band ' + cal.deadband);
  check(Math.abs(cal.half / cal.max - 0.64) < 0.07, 'half power ~64% of full (model sqrt curve): ' + (cal.half / cal.max).toFixed(3));
  check(cal.coastMs > 100 && cal.coastMs < 220, 'coast ~150 ms (model): ' + cal.coastMs);
  check(Math.abs(cal.spinEff - 240 * Math.PI / 180 * 140 / cal.max) < 0.06, 'spin efficiency from the full-power spin: ' + cal.spinEff);
  check(cal.saved === true && cal.tag === 3, 'the robot runs and keeps them (saved, still hardwood)');

  // Step 2: left side and front strong (FL 1.1, FR 0.95, RL 1.0, RR 0.85), rear-right sticks 8 PWM later
  control('wheels 1.1 0.95 1.0 0.85 0 0 0 8');
  t0 = Date.now();
  await page.click('#straightenButton');
  await waitFor(() => /^(straight|close|stopped)$/.test(document.getElementById('straightenStatus').textContent) && busyReason === null && calRun === null, 'drive straight finished', 400000);
  console.log('     took ' + Math.round((Date.now() - t0) / 1000) + ' s: ' + await page.evaluate(() => document.getElementById('straightenTable').textContent.replace(/\n/g, ' | ')));
  console.log('     ' + await page.evaluate(() => document.getElementById('straightenResult').textContent));
  cal = await page.evaluate(() => robotCal);
  check(await page.evaluate(() => document.getElementById('straightenStatus').textContent === 'straight'), 'drives straight in the end');
  // Strafe start: duty 110 plus the rear-right wheel's 8 = 118 before it slides
  const strafeLine = await page.evaluate(() => (/strafing starts at PWM (\d+)/.exec(document.getElementById('straightenTable').textContent) || [])[1]);
  check(Number(strafeLine) >= 118 && Number(strafeLine) <= 123 && cal.strafeStart > cal.sLow * 1.2,
    'strafe start found with the accelerometer: PWM ' + strafeLine + ' (model 118), wheel speed ' + Math.round(cal.strafeStart) + ' vs forward ' + Math.round(cal.sLow));
  // Each wheel's real speed in the model at a mid power, with its start offset and gain
  const evened = [0.4, 0.65].map((share) => {
    const pwm = firmwarePwmFor(share, cal);
    return [1.1, 0.95, 1.0, 0.85].map((strength, w) => {
      const p = pwm + cal['start' + w] + (pwm - cal.deadband) * (cal['gain' + w] - 1);
      return strength * Math.sqrt((p - 40) / 215);
    });
  });
  const spread = Math.max(...evened.map((speeds) => Math.max(...speeds) / Math.min(...speeds)));
  // (the diagonal pairs, FL + RR against FR + RL, turn nothing: the gyro cannot see them; step 3 sideways does)
  check(cal.gain0 < 1 && cal.gain3 > 1 && spread < 1.06,
    'strong wheels turned down, weak up: at 40 % and 65 % the four wheels are within ' + ((spread - 1) * 100).toFixed(1) + ' % (trims ' +
    [0, 1, 2, 3].map((w) => cal['start' + w].toFixed(1) + '/x' + cal['gain' + w].toFixed(3)).join(' ') + ')');
  check(/imu: /.test(simLog.split('drive straight').pop() || simLog) && await page.evaluate(() => /> in/.test(document.getElementById('console').textContent)), 'heading hold back on afterwards');

  // Step 3: asked 1000, went 1100 and 30 mm right
  await page.evaluate(() => { document.getElementById('distanceMeasuredInput').value = '1100'; document.getElementById('distanceSideInput').value = '30'; });
  const beforeMax = cal.max, beforeSpin = cal.spinEff;
  await page.evaluate((m) => { window.testTarget = m * 1.1; }, beforeMax);
  await page.click('#distanceApplyButton');
  await waitFor(() => busyReason === null && robotCal.saved === true && Math.abs(robotCal.max - window.testTarget) < 1, 'distance applied', 40000) ||
    console.log(await page.evaluate(() => document.getElementById('console').textContent.slice(-1200)));
  cal = await page.evaluate(() => robotCal);
  check(Math.abs(cal.max - beforeMax * 1.1) < 1 && Math.abs(cal.spinEff - beforeSpin / 1.1) < 0.002, 'speeds x1.1, spin efficiency /1.1: ' + cal.max + ' ' + cal.spinEff);
  check(await page.evaluate(() => /using it now/.test(document.getElementById('distanceResult').textContent)), 'says the robot is using it now');

  // Another surface, then back: hardwood's numbers come back from this browser
  await page.click('#surfaceButtons button[data-surface="1"]');
  await waitFor(() => robotCal.tag === 1 && busyReason === null && robotCal.saved, 'carpet picked');
  const hardwoodMax = await page.evaluate(() => robotCal.max);
  await page.evaluate(() => { var m = robotCal; return sendMotion(Object.assign(copyCal(m), { max: 500 })); });
  await page.click('#surfaceButtons button[data-surface="3"]');
  if (!await waitFor(() => robotCal.tag === 3 && busyReason === null && robotCal.saved, 'hardwood picked again', 60000)) {
    console.log(await page.evaluate(() => document.getElementById('console').textContent.slice(-1500)));
  }
  check(await page.evaluate((m) => Math.abs(robotCal.max - m) < 0.01, hardwoodMax), 'hardwood numbers back on the robot: max ' + await page.evaluate(() => robotCal.max));

  // The real robot's numbers (2026-10-01): stalled at 62 / 64, 46 deg/s at 72
  const real = await page.evaluate(() => analyseAutocal([[62, 0, 0], [64, 0, 0], [72, 46, 4.8], [128, 119, 16.2],
    [192, 172, 28.7], [255, 224, 49.3]].map((p) => ({ pwm: p[0], rate: p[1], coastDeg: p[2] })), 72, 64));
  check(real.lowPwm === 72 && real.deadband === 71 && Math.abs(real.halfShare - 0.531) < 0.01, 'real robot data: low PWM ' + real.lowPwm + ', dead band ' + real.deadband + ', half ' + real.halfShare.toFixed(3));

  // STOP part-way (still on the calibration sketch; step 1 folded by now: open it)
  await page.evaluate(() => focusStep(1));
  await page.click('#autocalButton');
  const keep = await page.evaluate(() => robotCal.max);
  await waitFor(() => /^PWM (6|7|8|9|1)/.test(document.getElementById('autocalStatus').textContent), 'second speed curve under way', 30000);
  await page.click('#floatingAbort');
  await waitFor(() => document.getElementById('autocalStatus').textContent === 'stopped' && calRun === null, 'speed curve stopped', 20000);
  check(await page.evaluate((k) => robotCal.max === k && robotCal.saved === true, keep), 'STOP aborts it and puts the old numbers back');

  // Upload the robot sketch over it: EEPROM stays, so it drives on the calibrated numbers
  const calibrated = await page.evaluate(() => JSON.stringify(MOTION_KEYS.map((k) => robotCal[k])));
  await page.evaluate(() => { robotCal = null; robotRole = null; sensorReadDone = false; });
  control('flash ' + robotElf);
  await waitFor(() => robotRole === 'mecanum_holonomic' && robotCal && robotCal.source === 'e', 'robot sketch running, numbers from EEPROM', 30000);
  check(await page.evaluate((c) => JSON.stringify(MOTION_KEYS.map((k) => robotCal[k])) === c, calibrated), 'the robot sketch reads exactly the calibrated numbers');
  check(await page.evaluate(() => getComputedStyle(document.getElementById('autocalButton').closest('.panel')).display === 'none' &&
    getComputedStyle(document.querySelector('.robot-only')).display !== 'none'), 'robot sketch: calibration steps hidden, the re-check note shown');
  // A small correction from the re-check: went 980 for 1000
  await page.evaluate(() => { document.getElementById('distanceMeasuredInput').value = '980'; document.getElementById('distanceSideInput').value = ''; });
  const maxBefore = await page.evaluate(() => (window.testTarget = robotCal.max * 0.98) / 0.98);
  await page.click('#distanceApplyButton');
  await waitFor(() => busyReason === null && robotCal.saved && Math.abs(robotCal.max - window.testTarget) < 0.5, 're-check adjustment on the robot sketch', 30000);
  check(true, 're-check adjusts the robot sketch: max ' + maxBefore + ' -> ' + await page.evaluate(() => robotCal.max));
  await page.screenshot({ path: path.join(build, 'autocal.png'), fullPage: false });
  check(!(await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)), 'no horizontal scroll at 390 px');
  check(await page.evaluate(() => document.getElementById('appVersionLabel').textContent === 'v' + APP_VERSION && document.title.indexOf(APP_VERSION) > 0),
    'header and title show ' + await page.evaluate(() => APP_VERSION));
  check(pageErrors.length === 0, 'no page errors' + (pageErrors.length ? ': ' + pageErrors.join(' | ') : ''));
  await browser.close();
  sim.kill();
  server.kill();
  console.log(failures.length ? `\n${failures.length} FAILED` : '\nall passed');
  process.exit(failures.length ? 1 : 0);
})().catch((error) => { console.error(error); process.exit(1); });
