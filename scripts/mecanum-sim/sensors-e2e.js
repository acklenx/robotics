// End-to-end: Mecanum Workshop Sensors tab against the real firmware running
// in simavr (uno-sim.c, live mode). Run: node scripts/mecanum-sim/sensors-e2e.js
// Add "bt" to send everything into A0 the way the Bluetooth module does
// (SoftwareSerial, which drops bytes while the robot is replying). The page's `link` is replaced by a bridge
// to the simulator's UART, so everything above the transport is the real app.
const { spawn, execSync } = require('child_process');
const os = require('os');
const path = require('path');
const puppeteer = require('puppeteer');

const PORT = 8193 + (process.argv[2] === 'bt' ? 10 : 0);
const root = path.resolve(__dirname, '..', '..');
// Each suite (and its Bluetooth run) builds in its own folder, so they can all run at once.
const build = path.join(root, 'temp', 'mecanum-sim', path.basename(__filename, '.js') + (process.argv[2] === 'bt' ? '-bt' : ''));
const simBinary = path.join(build, 'uno-sim');
const sketchCopy = path.join(build, 'mecanum_holonomic');
const elf = path.join(build, 'out', 'mecanum_holonomic.ino.elf');

// Build the simulator and the sketch fresh every run (both need
// libsimavr-dev + libelf-dev and ~/.local/bin/arduino-cli with arduino:avr).
function buildEverything() {
  require('fs').mkdirSync(sketchCopy, { recursive: true });
  execSync(`gcc -O2 -o "${simBinary}" "${path.join(__dirname, 'uno-sim.c')}" -lsimavr -lelf -lm`, { stdio: 'inherit' });
  require('fs').copyFileSync(path.join(root, 'mecanum_holonomic', 'mecanum_holonomic.ino'), path.join(sketchCopy, 'mecanum_holonomic.ino'));
  execSync(`"${path.join(os.homedir(), '.local', 'bin', 'arduino-cli')}" compile --fqbn arduino:avr:uno --output-dir "${path.join(build, 'out')}" "${sketchCopy}"`, { stdio: 'ignore' });
}
// The laser is off by default since sketch 0.10.19 (flash): its checks run only when it is built in.
const tofBuilt = /^#define USE_TOF 1/m.test(require('fs').readFileSync(path.join(root, 'mecanum_holonomic', 'mecanum_holonomic.ino'), 'utf8'));
const sonarBuilt = /^#define USE_SONAR 1/m.test(require('fs').readFileSync(path.join(root, 'mecanum_holonomic', 'mecanum_holonomic.ino'), 'utf8'));
const failures = [];
function check(condition, message) {
  console.log((condition ? 'ok   ' : 'FAIL ') + message);
  if (!condition) failures.push(message);
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  buildEverything();
  const server = spawn('python3', ['-m', 'http.server', String(PORT), '--bind', '0.0.0.0'], { cwd: root, stdio: 'ignore' });
  const sim = spawn(simBinary, [elf, 'live'].concat(process.argv[2] === 'bt' ? ['bt'] : []));
  let simLog = '';
  await sleep(800);
  const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox', '--disable-gpu'] });
  const page = await browser.newPage();
  await page.setViewport({ width: 390, height: 900, deviceScaleFactor: 1 });
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
    while (Date.now() - started < timeoutMs) {
      if (await page.evaluate(fn)) return true;
      await sleep(100);
    }
    check(false, 'timed out waiting for ' + label);
    return false;
  };
  const queueIdle = () => sensorQueue.length === 0 && !sensorInFlight;
  const clickPreset = (containerId, name) => page.evaluate((id, n) => {
    const button = [...document.querySelectorAll('#' + id + ' button')].find((b) => b.textContent === n);
    button.click();
  }, containerId, name);

  // Connect, then reboot the Uno like opening a USB port does.
  await page.evaluate(() => {
    link = { kind: 'usb', label: 'simavr', write: (b) => window.simWrite(Array.from(b)), close: () => Promise.resolve() };
    setConnectedState();
    showTab('sensors');
  });
  control('reboot');
  await waitFor(() => sensorReadDone, 'first read after boot');
  check(await page.evaluate(() => robotSensorValues.ln === '0' && robotSensorValues.bn === '0'), 'fresh robot has no sensors');
  check(await page.evaluate(() => sensorTakenPins.a0 === 'bluetooth'), 'robot reports A0 taken by bluetooth');
  check(await page.evaluate(() => robotDemoKeys === 'k' && document.querySelectorAll('.demo-card.unavailable').length === 20 &&
    !document.querySelector('.demo-card[data-demo="pk"]').disabled && document.querySelector('.demo-card[data-demo="p1"]').disabled),
    'default build: circle strafe usable, the other 20 greyed out');
  if (sonarBuilt) check(await page.evaluate(() => document.getElementById('sonarReadout').textContent === 'ready'), 'sonar ready: nothing wired to A2/A3 yet');
  check(await page.evaluate(() => rangeStatus.textContent === 'not found' && document.getElementById('rangeDriveItem').hidden), 'no laser: range shows not found');

  await clickPreset('linePresets', 'D3 + D6 on A2 A3');
  await clickPreset('lightPresets', '2 front corners on A4 A5');
  await waitFor(queueIdle, 'presets sent');
  check(await page.evaluate(() => robotSensorValues.ln === '2' && robotSensorValues.lp0 === 'a2' && robotSensorValues.lo1 === '12'), 'line preset landed on the robot');
  check(await page.evaluate(() => robotSensorValues.bn === '2' && robotSensorValues.bp1 === 'a5' && robotSensorValues.bb0 === '-45'), 'light preset landed on the robot');
  check(await page.evaluate(() => document.querySelectorAll('#lineChannelTable select').length === 4), 'line table shows 2 channel rows');

  // A tuning slider change goes to the robot
  await page.evaluate(() => {
    const slider = document.querySelectorAll('#lineTuning input[type=range]')[0];
    slider.value = 200;
    slider.dispatchEvent(new Event('change'));
  });
  await waitFor(queueIdle, 'slider sent');
  check(await page.evaluate(() => robotSensorValues.lv === '200'), 'speed slider -> robot lv=200');
  check(await page.evaluate(() => /unsaved/.test(document.getElementById('sensorSyncStatus').textContent)), 'status says unsaved');

  await page.click('#sensorSaveButton');
  await waitFor(() => sensorQueue.length === 0 && !sensorInFlight && /saved on robot/.test(document.getElementById('sensorSyncStatus').textContent), 'save');
  check(true, 'saved');

  // Survives a reboot
  await page.evaluate(() => { robotSensorValues = {}; });
  control('reboot');
  await sleep(500);
  await waitFor(() => sensorReadDone, 'read after reboot');
  check(await page.evaluate(() => robotSensorValues.ln === '2' && robotSensorValues.lv === '200' && robotSensorValues.bp0 === 'a4'), 'settings survived a power cycle');

  // Pin map: clash shown, undo restores
  let chips = await page.evaluate(() => [...document.querySelectorAll('#pinMap .pin-chip')].map((c) => c.className + ':' + c.textContent));
  check(chips.some((c) => /line/.test(c) && /A2/.test(c)) && chips.some((c) => /light/.test(c) && /A4/.test(c)) && !chips.some((c) => /clash/.test(c)), 'pin map: A2 line, A4 light, no clash');
  await clickPreset('lightPresets', '2 front corners on A2 A3');
  await waitFor(queueIdle, 'clashing preset sent');
  chips = await page.evaluate(() => [...document.querySelectorAll('#pinMap .pin-chip.clash')].length);
  check(chips === 2, 'pin map shows 2 clashes (A2, A3)');
  check(/PIN CLASH: line sensor and light sensor both on pin a2/.test(simLog), 'robot also reported the clash');
  await page.click('#sensorUndoButton');
  await waitFor(() => sensorQueue.length === 0 && !sensorInFlight && sensorReadDone, 'undo');
  check(await page.evaluate(() => sensorForm.bp0 === 'a4' && !/unsaved/.test(document.getElementById('sensorSyncStatus').textContent)), 'undo restored the saved light pins');

  // Live reading: tape (lower voltage) under channel 0
  control('adc 2 2000');
  control('adc 3 3000');
  await page.click('#lineShowButton');
  await waitFor(() => document.getElementById('linePositionReadout').textContent === '-12', 'line reading offset -12');
  check(true, 'line reading: tape under #0 -> offset -12 mm');

  // Calibrate: robot goes busy, queue holds, calibration saved
  await page.click('#lineCalibrateButton');
  await sleep(300);
  control('adc 2 3000'); control('adc 3 2000');
  await sleep(1500);
  control('adc 2 2000'); control('adc 3 3000');
  await waitFor(() => sensorCalibrated.line && !busyReason, 'line calibration', 12000);
  check(await page.evaluate(() => document.getElementById('lineCalStatus').textContent === 'calibrated'), 'line shows calibrated');

  // Offline edits survive the read on connect
  await page.evaluate(() => { link = null; setConnectedState(); setSensorValue('bv', '220'); });
  await page.evaluate(() => {
    link = { kind: 'usb', label: 'simavr', write: (b) => window.simWrite(Array.from(b)), close: () => Promise.resolve() };
    setConnectedState();
  });
  control('reboot');
  await waitFor(() => sensorReadDone, 'read after reconnect');
  check(await page.evaluate(() => sensorForm.bv === '220' && /differs/.test(document.getElementById('sensorSyncStatus').textContent)), 'offline edit kept, status says form differs');

  const defaultsBlock = await page.evaluate(() => document.getElementById('sensorDefaultsOutput').value);
  check(/LINE_DEFAULT_COUNT = 2;/.test(defaultsBlock) && /DIRECT\(A2\), DIRECT\(A3\), NO_SOURCE/.test(defaultsBlock) && /LIGHT_DEFAULT_SPEED_MM_PER_SEC = 220;/.test(defaultsBlock), 'sketch defaults block reflects the form');

  // ---- ADS1115 boards + MPU-6050: the robot picks the ADC layout by itself ----
  for (const line of ['dev mpu', 'dev tof', 'tof 450', 'dev ads 0', 'dev ads 1', 'dev ads 2', 'ads 0 0 2000', 'ads 0 1 3000', 'ads 0 2 3000', 'ads 0 3 3000',
    'ads 1 0 3000', 'ads 1 1 3000', 'ads 1 2 3000', 'ads 1 3 3000', 'ads 2 0 1000', 'ads 2 1 4000']) control(line);
  await page.evaluate(() => { sensorOfflineEdits = {}; });
  control('reboot');
  await sleep(500);
  await waitFor(() => sensorReadDone && sensorRunningLayout === 'adc', 'ADC layout after boot with boards', 30000);
  check(await page.evaluate(() => sensorEditLayout === 'adc' && /0x48 0x49 0x4A - running the ADC layout/.test(document.getElementById('adcFoundText').textContent)), 'app shows 3 boards found, ADC layout running and edited');
  check(await page.evaluate(() => document.querySelectorAll('#lineChannelTable select').length === 16), 'ADC layout: 8 line channels listed');
  check(await page.evaluate(() => [...document.querySelector('#lineChannelTable select').options].some((o) => o.value === 'i00' && /0x48 AIN0/.test(o.textContent))), 'input picker offers ADS1115 inputs');
  check(await page.evaluate(() => /^on MPU-6050/.test(document.getElementById('imuStatusText').textContent)), 'IMU status shows the MPU-6050');
  // Over Bluetooth there is no boot banner: the settings read alone must bring the IMU in.
  await page.evaluate(() => { imuChip.textContent = ''; imuSensorStatus.textContent = 'unknown'; enqueueSensorCommand('?'); });
  await waitFor(() => /^IMU /.test(imuChip.textContent) && /MPU-6050 0x68/.test(imuSensorStatus.textContent), 'IMU from the settings read (K,imu)', 10000);
  check(true, 'K,imu: chip and Sensors-tab status without a reboot');
  await waitFor(queueIdle, 'settings read finished');
  check(await page.evaluate(() => document.getElementById('firmwareNotice').style.display === 'none'), 'no old-firmware notice (K,fw matches)');
  // No Watch: the app polls ih, so the heading moves by itself (0..360, clockwise).
  control('gyro -90');
  await sleep(1000);
  control('gyro 0');
  await waitFor(() => { const d = parseFloat(document.getElementById('imuHeadingReadout').textContent); return d > 75 && d < 105; }, 'heading ~90 without Watch', 8000);
  check(await page.evaluate(() => /^IMU (7[5-9]|[89]\d|10[0-5])/.test(imuChip.textContent)), 'live heading without Watch: ' + await page.evaluate(() => imuChip.textContent));
  // Live heading while the robot stands still and someone turns it.
  await page.click('#imuSensorWatchButton');
  await waitFor(() => imuWatching && /STOP WATCHING/.test(document.getElementById('floatingAbort').textContent), 'IMU watch started', 8000);
  control('gyro 45');
  await sleep(1000);
  control('gyro 0');
  await waitFor(() => { const d = parseFloat(document.getElementById('imuHeadingReadout').textContent); return d > 30 && d < 60; }, 'heading back ~45 (90 - 45)', 8000);
  check(await page.evaluate(() => /^IMU [3-5]\d/.test(imuChip.textContent)), 'Watch: heading follows the gyro: ' + await page.evaluate(() => imuChip.textContent));
  await page.click('#floatingAbort');
  await waitFor(() => !imuWatching && document.getElementById('floatingAbort').hidden, 'IMU watch stopped from the bar', 8000);
  check(true, 'IMU watch stopped from the STOP bar');
  if (tofBuilt) {
  check(await page.evaluate(() => rangeStatus.textContent === 'found' && !document.getElementById('rangeDriveItem').hidden), 'VL53L0X found next to the IMU and ADCs');
  await page.click('#rangeWatchButton');
  await waitFor(() => rangeStatus.textContent === 'watching' && document.getElementById('rangeReadout').textContent === '450 mm', 'range watch shows 450 mm', 8000);
  control('tof 8190');
  await waitFor(() => document.getElementById('rangeReadout').textContent === 'nothing in range', 'range follows the laser', 8000);
  check(true, 'range watch: 450 mm, then nothing in range');
  await page.click('#floatingAbort');
  await waitFor(() => rangeStatus.textContent === 'found' && document.getElementById('floatingAbort').hidden, 'range watch stopped from the bar', 8000);
  check(true, 'range watch stopped from the STOP bar');
  } else {
    console.log('skip laser checks: USE_TOF 0 in the sketch');
  }
  if (sonarBuilt) {
    control('sonar 850');
    await page.click('#sonarShowButton');
    await waitFor(() => /^8(49|50) mm$/.test(document.getElementById('sonarReadout').textContent), 'sonar reads 850 mm', 8000);
    check(true, 'sonar reading: 850 mm');
  } else {
    console.log('skip sonar checks: USE_SONAR 0 in the sketch');
  }
  // Forward driving stops short of a wall
  if (tofBuilt) {
  control('tof 300');
  await sleep(300);
  await page.evaluate(() => sendCommand('w'));
  await sleep(500);
  control('tof 90');
  await waitFor(() => /tof: wall at 90 mm/.test(document.getElementById('console').textContent) && document.getElementById('rangeDriveReadout').textContent === '90 mm', 'wall stop', 8000);
  check(/tof: wall at 90 mm\r?\nstop/.test(simLog), 'driving forward stops 90 mm from the wall');
  control('tof 8190');
  }
  await page.click('#lineShowButton');
  await waitFor(() => document.querySelectorAll('#lineBars .line-bar').length === 8, '8 line bars from the ADC layout');
  check(true, 'line reading through the ADS1115s: 8 bars');
  // Checkbox off: the robot switches to the wired layout at once
  await page.click('#adcAutoCheck');
  await waitFor(() => sensorRunningLayout === 'wired' && robotSensorValues.au === '0' && sensorQueue.length === 0 && !sensorInFlight, 'wired layout after unticking');
  check(await page.evaluate(() => robotSensorValues.au === '0' && /running the wired layout/.test(document.getElementById('adcFoundText').textContent)), 'unticking runs the wired layout');
  await page.click('#editWiredButton');
  check(await page.evaluate(() => document.querySelectorAll('#lineChannelTable select').length === 4 && /wired/.test(document.getElementById('lineLayoutName').textContent)), 'editing the wired layout shows its 2 channels');
  // An ADC-layout preset sends le/be keys, not the wired ones
  await page.click('#editAdcButton');
  await page.click('#adcAutoCheck');
  await clickPreset('lightPresets', '4 corners on 0x4A');
  await waitFor(() => sensorQueue.length === 0 && !sensorInFlight, 'ADC light preset sent');
  check(await page.evaluate(() => robotSensorValues.ben === '4' && robotSensorValues.bep3 === 'i23' && robotSensorValues.bn === '2'), 'ADC preset set ben/bep, wired bn untouched');
  const block = await page.evaluate(() => document.getElementById('sensorDefaultsOutput').value);
  check(/LIGHT_ADC_DEFAULT_COUNT = 4;/.test(block) && /ADS\(2, 3\)/.test(block), 'defaults block has the ADC layout');


  // ---- Watch: live raw numbers with the robot still; tape under channel 0 moves its number ----
  await page.evaluate(() => showTab('sensors'));
  await page.click('#lineWatchButton');
  await waitFor(() => lineStatus.textContent === 'watching' && /STOP WATCHING/.test(document.getElementById('floatingAbort').textContent), 'watching the line sensors', 8000);
  await waitFor(() => document.getElementById('lineRawReadout').textContent.split(/\s+/).length === 8, 'raw numbers for 8 channels', 8000);
  control('ads 0 0 1000');
  await waitFor(() => /^204 /.test(document.getElementById('lineRawReadout').textContent), 'channel 0 raw follows the tape', 8000);
  check(true, 'watch: 8 raw readings stream and follow the sensor');
  await page.click('#floatingAbort');
  await waitFor(() => lineStatus.textContent === 'stopped' && document.getElementById('floatingAbort').hidden, 'watch stopped from the bar', 8000);
  check(true, 'watch stopped from the STOP bar');
  control('ads 0 0 2000');

  // ---- emitters dark: every channel reads floor, follow gives up with a hint ----
  for (let chip = 0; chip < 2; chip++) for (let ch = 0; ch < 4; ch++) control('ads ' + chip + ' ' + ch + ' 5000');
  await page.click('#lineFollowButton');
  await waitFor(() => lineStatus.textContent === 'never saw line', 'never-saw-the-line hint', 8000);
  check(await page.evaluate(() => /never saw the line\. Press Watch/.test(document.getElementById('console').textContent)), 'follow without a visible line explains itself');
  for (let chip = 0; chip < 2; chip++) for (let ch = 0; ch < 4; ch++) control('ads ' + chip + ' ' + ch + ' 3000');
  control('ads 0 0 2000');

  // ---- Drive tab: follow the line, stop it from the floating bar ----
  await page.evaluate(() => showTab('drive'));
  await page.click('#driveLineFollowButton');
  await waitFor(() => !document.getElementById('floatingAbort').hidden && /STOP LINE FOLLOWING/.test(document.getElementById('floatingAbort').textContent), 'STOP bar while following', 8000);
  check(true, 'following: STOP LINE FOLLOWING bar on screen');
  await sleep(1500);   // let telemetry flow, so the stop has to get past it
  await page.click('#floatingAbort');
  await waitFor(() => document.getElementById('floatingAbort').hidden && lineStatus.textContent === 'stopped', 'line following stopped from the bar', 8000);
  check(true, 'STOP bar stopped line following and went away');

  // ---- a demo can be aborted from the floating bar ----
  await page.evaluate(() => showTab('demos'));
  await page.click('.demo-card[data-demo="pk"]');
  await waitFor(() => busyReason === 'pk' && /ABORT DEMO pk/.test(document.getElementById('floatingAbort').textContent), 'ABORT DEMO bar during pk', 8000);
  check(true, 'demo running: ABORT DEMO pk bar on screen');
  await sleep(1000);
  await page.click('#floatingAbort');
  await waitFor(() => busyReason === null && document.getElementById('floatingAbort').hidden, 'demo aborted from the bar', 8000);
  check(await page.evaluate(() => /Demo aborted/.test(document.getElementById('console').textContent)), 'robot reported the demo aborted');

  // ---- IR remote: 9 starts circle strafe, OK aborts it; hold up = drive, release = stop ----
  await sleep(500);
  control('ir 09');
  await waitFor(() => busyReason === 'pk', 'IR 9 starts demo pk', 8000);
  check(true, 'IR remote key 9 started circle strafe');
  await sleep(500);
  control('ir 1c');
  await waitFor(() => busyReason === null, 'IR OK aborts the demo', 8000);
  check(/demo pk: start[\s\S]*demo: ABORTED/.test(simLog), 'IR OK key aborted the demo');
  const stopsBefore = (simLog.match(/^stop\r?$/gm) || []).length;
  control('ir 18 700');
  await sleep(1200);
  check((simLog.match(/^stop\r?$/gm) || []).length === stopsBefore + 1, 'IR up held, then released: exactly one stop');
  control('ir 46');
  await waitFor(() => /ir: key 0x46 not mapped/.test(document.getElementById('console').textContent), 'unmapped IR key reported', 8000);
  check(true, 'unmapped IR key (2) prints its code');

  // ---- a demo starts, runs and reports done (the bug: z4p1 in one burst lost the p1) ----
  await page.evaluate(() => {
    showTab('demos');
    demoSizeSlider.value = 1;
    demoSizeSlider.dispatchEvent(new Event('input'));
    demoSizeSlider.dispatchEvent(new Event('change'));
  });
  await page.click('.demo-card[data-demo="pk"]');
  await waitFor(() => busyReason === 'pk', 'robot confirms demo pk started', 8000);
  check(true, 'demo pk started (robot said so)');
  await waitFor(() => busyReason === null && /^idle$/.test(demoStatus.textContent), 'demo pk finished', 40000);
  check(await page.evaluate(() => /Demo finished/.test(document.getElementById('console').textContent)), 'demo pk ran to "done"');
  await page.evaluate(() => showTab('sensors'));

  const overflow = await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth);
  check(!overflow, 'no horizontal scroll at 390 px');
  await page.screenshot({ path: path.join(build, 'sensors-tab.png'), fullPage: true });
  await page.setViewport({ width: 320, height: 800 });
  check(!(await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)), 'no horizontal scroll at 320 px');
  await page.evaluate(() => showTab('drive'));
  const demos = await page.evaluate(() => { showTab('demos'); return document.querySelectorAll('.demo-card polyline.path').length; });
  check(demos === 21, '21 demo cards render');
  check(pageErrors.length === 0, 'no page errors' + (pageErrors.length ? ': ' + pageErrors.join(' | ') : ''));

  await browser.close();
  sim.kill();
  server.kill();
  console.log(failures.length ? `\n${failures.length} FAILED` : '\nall passed');
  process.exit(failures.length ? 1 : 0);
})().catch((error) => { console.error(error); process.exit(1); });
