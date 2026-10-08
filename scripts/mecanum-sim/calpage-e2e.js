// End-to-end: mecanum/calibration.html (calibrating by hand, one number at a time) against
// calibration/calibration.ino in simavr. Step 2, start power: Find it (Yes / No answers only),
// then Check the code. Run: node scripts/mecanum-sim/calpage-e2e.js [bt]
const { spawn, execSync } = require('child_process');
const os = require('os');
const path = require('path');
const puppeteer = require('puppeteer');

const BT = process.argv[2] === 'bt';
const PORT = 8196 + (BT ? 10 : 0);
const root = path.resolve(__dirname, '..', '..');
const build = path.join(root, 'temp', 'mecanum-sim', path.basename(__filename, '.js') + (BT ? '-bt' : ''));
const simBinary = path.join(build, 'uno-sim');
const elf = path.join(build, 'out-cal', 'calibration.ino.elf');
function buildEverything() {
  const fs = require('fs');
  const cli = path.join(os.homedir(), '.local', 'bin', 'arduino-cli');
  fs.mkdirSync(path.join(build, 'calibration'), { recursive: true });
  execSync(`gcc -O2 -o "${simBinary}" "${path.join(__dirname, 'uno-sim.c')}" -lsimavr -lelf -lm`, { stdio: 'inherit' });
  fs.copyFileSync(path.join(root, 'calibration', 'calibration.ino'), path.join(build, 'calibration', 'calibration.ino'));
  execSync(`"${cli}" compile --fqbn arduino:avr:uno --output-dir "${path.join(build, 'out-cal')}" "${path.join(build, 'calibration')}"`, { stdio: 'ignore' });
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
  await page.goto(`http://127.0.0.1:${PORT}/mecanum/calibration.html`, { waitUntil: 'load' });
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
  // Wait for the question about this power, then answer it.
  const answer = async (power, yes) => {
    await waitFor(new Function('return /^Power ' + power + '[ :]/.test(document.getElementById("questionText").textContent) && !document.getElementById("questionBox").hidden'),
      'the question at power ' + power, 15000);
    await page.click(yes ? '#yesButton' : '#noButton');
  };

  // ---- Connect --------------------------------------------------------------------
  check(await page.evaluate(() => document.querySelectorAll('.step').length === 11 && document.querySelector('.step[data-step="connect"]').open),
    'eleven steps: Connect (open), Tune, Start power, Slowest speed, Faster speeds, Start and stop, Turning, Strafing, Drive straight, Slide straight, Slide creep');
  control('reboot');
  await page.evaluate(() => { link = { kind: 'usb', label: 'simavr', write: (b) => window.simWrite(Array.from(b)), close: () => Promise.resolve() }; linked(4000); });
  await waitFor(() => robot.role === 'calibration' && robot.startPower !== null && document.querySelector('.step[data-step="start"]').open, 'connected, start power step open', 25000);
  // The code's numbers, read from the robot: every expectation below is worked out from them.
  const code = await page.evaluate(() => JSON.parse(JSON.stringify(robot)));
  const P = code.startPower;
  check(P > 40 && code.deadband === P - 1 && code.source === 'd', 'reads the code\'s start power (' + P + '), dead band ' + code.deadband + ', source d');
  check(await page.evaluate((p) => new RegExp('Check the code \\(start power ' + p + '\\)').test(document.getElementById('checkButton').textContent), P), 'Check button names the code\'s number');

  // ---- Find it: no at 40-55; 60 turns it twice then not (the edge band); 65 three times -> 65 ----
  let n = simLog.length;
  await page.click('#findButton');
  check(await page.evaluate(() => document.getElementById('findButton').disabled), 'Find it is locked while a test runs');
  for (const p of [40, 45, 50, 55]) await answer(p, false);
  await answer(60, true);
  check(/^Power 60 \(try 2 of 3\)/.test(await page.evaluate(() => document.getElementById('questionText').textContent)) ||
    await waitFor(() => /^Power 60 \(try 2 of 3\)/.test(document.getElementById('questionText').textContent), 'try 2 of 3', 10000), 'a yes asks again: try 2 of 3');
  await answer(60, true);
  await answer(60, false);   // only sometimes: not good enough
  await answer(65, true);
  await answer(65, true);
  await answer(65, true);
  await waitFor(() => !document.getElementById('startResult').hidden, 'the result');
  const result = await page.evaluate(() => document.getElementById('startResult').textContent);
  check(/Start power found/.test(result) && /PWM_DEADBAND = 64;/.test(result) && /CURVE_LOW_PWM = 65;/.test(result),
    '60 worked only 2 of 3: result 65 (3 of 3), with the two code lines');
  const runs = (simLog.slice(n).match(/run: done/g) || []).length;
  check(runs === 10, 'ten spins (40 45 50 55, 60 60 60, 65 65 65): ' + runs);
  check(await page.evaluate(() => /Tried: 40: no, 45: no, 50: no, 55: no, 60: yes, 60: yes, 60: no, 65: yes, 65: yes, 65: yes/.test(document.getElementById('triedList').textContent)), 'the tries are listed');

  // ---- Check the code: the code's power three times must turn it; 5 lower twice is information ----
  await page.click('#checkButton');
  for (const [p, yes] of [[P, true], [P, true], [P, true], [P - 5, true], [P - 5, false]]) await answer(p, yes);
  await waitFor(() => !document.getElementById('startResult').hidden, 'the check result');
  const verified = await page.evaluate(() => document.getElementById('startResult').textContent);
  check(/VERIFIED/.test(verified) && new RegExp((P - 5) + ' turned it 1 of 2 times: the number sits just above the edge').test(verified), 'three of three at ' + P + ': VERIFIED, ' + (P - 5) + ' reported: ' + verified.slice(0, 160));
  await page.click('#checkButton');
  for (const [p, yes] of [[P, true], [P, false], [P, true], [P - 5, false], [P - 5, false]]) await answer(p, yes);
  await waitFor(() => !document.getElementById('startResult').hidden, 'the second check result');
  const wrong = await page.evaluate(() => document.getElementById('startResult').textContent);
  check(/NOT right/.test(wrong) && new RegExp(P + ' did not turn').test(wrong), 'a miss at ' + P + ': NOT right: ' + wrong.slice(0, 120));

  // ---- Step 3, slowest speed: 2 s and 4 s at the start power; (d4 - d2) / 2 ----------------
  await page.evaluate(() => openStep('slow'));
  n = simLog.length;
  await page.click('#run2Button');
  await waitFor(() => /Measure from the mark/.test(document.getElementById('slowWorking').textContent), 'the 2 s run', 10000);
  await page.click('#run4Button');
  await waitFor(() => /Measure from the mark/.test(document.getElementById('slowWorking').textContent) && !slowRunning, 'the 4 s run', 12000);
  check(/run: done[\s\S]*run: done/.test(simLog.slice(n)), 'both runs driven at the start power (o0,78,2000 / 4000)');
  await page.evaluate(() => { document.getElementById('run2Input').value = '900'; document.getElementById('run4Input').value = '1700'; });
  await page.click('#slowWorkButton');
  let slow = await page.evaluate(() => document.getElementById('slowResult').textContent);
  check(/Slowest speed found/.test(slow) && /SPEED_AT_LOW_PWM = 400\.0;/.test(slow) && /add 100 mm/.test(slow), '900 / 1700 mm: 400 mm/s, the code line, 100 mm extra: ' + slow.slice(0, 160));
  // the code's slowest speed measured: 2 s = 2 v, 4 s = 4 v (plus the same 20 mm) -> verified
  await page.evaluate((v) => { document.getElementById('run2Input').value = String(Math.round(2 * v + 20)); document.getElementById('run4Input').value = String(Math.round(4 * v + 20)); }, code.slowSpeed);
  await page.click('#slowWorkButton');
  slow = await page.evaluate(() => document.getElementById('slowResult').textContent);
  check(/VERIFIED/.test(slow) && new RegExp(String(Math.round(code.slowSpeed))).test(slow), 'a measurement that fits the code (' + code.slowSpeed + '): VERIFIED: ' + slow.slice(0, 120));
  await page.evaluate(() => { document.getElementById('run2Input').value = '700'; document.getElementById('run4Input').value = '800'; });
  await page.click('#slowWorkButton');
  check(/a good deal further/.test(await page.evaluate(() => document.getElementById('slowWorking').textContent)), 'numbers that cannot be right are refused');

  // ---- Step 4, faster speeds: 1 s and 2 s at 128, 192, 255 ---------------------------------
  // (code: 360 / 505 / 590)
  await page.evaluate(() => openStep('curve'));
  n = simLog.length;
  await page.click('#curve128s1');
  await waitFor(() => /Measure from the mark/.test(document.getElementById('curveWorking').textContent) && !curveRunning, 'the 128 x 1 s run', 10000);
  await page.click('#curve255s2');
  await waitFor(() => /Measure from the mark/.test(document.getElementById('curveWorking').textContent) && !curveRunning, 'the 255 x 2 s run', 10000);
  check(/run: done[\s\S]*run: done/.test(simLog.slice(n)), 'runs at 128 and 255 driven (o0,128,1000 / o0,255,2000)');
  // 15 % under the code's three speeds: not verified, the three lines; then 1 % over: verified
  const fill = (factor) => page.evaluate((f, c) => {
    [[128, c.half], [192, c.s192], [255, c.max]].forEach(([p, v]) => {
      document.getElementById('curve' + p + 's1Input').value = '300';
      document.getElementById('curve' + p + 's2Input').value = String(Math.round(300 + v * f));
    });
  }, factor, code);
  await fill(0.85);
  await page.click('#curveWorkButton');
  let curve = await page.evaluate(() => document.getElementById('curveResult').textContent);
  check(/Speeds found/.test(curve) && /HALF_POWER_MM_PER_SEC = /.test(curve) && /SPEED_AT_PWM_192 = /.test(curve) && /MAX_WHEEL_MM_PER_SEC = /.test(curve) && !/VERIFIED/.test(curve),
    '15 % under the code: the three code lines: ' + curve.slice(0, 100));
  await fill(1.01);
  await page.click('#curveWorkButton');
  curve = await page.evaluate(() => document.getElementById('curveResult').textContent);
  check(/VERIFIED/.test(curve), 'all three within 5 % of the code: VERIFIED');
  await page.evaluate((c) => { document.getElementById('curve192s2Input').value = String(Math.round(300 + c.half * 0.9)); }, code);
  await page.click('#curveWorkButton');
  check(/no faster than/.test(await page.evaluate(() => document.getElementById('curveWorking').textContent)), 'a speed out of order is refused');

  // ---- Step 5, start and stop: a real 1 m move; 930 mm -> 167 - 70/249 s = -114 ms; 1005 -> verified ----
  await page.evaluate(() => { openStep('coast'); window.scrollTo(0, 0); });
  n = simLog.length;
  await page.evaluate(() => document.getElementById('coastDriveButton').click());   // (a real click here lands on the sticky header)
  await waitFor(() => /Measure from the mark/.test(document.getElementById('coastWorking').textContent) && !coastRunning, 'the 1 m move', 30000);
  check(/move: start[\s\S]*move: done/.test(simLog.slice(n)), 'drove 1 m with the robot\'s own move (m0,1000,0)');
  check(await page.evaluate((c) => robot.coastMs === c, code.coastMs), 'reads the code\'s start and stop (' + code.coastMs + ' ms)');
  await page.evaluate(() => { document.getElementById('coastInput').value = '930'; });
  await page.click('#coastWorkButton');
  let coast = await page.evaluate(() => document.getElementById('coastResult').textContent);
  const coastExpected = Math.round(code.coastMs - 70 / code.slowSpeed * 1000);
  check(/Start and stop found/.test(coast) && new RegExp('SPIN_COAST_MS = ' + coastExpected + ';').test(coast) && /70 mm short/.test(coast), '930 mm: 70 short -> ' + coastExpected + ' ms, the code line: ' + coast.slice(0, 120));
  await page.evaluate(() => { document.getElementById('coastInput').value = '1005'; });
  await page.click('#coastWorkButton');
  coast = await page.evaluate(() => document.getElementById('coastResult').textContent);
  check(/VERIFIED/.test(coast), 'within 2 cm: VERIFIED');

  // ---- Step 6, turning: 4 x m0,0,90; 42 past -> 0.705 x 402/360 = 0.787; 3 past -> verified ----
  await page.evaluate(() => openStep('turn'));
  n = simLog.length;
  await page.click('#turnButton');
  await waitFor(() => /Look where the nose points/.test(document.getElementById('turnWorking').textContent) && !turnRunning, 'four quarter turns', 40000);
  check((simLog.slice(n).match(/move: done/g) || []).length === 4, 'four quarter turns driven (m0,0,90)');
  await page.evaluate(() => { document.getElementById('turnInput').value = '42'; });
  await page.click('#turnWorkButton');
  let turn = await page.evaluate(() => document.getElementById('turnResult').textContent);
  const spinExpected = (code.spinEff * 402 / 360).toFixed(3);
  check(/Spin factor found/.test(turn) && new RegExp('SPIN_EFFICIENCY = ' + spinExpected.replace('.', '\\.') + ';').test(turn) && /turned 402 for 360/.test(turn), '42 past: ' + code.spinEff + ' x 402/360 = ' + spinExpected + ', the code line: ' + turn.slice(0, 100));
  await page.evaluate(() => { document.getElementById('turnInput').value = '-3'; });
  await page.click('#turnWorkButton');
  check(/VERIFIED/.test(await page.evaluate(() => document.getElementById('turnResult').textContent)), 'within 5 degrees: VERIFIED');

  // ---- Step 7, strafing: m90,1000,0; 850 mm -> 0.54 x 0.85 = 0.459; 990 -> verified ---------
  await page.evaluate(() => openStep('strafe'));
  n = simLog.length;
  await page.click('#strafeButton');
  await waitFor(() => /Measure from the mark/.test(document.getElementById('strafeWorking').textContent) && !strafeRunning, 'the 1 m slide', 40000);
  check(/move: start[\s\S]*move: done/.test(simLog.slice(n)), 'slid 1 m with the robot\'s own move (m90,1000,0)');
  await page.evaluate(() => { document.getElementById('strafeInput').value = '850'; });
  await page.click('#strafeWorkButton');
  let strafe = await page.evaluate(() => document.getElementById('strafeResult').textContent);
  const strafeExpected = (code.strafeEff * 0.85).toFixed(3);
  check(/Strafe factor found/.test(strafe) && new RegExp('STRAFE_EFFICIENCY = ' + strafeExpected.replace('.', '\\.') + ';').test(strafe), '850 mm: ' + code.strafeEff + ' x 0.85 = ' + strafeExpected + ', the code line: ' + strafe.slice(0, 100));
  await page.evaluate(() => { document.getElementById('strafeInput').value = '990'; });
  await page.click('#strafeWorkButton');
  check(/VERIFIED/.test(await page.evaluate(() => document.getElementById('strafeResult').textContent)), 'within 3 cm: VERIFIED');

  // ---- Step 8, drive straight: m0,1000,0; 45 mm right -> left gains down, right up; 10 -> verified ----
  await page.evaluate(() => openStep('straight'));
  n = simLog.length;
  await page.click('#straightButton');
  await waitFor(() => /Measure how far/.test(document.getElementById('straightWorking').textContent) && !straightRunning, 'the straight 1 m drive', 30000);
  check(/move: start[\s\S]*move: done/.test(simLog.slice(n)), 'drove 1 m (m0,1000,0)');
  await page.evaluate(() => { document.getElementById('straightInput').value = '45'; });
  await page.click('#straightWorkButton');
  const straight = await page.evaluate(() => document.getElementById('straightResult').textContent);
  const gains = (/WHEEL_GAIN\[4\] = \{ ([\d.]+), ([\d.]+), ([\d.]+), ([\d.]+) \}/.exec(straight) || []).slice(1).map(Number);
  check(gains.length === 4 && gains[0] < 1 && gains[2] < 1 && gains[1] > 1 && gains[3] > 1 && Math.abs(gains[0] - gains[2]) < 0.001 &&
    Math.abs((gains[0] + gains[1] + gains[2] + gains[3]) / 4 - 1) < 0.001 && gains[1] - gains[0] > 0.02 && gains[1] - gains[0] < 0.2,
    '45 mm right: left side stronger, left gains down and right up, average 1: ' + gains.join(' '));
  check(/left side is [\d.]+ % stronger/.test(straight), 'says which side and by how much: ' + (/left side is [\d.]+ % stronger/.exec(straight) || [''])[0]);
  await page.evaluate(() => { document.getElementById('straightInput').value = '-10'; });
  await page.click('#straightWorkButton');
  check(/VERIFIED/.test(await page.evaluate(() => document.getElementById('straightResult').textContent)), 'within 2 cm: VERIFIED');

  // ---- Step 9, slide straight: m90,1000,0; the twist sets the sliding share ------------------
  await page.evaluate(() => openStep('sidestraight'));
  n = simLog.length;
  await page.click('#sideButton');
  await waitFor(() => /Write the twist/.test(document.getElementById('sideWorking').textContent) && !sideRunning, 'the sideways 1 m slide', 40000);
  check(/move: start[\s\S]*move: done/.test(simLog.slice(n)), 'slid 1 m (m90,1000,0)');
  // twisted 45 degrees LEFT (the back pushes harder) with the code's 0.017: b = -0.785 x 140 x 0.54 / (0.705 x 1000) = -0.0842, half step -> 0.017 + 0.042 = 0.059
  await page.evaluate(() => { document.getElementById('twistInput').value = '-45'; document.getElementById('sideInput').value = '300'; });
  await page.click('#sideWorkButton');
  const side = await page.evaluate(() => document.getElementById('sideResult').textContent);
  const b45 = 45 * Math.PI / 180 * code.lever * code.strafeEff / (code.spinEff * 1000);
  const biasUp = (code.frontBias + 0.5 * b45).toFixed(3), biasDown = (code.frontBias - 0.5 * b45).toFixed(3);
  check(/Sliding share found/.test(side) && new RegExp('STRAFE_FRONT_BIAS = ' + biasUp.replace('.', '\\.') + ';').test(side) && /the back pushes harder, so the front/.test(side) && /300 mm ahead: that creep is step 10/.test(side),
    '45 degrees left: the back pushes harder, bias ' + code.frontBias + ' -> ' + biasUp + ' (half step), creep noted for step 10: ' + side.slice(0, 100));
  await page.evaluate(() => { document.getElementById('twistInput').value = '45'; });
  await page.click('#sideWorkButton');
  check(new RegExp('STRAFE_FRONT_BIAS = ' + biasDown.replace('.', '\\.').replace('-', '\\-') + ';').test(await page.evaluate(() => document.getElementById('sideResult').textContent)) && /the front pushes harder, so the back/.test(await page.evaluate(() => document.getElementById('sideResult').textContent)),
    '45 degrees right: the front pushes harder, bias goes down to ' + biasDown);
  await page.evaluate(() => { document.getElementById('twistInput').value = '-3'; document.getElementById('sideInput').value = '80'; });
  await page.click('#sideWorkButton');
  check(/VERIFIED/.test(await page.evaluate(() => document.getElementById('sideResult').textContent)), 'twist within 5 degrees: VERIFIED (the creep is for step 10)');

  // ---- Step 10, slide creep (code: diag 0.101, fwd-roll 0.039): right 260 ahead, left 240 ahead -> f = 250/1000 x 0.54 = 0.135, g = 0.005 added ----
  await page.evaluate(() => openStep('creep'));
  n = simLog.length;
  await page.click('#creepRightButton');
  await waitFor(() => /right slide's box/.test(document.getElementById('creepWorking').textContent) && !creepRunning, 'the right slide', 40000);
  await page.click('#creepLeftButton');
  await waitFor(() => /left slide's box/.test(document.getElementById('creepWorking').textContent) && !creepRunning, 'the left slide', 40000);
  check((simLog.slice(n).match(/move: done/g) || []).length === 2, 'slid right (m90) and left (m-90)');
  await page.evaluate(() => { document.getElementById('creepRightInput').value = '260'; document.getElementById('creepLeftInput').value = '240'; });
  await page.click('#creepWorkButton');
  const creep = await page.evaluate(() => document.getElementById('creepResult').textContent);
  const diag1 = (code.diagBias + 0.01 * code.strafeEff).toFixed(3), roll1 = (code.fwdRollBias + 0.25 * code.strafeEff).toFixed(3);   // g = (260 - 240) / 2 / 1000, f = (260 + 240) / 2 / 1000
  check(/Slide creep found/.test(creep) && new RegExp('STRAFE_DIAG_BIAS = ' + diag1.replace('.', '\\.') + ';').test(creep) && new RegExp('STRAFE_FWD_ROLL_BIAS = ' + roll1.replace('.', '\\.') + ';').test(creep) && /weaker in reverse/.test(creep) && !/FL \+ RR/.test(creep),
    'forward both ways: weaker in reverse, fwd-roll ' + code.fwdRollBias + ' -> ' + roll1 + ', diag ' + code.diagBias + ' -> ' + diag1 + ': ' + creep.slice(0, 120));
  await page.evaluate(() => { document.getElementById('creepRightInput').value = '200'; document.getElementById('creepLeftInput').value = '-200'; });
  await page.click('#creepWorkButton');
  const creep2 = await page.evaluate(() => document.getElementById('creepResult').textContent);
  const diag2 = (code.diagBias + 0.2 * code.strafeEff).toFixed(3), roll2 = code.fwdRollBias.toFixed(3);
  check(new RegExp('STRAFE_DIAG_BIAS = ' + diag2.replace('.', '\\.') + ';').test(creep2) && new RegExp('STRAFE_FWD_ROLL_BIAS = ' + roll2.replace('.', '\\.') + ';').test(creep2) && /FL \+ RR pair pushes/.test(creep2), 'forward right, back left: the FL + RR pair, diag ' + code.diagBias + ' -> ' + diag2 + ': ' + creep2.slice(0, 120));
  await page.evaluate(() => { document.getElementById('creepRightInput').value = '10'; document.getElementById('creepLeftInput').value = '-20'; });
  await page.click('#creepWorkButton');
  check(/VERIFIED/.test(await page.evaluate(() => document.getElementById('creepResult').textContent)), 'both within 3 cm: VERIFIED');

  // ---- Tune this robot: Try (live), Save to the robot (survives a power cycle), Back to the code's numbers ----
  await page.evaluate(() => openStep('tune'));
  check(await page.evaluate(() => document.getElementById('tuneState').textContent === "the code's numbers"), 'Tune header: the code\'s numbers');
  await page.evaluate(() => { document.getElementById('tuneInput6').value = '0.650'; document.getElementById('tuneTry6').click(); });
  await waitFor(() => !tuneBusy && Math.abs(robot.spinEff - 0.65) < 0.001, 'Try spin 0.650', 10000);
  check(await page.evaluate(() => document.getElementById('tuneState').textContent === 'CHANGED, NOT SAVED' && /Not saved yet/.test(document.getElementById('tuneWorking').textContent)), 'Try: the robot uses 0.650, header says CHANGED, NOT SAVED');
  await page.evaluate(() => document.getElementById('tuneSaveButton').click());
  await waitFor(() => !tuneBusy && robot.source === 'e' && !robot.unsaved, 'Save to the robot', 10000);
  check(/config: saved/.test(simLog) && await page.evaluate(() => document.getElementById('tuneState').textContent === 'saved on the robot'), 'Save: config: saved, header says saved on the robot');
  await page.evaluate(() => { robot = freshRobot(); });
  control('reboot');
  await page.evaluate(() => linked(4000));
  await waitFor(() => robot.role === 'calibration' && robot.spinEff !== null, 'robot back after the power cycle', 25000);
  check(await page.evaluate(() => Math.abs(robot.spinEff - 0.65) < 0.001 && robot.source === 'e'), 'after the power cycle the robot still has 0.650 (saved on the robot)');
  await page.evaluate(() => { openStep('tune'); document.getElementById('tuneResetButton').click(); });
  await waitFor(() => !tuneBusy && robot.source === 'd', 'Back to the code\'s numbers', 10000);
  check(await page.evaluate((c) => Math.abs(robot.spinEff - c) < 0.001 && document.getElementById('tuneState').textContent === "the code's numbers", code.spinEff), 'Back to the code: spin ' + code.spinEff + ' again, header says the code\'s numbers');
  control('reboot');
  await page.evaluate(() => { robot = freshRobot(); linked(4000); });
  await waitFor(() => robot.role === 'calibration' && robot.spinEff !== null, 'robot back again', 25000);
  check(await page.evaluate((c) => Math.abs(robot.spinEff - c) < 0.001 && robot.source === 'd', code.spinEff), 'and it stays the code\'s after another power cycle');

  // ---- STOP in the middle ------------------------------------------------------------------
  await page.evaluate(() => { openStep('start'); window.scrollTo(0, 0); document.getElementById('findButton').click(); });   // (a real click here lands on the sticky header)
  await answer(40, false);
  await page.click('#stopButton');
  await waitFor(() => test === null && !document.getElementById('findButton').disabled, 'stopped, Find it free again', 10000);
  check(await page.evaluate(() => /Stopped/.test(document.getElementById('startWorking').textContent)), 'STOP ends the test');

  // (clicks in this test scroll the page themselves, so the page's own code is checked instead)
  const pageSource = require('fs').readFileSync(path.join(root, 'mecanum', 'calibration.html'), 'utf8');
  check(!/scrollIntoView|scrollTo\(|\.focus\(/.test(pageSource), 'the page never scrolls or moves the cursor by itself');
  check(!(await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth)), 'no horizontal scroll at 390 px');
  check(pageErrors.length === 0, 'no page errors' + (pageErrors.length ? ': ' + pageErrors.join(' | ') : ''));
  await page.screenshot({ path: path.join(build, 'calpage.png'), fullPage: true });
  await browser.close();
  sim.kill();
  server.kill();
  console.log(failures.length ? `\n${failures.length} FAILED` : '\nall passed');
  process.exit(failures.length ? 1 : 0);
})().catch((error) => { console.error(error); process.exit(1); });
