// Runs every Mecanum simulator suite, over USB and over Bluetooth, side by
// side (each in its own build folder and port), and prints a summary.
// The suites run in real time (a 2 s spin takes 2 s, Bluetooth is 9600 baud),
// so one suite cannot go faster; running them side by side can. Too many at
// once starve each other (headless Chrome + simavr each) and time out, so at
// most JOBS run together, and any failure is rerun alone once: a pass on the
// rerun is reported as flaky, not hidden.
// Run: node scripts/mecanum-sim/run-all.js [jobs]   (or: npm run test:mecanum)
//      MECANUM_JOBS=8 npm run test:mecanum
const { spawn } = require('child_process');
const path = require('path');

// (autocal and tape test the old app-driven calibration: calibration.ino is by hand now)
const SUITES = ['sensors', 'program', 'sticks', 'calpage', 'rename', 'ir', 'blocks'];
const USB_ONLY = ['ir', 'blocks'];   // (ir-e2e drives the simulator from a script; blocks-e2e points the page's serial link at it)
const JOBS = Math.max(1, parseInt(process.argv[2] || process.env.MECANUM_JOBS || '4', 10));
const runs = [];
SUITES.forEach((suite) => (USB_ONLY.includes(suite) ? ['usb'] : ['usb', 'bt']).forEach((link) => runs.push({ suite, link })));
// Longest suites first so the tail is short.
const ORDER = { calpage: 0, autocal: 1, tape: 2, blocks: 3, program: 4, sensors: 5, sticks: 6, rename: 7, ir: 8 };
runs.sort((a, b) => ORDER[a.suite] - ORDER[b.suite]);

// The two sketches must share the same motion code before any suite runs.
try {
  require('child_process').execFileSync(process.execPath, [path.join(__dirname, '..', 'check-mecanum-shared.js')], { stdio: 'inherit' });
} catch (error) {
  process.exit(1);
}

const started = Date.now();
const elapsed = () => Math.round((Date.now() - started) / 1000);

function runOne(run) {
  return new Promise((resolve) => {
    const args = [path.join(__dirname, run.suite + '-e2e.js')].concat(run.link === 'bt' ? ['bt'] : []);
    const child = spawn(process.execPath, args, { stdio: ['ignore', 'pipe', 'pipe'] });
    let output = '';
    child.stdout.on('data', (d) => { output += d; });
    child.stderr.on('data', (d) => { output += d; });
    child.on('close', (code) => resolve({
      code,
      failures: output.split('\n').filter((line) => /^FAIL|Error/.test(line)),
    }));
  });
}

function report(run, result, label) {
  console.log((result.code === 0 ? label : 'FAIL ') + run.suite + ' ' + run.link + ' (' + elapsed() + ' s)' +
    (result.failures.length ? '\n  ' + result.failures.join('\n  ') : ''));
}

async function pool(items, size, fn) {
  let next = 0;
  const worker = async () => { while (next < items.length) await fn(items[next++]); };
  await Promise.all(Array.from({ length: Math.min(size, items.length) }, worker));
}

(async () => {
  console.log('running ' + runs.length + ' suites, ' + JOBS + ' at a time');
  await pool(runs, JOBS, async (run) => {
    run.first = await runOne(run);
    report(run, run.first, 'pass ');
  });
  const retry = runs.filter((run) => run.first.code !== 0);
  if (retry.length) console.log('\nrerunning ' + retry.length + ' failed suite(s) one at a time');
  for (const run of retry) {
    run.second = await runOne(run);
    report(run, run.second, 'flaky (passed alone) ');
  }
  const failed = runs.filter((run) => run.first.code !== 0 && run.second.code !== 0);
  const flaky = retry.length - failed.length;
  console.log('\n' + (failed.length ? failed.length + ' of ' + runs.length + ' failed' : 'all ' + runs.length + ' passed') +
    (flaky ? ', ' + flaky + ' flaky' : '') + ' in ' + elapsed() + ' s');
  process.exit(failed.length ? 1 : 0);
})();
