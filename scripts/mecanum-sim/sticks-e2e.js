// End-to-end: the Mecanum Sticks page (mecanum/sticks.html) against the
// real firmware in simavr. Run: node scripts/mecanum-sim/sticks-e2e.js [bt]
// Real multi-touch goes in through the DevTools protocol, the page's link is
// a bridge to the simulator's UART, and the motor PWM is read back from the
// simulated timers, so this covers stick -> v command -> wheels.
const { spawn, execSync } = require('child_process');
const os = require('os');
const path = require('path');
const puppeteer = require('puppeteer');

const PORT = 8194 + (process.argv[2] === 'bt' ? 10 : 0);
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
  await page.setViewport({ width: 844, height: 390, deviceScaleFactor: 1, isMobile: true, hasTouch: true });
  const pageErrors = [];
  page.on('pageerror', (e) => pageErrors.push(e.message));
  await page.goto(`http://127.0.0.1:${PORT}/mecanum/sticks.html`, { waitUntil: 'load' });
  await page.exposeFunction('simWrite', (bytes) => sim.stdin.write(Buffer.from(bytes)));
  sim.stdout.on('data', (chunk) => {
    simLog += chunk.toString();
    page.evaluate((bytes) => handleIncomingBytes(new Uint8Array(bytes)), [...chunk]).catch(() => {});
  });
  const control = (text) => sim.stdin.write('\x01' + text + '\n');
  const waitFor = async (fn, label, timeoutMs = 8000) => {
    const started = Date.now();
    while (Date.now() - started < timeoutMs) {
      if (await page.evaluate(fn)) return true;
      await sleep(50);
    }
    check(false, 'timed out waiting for ' + label);
    return false;
  };
  // Motor duties M1-M4 as the simulated timers have them right now.
  const pwm = async () => {
    const before = simLog.length;
    control('pwm');
    for (let i = 0; i < 40; i++) {
      await sleep(25);
      const match = /\[pwm (\d+) (\d+) (\d+) (\d+)\]/.exec(simLog.slice(before));
      if (match) return match.slice(1).map(Number);
    }
    return null;
  };
  const moving = (duties) => !!duties && duties.some((d) => d > 0);

  // Touch helpers. Points are fractions of each stick's base radius from its centre (up positive).
  const cdp = await page.target().createCDPSession();
  const stickPoint = (id, fx, fy) => page.evaluate((stickId, x, y) => {
    const stick = stickId === 'leftStick' ? leftStick : rightStick;
    const g = stick.geometry();
    return { x: g.rect.left + g.cx + x * g.radius, y: g.rect.top + g.cy - y * g.radius };
  }, id, fx, fy);
  const touches = {};   // touch id -> { x, y }
  const sendTouches = (type) => cdp.send('Input.dispatchTouchEvent', {
    type, touchPoints: Object.keys(touches).map((id) => ({ x: touches[id].x, y: touches[id].y, id: Number(id) }))
  });
  const touchDown = async (id, point) => { touches[id] = point; await sendTouches('touchStart'); };
  const touchMove = async (id, point) => { touches[id] = point; await sendTouches('touchMove'); };
  // This Chrome releases the points listed in a touchEnd: list only the lifted finger.
  const touchUp = async (id) => {
    const lifted = touches[id];
    delete touches[id];
    await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [{ x: lifted.x, y: lifted.y, id: Number(id) }] });
  };

  // Record every command the page sends.
  await page.evaluate(() => {
    window.sentCommands = [];
    link = { kind: 'usb', label: 'simavr', write: (b) => { window.sentCommands.push(new TextDecoder().decode(b)); return window.simWrite(Array.from(b)); }, close: () => Promise.resolve() };
    setConnectedState();
  });
  control('reboot');
  await sleep(2500);
  check(/mecanum_holonomic v\d+\.\d+\.\d+/.test(simLog), "robot booted");
  check(await page.evaluate(() => document.getElementById('notice').hidden), 'no old-firmware warning for 0.10.8');

  // The robot's start speeds, from its K,cal (sketch defaults: start 161.8 of
  // 788.1 mm/s forward; strafe = the forward start x STRAFE_EFFICIENCY 0.45)
  await page.evaluate(() => { calAsked = 0; askRobotCal(); });
  await waitFor(() => robotStart.known, 'start speeds read from K,cal', 8000);
  await sleep(1500);   // the rest of the k? readout (the robot reads nothing while it talks)
  check(await page.evaluate(() => Math.abs(robotStart.forward - 161.8 / 788.1) < 0.002 && Math.abs(robotStart.strafe - 161.8 * 0.45 / 788.1) < 0.002),
    'start shares from the robot: ' + await page.evaluate(() => JSON.stringify(robotStart)));

  // ---- Analog (mode 2): right = direction, left X = turn, left Y = speed ----
  await page.evaluate(() => setMode('analog'));
  await touchDown(1, await stickPoint('rightStick', 0.3, -0.2));
  await sleep(150);
  check(await page.evaluate(() => Math.abs(rightStick.x) < 0.01 && Math.abs(rightStick.y) < 0.01), 'analog: touching off-centre re-centres the stick there (reads 0,0)');
  check(await page.evaluate(() => document.getElementById('speedReadout').textContent === '50%'), 'analog: speed 50 % with the left stick at rest');
  // right stick up at 50 %: forward, halfway up the usable range at full push
  await touchMove(1, await stickPoint('rightStick', 0.3, 0.7));
  await sleep(400);
  const forward = await page.evaluate(() => Number(document.getElementById('forwardReadout').textContent));
  check(forward >= 50 && forward <= 58, 'analog: right up at speed 50 % = forward ' + forward + '% (start 21 % .. top 60 %)');
  check(await page.evaluate(() => /^v\d+,0,0;$/.test(sentCommands[sentCommands.length - 1])), 'analog: sends v<f>,0,0;');
  let duties = await pwm();
  check(moving(duties) && duties.every((d) => d > 0), 'robot wheels turning: ' + JSON.stringify(duties));
  // keep holding: the refresh keeps it alive past the 500 ms dead-man
  await sleep(1200);
  check(moving(await pwm()), 'held for 1.2 s: refreshes keep it driving');
  // left stick all the way down: speed 0 = stop
  await touchDown(2, await stickPoint('leftStick', 0, 0));
  await touchMove(2, await stickPoint('leftStick', 0, -1.2));
  await sleep(400);
  check(await page.evaluate(() => document.getElementById('speedReadout').textContent === '0%' && sentCommands[sentCommands.length - 1] === 'v0,0,0;'), 'analog: left stick down = speed 0, v0,0,0;');
  check(!moving(await pwm()), 'robot stopped at speed 0');
  // all the way up: full
  await touchMove(2, await stickPoint('leftStick', 0, 1.2));
  await sleep(400);
  const fast = await page.evaluate(() => Number(document.getElementById('forwardReadout').textContent));
  check(fast >= 84 && fast <= 90 && moving(await pwm()), 'analog: left stick up = speed 100 %, forward ' + fast + '%');
  await touchUp(2);
  await sleep(300);
  check(await page.evaluate(() => document.getElementById('speedReadout').textContent === '50%'), 'analog: let go of the speed stick = back to 50 %');
  // lift the direction thumb (right): the robot stops
  await touchUp(1);
  await sleep(400);
  check(await page.evaluate(() => rightStick.x === 0 && rightStick.y === 0 && sentCommands[sentCommands.length - 1] === 'v0,0,0;'), 'lift right thumb: stick centres, v0,0,0; sent');
  check(!moving(await pwm()), 'robot stopped');

  // Right stick right = strafe (a touch far out deflects at once)
  await touchDown(3, await stickPoint('rightStick', 0.85, 0));
  await sleep(150);
  check(await page.evaluate(() => rightStick.x > 0.8), 'analog: touching far out deflects at once');
  await sleep(300);
  check(await page.evaluate(() => /^v0,\d+,0;$/.test(sentCommands[sentCommands.length - 1])) && moving(await pwm()), 'analog: right stick right = strafe right: ' +
    await page.evaluate(() => sentCommands[sentCommands.length - 1]));
  await touchUp(3);
  await sleep(300);
  check(!moving(await pwm()), 'lift: stops strafing');
  // a small push still moves it: never below the start speed
  await touchDown(9, await stickPoint('rightStick', 0, 0));
  await touchMove(9, await stickPoint('rightStick', 0, 0.12));
  await sleep(400);
  const nudge = await page.evaluate(() => Number(document.getElementById('forwardReadout').textContent));
  check(nudge >= 20 && moving(await pwm()), 'analog: a small push = the start speed (' + nudge + '%), wheels turning');
  await touchUp(9);
  await sleep(300);
  // left stick X alone = turn in place
  await touchDown(10, await stickPoint('leftStick', 0.85, 0));
  await sleep(400);
  check(await page.evaluate(() => /^v0,0,\d+;$/.test(sentCommands[sentCommands.length - 1])) && moving(await pwm()), 'analog: left stick right = turning in place');
  await touchUp(10);
  await sleep(300);
  check(!moving(await pwm()), 'lift: stops turning');

  // ---- Digital ----
  await page.click('#digitalButton');
  check(await page.evaluate(() => mode === 'digital' && document.querySelectorAll('#rightStick .sector').length === 6 && document.querySelectorAll('#leftStick .sector').length === 8), 'digital: 6 crab wedges right, 8 sectors left');
  // corner NE = 50 %
  await touchDown(4, await stickPoint('leftStick', 0.6, 0.6));
  await sleep(200);
  await touchUp(4);
  await sleep(200);
  check(await page.evaluate(() => digitalSpeed === 50), 'digital: left corner (top right) picks 50 %');
  check(!moving(await pwm()), 'picking a speed does not move the robot');
  // straight right = the 90 degree crab wedge
  await touchDown(5, await stickPoint('rightStick', 0.9, 0));
  await sleep(300);
  check(await page.evaluate(() => sentCommands[sentCommands.length - 1] === 'v0,55,0;'), 'digital: right wedge 90 = pure strafe right at 50 % of its usable range (v0,55,0;)');
  // nearly straight up lands in a 30 or 330 wedge: never straight forward
  await touchMove(5, await stickPoint('rightStick', 0.05, 0.9));
  await sleep(300);
  check(await page.evaluate(() => sentCommands[sentCommands.length - 1] === 'v52,30,0;'), 'digital: pushing up gives the 30 degree crab (v52,30,0;)');
  check(moving(await pwm()), 'robot crabbing');
  await touchUp(5);
  await sleep(300);
  check(!moving(await pwm()), 'lift: stops');
  // corner SW = 100 %, then left stick up = forward at 100
  await touchDown(6, await stickPoint('leftStick', -0.6, -0.6));
  await sleep(150);
  await touchMove(6, await stickPoint('leftStick', 0, 0.9));
  await sleep(300);
  check(await page.evaluate(() => digitalSpeed === 100 && sentCommands[sentCommands.length - 1] === 'v100,0,0;'), 'digital: SW corner = 100 %, then up = v100,0,0;');
  // STOP
  await page.click('#stopButton');
  await sleep(300);
  check(!moving(await pwm()) && await page.evaluate(() => sentCommands.indexOf('xx') >= 0), 'STOP button stops it');
  await touchUp(6);

  // ---- Dead-man: the page goes quiet mid-drive, the robot stops by itself ----
  await page.evaluate(() => setMode('analog'));
  await touchDown(7, await stickPoint('leftStick', 0, 0));
  await touchMove(7, await stickPoint('leftStick', 0, 0.9));
  await touchDown(8, await stickPoint('rightStick', 0, 0));
  await touchMove(8, await stickPoint('rightStick', 0, 0.9));
  await sleep(400);
  check(moving(await pwm()), 'driving again');
  await page.evaluate(() => { link = null; });   // connection gone: nothing more is sent
  await sleep(800);
  check(!moving(await pwm()), 'link lost: robot stops within 500 ms by itself');
  await touchUp(7);
  await touchUp(8);

  // ---- Old firmware warning ----
  await page.evaluate(() => handleRobotLine('mecanum_holonomic v0.10.7'));
  check(await page.evaluate(() => !document.getElementById('notice').hidden && /0\.10\.8/.test(document.getElementById('notice').textContent)), 'old firmware: page says to re-flash');

  // ---- Layout ----
  for (const size of [[844, 390], [390, 844], [320, 568]]) {
    await page.setViewport({ width: size[0], height: size[1], isMobile: true, hasTouch: true });
    await sleep(150);
    check(!(await page.evaluate(() => document.documentElement.scrollWidth > window.innerWidth || document.documentElement.scrollHeight > window.innerHeight + 1)), `no scroll at ${size[0]}x${size[1]}`);
  }
  await page.setViewport({ width: 844, height: 390, isMobile: true, hasTouch: true });
  await page.evaluate(() => setMode('digital'));
  await page.screenshot({ path: path.join(build, 'sticks-digital.png') });
  await page.evaluate(() => setMode('analog'));
  await page.screenshot({ path: path.join(build, 'sticks-analog.png') });
  check(pageErrors.length === 0, 'no page errors' + (pageErrors.length ? ': ' + pageErrors.join(' | ') : ''));

  await browser.close();
  sim.kill();
  server.kill();
  console.log(failures.length ? `\n${failures.length} FAILED` : '\nall passed');
  process.exit(failures.length ? 1 : 0);
})().catch((error) => { console.error(error); process.exit(1); });
