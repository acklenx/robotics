// End-to-end: ir.ino (the remote-only robot) in simavr, driven by a script of
// remote keys, sonar readings and instructor commands. No browser: the test
// reads what the sketch prints on USB and the motor PWM the simulator sees.
// Run: node scripts/mecanum-sim/ir-e2e.js
const { spawnSync, execSync } = require('child_process');
const fs = require('fs');
const os = require('os');
const path = require('path');

const root = path.resolve(__dirname, '..', '..');
const build = path.join(root, 'temp', 'mecanum-sim', 'ir-e2e');
const simBinary = path.join(build, 'uno-sim');
const sketchCopy = path.join(build, 'ir');
const elf = path.join(build, 'out', 'ir.ino.elf');

function buildEverything() {
  fs.mkdirSync(sketchCopy, { recursive: true });
  execSync(`gcc -O2 -o "${simBinary}" "${path.join(__dirname, 'uno-sim.c')}" -lsimavr -lelf -lm`, { stdio: 'inherit' });
  fs.copyFileSync(path.join(root, 'ir', 'ir.ino'), path.join(sketchCopy, 'ir.ino'));
  execSync(`"${path.join(os.homedir(), '.local', 'bin', 'arduino-cli')}" compile --fqbn arduino:avr:uno --output-dir "${path.join(build, 'out')}" "${sketchCopy}"`, { stdio: 'ignore' });
}

const failures = [];
function check(condition, message) { console.log((condition ? 'ok   ' : 'FAIL ') + message); if (!condition) failures.push(message); }

// The 17-key remote's NEC command bytes
const KEY = { U: '18', D: '52', L: '08', R: '5A', O: '1C', '*': '16', '#': '0D', 0: '19', 1: '45', 2: '46', 3: '47', 4: '44', 5: '40', 6: '43', 7: '07', 8: '15', 9: '09' };
const press = (keys, holdMs) => keys.split('').map((k) => 'ir ' + KEY[k] + (holdMs ? ' ' + holdMs : '')).join('\n');
// A marker line so the output can be cut into sections
let markers = 0;
const mark = () => '[script: --- ' + (++markers) + ' ---]';

buildEverything();
const script = [
  'irpin B 2',                       // the receiver on D10 (PB2), the sketch's default
  'wait 2600',                       // boot: the Bluetooth module gets 2 s
  'send ?\\n',
  'wait 200',
  mark(),                            // 1 drive by remote: hold UP
  press('U', 400), 'pwm', 'wait 400', 'pwm',
  mark(),                            // 2 the sonar: something 150 mm ahead
  'sonar 150', press('U', 300), 'pwm', 'wait 400',
  press('D', 300), 'pwm', 'wait 400',
  'send u\\n', 'wait 100',
  'sonar -1',
  mark(),                            // 3 a program: * * *, * UP 5 0, * 3 9 0, * * *, slot 3, * *
  press('***'), 'wait 800',
  press('*U50*'), 'wait 400',
  press('390***'), 'wait 800',
  press('3**'), 'wait 400',
  'send p?\\n', 'wait 300',
  mark(),                            // 4 run slot 3 (500 mm + 90 deg at the program speed)
  press('*3'), 'wait 300', 'pwm', 'wait 4500',
  mark(),                            // 5 run again, stop it with 0
  press('*3'), 'wait 400', press('0'), 'wait 300', 'pwm',
  mark(),                            // 6 an empty slot
  press('*4'), 'wait 800', 'pwm', 'wait 10000',
  mark(),                            // 7 the instructor: store, list, run by text; cancel with #
  'send p5=f20 t-90 b20;\\n', 'wait 200',
  'send p?\\n', 'wait 400',
  'send i***;\\n', 'wait 800',
  'send iU5;\\n', 'wait 200',
  'send i#;\\n', 'wait 800',
  'send i*5;\\n', 'wait 3500',
  mark(),                            // 8 the sonar stops a running program
  press('***'), 'wait 800', press('*U99***'), 'wait 1200', press('O'), 'wait 300', 'sonar 100', 'wait 600', 'pwm',
  'sonar -1', 'wait 200', press('#'), 'wait 800',   // (still in program mode: # leaves it)
  mark(),                            // 9 a lone * is forgotten after 3 s; # stops driving
  press('*'), 'wait 3200', press('5'), 'wait 200',
  press('U', 300), press('#'), 'wait 100', 'pwm',
  mark(),                            // 10 calibration from the remote: # # #, start power, 1 m, drift, four turns
  press('###'), 'wait 1200',
  press('D'), 'wait 700', press('U'), 'wait 700', press('O'), 'wait 600',
  press('O'), 'wait 4000', press('*'), 'wait 4000',
  press('90O'), 'wait 700',
  press('R5O'), 'wait 600',
  press('O'), 'wait 8500',
  press('R10O'), 'wait 1200',
  'send k?\\n', 'wait 1500',
  mark(),                            // 12 keep driving from the phone: v drives until it is not repeated; the sonar zeroes forward
  'send v40,0,0;\\n', 'wait 100', 'pwm', 'wait 700', 'pwm',
  'sonar 150', 'send v40,20,0;\\n', 'wait 100', 'pwm', 'send x\\n', 'sonar -1', 'wait 300',
  mark(),                            // 11 cancel puts the saved numbers back
  press('###'), 'wait 1200', press('U'), 'wait 700', press('#'), 'wait 800', 'send k?\\n', 'wait 1500',
  mark(),                            // 13 new steps: a square in three steps (forward 20, turn right, do it all again 4 times)
  press('***'), 'wait 800', press('*U20*'), 'wait 400', press('*3*'), 'wait 400', press('*84***'), 'wait 900',
  press('6**'), 'wait 600', 'send p?\\n', 'wait 1500',
  press('*6'), 'wait 300', 'wait 9000',
  mark(),                            // 14 add to slot 6 (* * * * 6): pause 2 s and diagonal left 15; text form of every new letter
  press('****6'), 'wait 900', press('*52*'), 'wait 400', press('*715*'), 'wait 400', press('***'), 'wait 800', press('6**'), 'wait 600',
  'send p7=q15 e15 p1 n2 c60;\\n', 'wait 300', 'send p?\\n', 'wait 1800',
  mark(),                            // 15 program 0: the circle
  press('*0'), 'wait 600', 'pwm', 'wait 14000',
  mark(),
  '',
].join('\n');
fs.writeFileSync(path.join(build, 'script.txt'), script);
const result = spawnSync(simBinary, [elf], { input: script, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
const output = result.stdout || '';
fs.writeFileSync(path.join(build, 'output.txt'), output + (result.stderr || ''));
const sections = output.split(/\[script: --- \d+ ---\]/);
const pwms = (text) => (text.match(/\[pwm [^\]]*\]/g) || []);   // [pwm M1 M2 M3 M4]
const moving = (line) => /[1-9]/.test(line.replace('pwm', ''));

check(/^ir v0\.10\.\d+/m.test(output) && /ir: receiver on pin 10, sonar stop at 250 mm/.test(output), 'boots as ir, receiver on pin 10, sonar stop 250 mm');
check(/^ready/m.test(sections[0]) && /i<keys>; p\?;/.test(sections[0]), 'help lists the remote and program commands');

// 1 hold UP: drives forward while held, stops 250 ms after the last repeat
let s = sections[1];
let p = pwms(s);
check(/ir: up/.test(s) && /drive: forward/.test(s), 'UP echoed and drives forward');
check(p.length === 2 && moving(p[0]) && !moving(p[1]), 'motors on while held, off after release: ' + p.join(' | '));
check(/drive: stop/.test(s), 'release stops');

// 2 sonar
s = sections[2];
p = pwms(s);
check(/stop: something ahead/.test(s) && p.length >= 1 && !moving(p[0]), 'UP with something 150 mm ahead: does not drive, says why');
check(/drive: back/.test(s) && p.length >= 2 && moving(p[1]), 'DOWN still drives with something ahead');
check(/^U,1[3-7]\d$/m.test(s), 'u reads the sonar (~150 mm)');

// 3 programming
s = sections[3];
check(/program: listening/.test(s) && /gesture: wiggle/.test(s), '* * * enters program mode with a wiggle');
check(/step 1: forward 50 cm/.test(s) && /step 2: turn right 90 deg/.test(s), 'steps understood: forward 50 cm, turn right 90 deg');
check((s.match(/gesture: nod/g) || []).length >= 3, 'a nod per step and for the save');
check(/program: 2 steps - which slot\?/.test(s), '* * * ends the program and asks for a slot');
check(/program: saved in slot 3/.test(s), '3 * * saves in slot 3');
check(/^P,3,f50,t90$/m.test(s) && /^P,1,f50,t90,n4,builtin$/m.test(s), 'p? lists slot 3 as f50,t90 and slot 1 as its built-in square');

// 4 run
s = sections[4];
p = pwms(s);
check(/run: slot 3/.test(s) && /step 1: forward 50 cm/.test(s) && /step 2: turn right 90 deg/.test(s), '* 3 runs slot 3, both steps');
check(p.length && moving(p[0]), 'motors on during the run');
check(/run: done/.test(s), 'run finishes');

// 5 stopped by a key
s = sections[5];
p = pwms(s);
check(/run: slot 3/.test(s) && /run: stopped/.test(s) && !/run: done/.test(s), '0 during a run stops it');
check(p.length && !moving(p[0]), 'motors off after the stop');

// 6 empty slot
s = sections[6];
p = pwms(s);
check(/run: slot 4 \(built-in demo\)/.test(s) && /step 1: forward 50 cm/.test(s) && /step 2: turn right 120 deg/.test(s) && p.length && moving(p[0]) && /run: done/.test(s), '* 4 with nothing saved: runs the built-in triangle');

// 7 instructor commands
s = sections[7];
check(/program: slot 5 = 3 steps/.test(s) && /^P,5,f20,t-90,b20$/m.test(s), 'p5=f20 t-90 b20 stored and listed');
check(/program: listening/.test(s) && /ir: up/.test(s) && /program: cancelled/.test(s) && /gesture: no/.test(s), 'i***; iU5; i#; types keys: program mode then cancelled');
check(/run: slot 5/.test(s) && /step 2: turn left 90 deg/.test(s) && /run: done/.test(s), 'i*5; runs slot 5 (turn left 90 included)');

// 8 the sonar stops a running program (OK = try the draft)
s = sections[8];
p = pwms(s);
check(/run: the new program/.test(s) && /step 1: forward 99 cm/.test(s), 'OK after * * * tries the new program');
check(/stop: something ahead/.test(s) && /run: stopped/.test(s), 'something 100 mm ahead stops the forward step');
check(p.length && !moving(p[0]), 'motors off after the sonar stop');
check(/program: cancelled/.test(s), '# afterwards leaves program mode');

// 9 timeouts and #
s = sections[9];
p = pwms(s);
check(!/run: slot 5/.test(s), 'a * forgotten after 3 s: 5 alone does not run slot 5');
check(/drive: forward/.test(s) && /drive: stop/.test(s) && p.length && !moving(p[0]), '# stops driving');

// 10 calibration
s = sections[10];
check(/calibrate: start/.test(s) && /gesture: wiggle/.test(s) && /calibrate: pulse at 85/.test(s), '# # # starts calibration with a wiggle and a pulse at the start power');
check(/pulse at 82/.test(s) && (s.match(/pulse at 85/g) || []).length === 2, 'DOWN pulses at 82, UP back at 85');
check(/calibrate: start power 85/.test(s) && /calibrate: 2 put it on a mark - OK drives 1 m/.test(s), 'OK keeps the start power and waits for OK before driving');
check((s.match(/calibrate: driving 1 m/g) || []).length === 2 && /calibrate: 2 how many cm/.test(s), 'OK drives 1 m, * drives it again, then asks for the cm');
check(/calibrate: speeds x 0\.900/.test(s), '90 cm typed: speeds x 0.9');
check(/calibrate: left gain x 0\.9\d\d/.test(s), 'drift RIGHT 5 cm: left wheels turned down');
check(/calibrate: 3 mark where it faces - OK turns/.test(s) && /turning right 90, four times/.test(s) && /calibrate: 3 off by\?/.test(s), 'waits for OK, four right 90s, then asks how far off');
check(/calibrate: spin efficiency 0\.62\d/.test(s), 'RIGHT 10 past: spin efficiency x 370/360');
check(/calibrate: saved on the robot/.test(s) && /^K,cal,84\.000,85\.000,301\.500,432\.000,.*,e$/m.test(s), 'saved: k? shows the new numbers from EEPROM (e)');

// 12 keep driving (v)
s = sections[11];
p = pwms(s);
check(p.length >= 3 && moving(p[0]) && !moving(p[1]), 'v40,0,0; runs the motors and they stop when it is not repeated: ' + p.slice(0, 2).join(' | '));
check(/stop: something ahead/.test(s) && moving(p[2]), 'v with something 150 mm ahead: forward zeroed and said, the sideways part still drives: ' + p[2]);

// 11 cancel
s = sections[12];
check(/calibrate: cancelled/.test(s) && /gesture: no/.test(s) && /^K,cal,84\.000,85\.000,301\.500,432\.000,.*,e$/m.test(s), '# in calibration: cancelled, saved numbers back');

// 13 diagonals / pause / repeat
s = sections[13];
check(/step 3: do it all again 4 times/.test(s) && /program: saved in slot 6/.test(s) && /^P,6,f20,t90,n4$/m.test(s), '* 8 4 is a repeat step; slot 6 lists as f20,t90,n4');
check((s.slice(s.indexOf('run: slot 6')).match(/step 1: forward 20 cm/g) || []).length === 4 && /again: 3 more/.test(s) && /again: 1 more/.test(s) && /run: done/.test(s), 'running it goes round four times');

// 14 add mode and the text letters
s = sections[14];
check(/program: adding to slot 6 \(3 steps\)/.test(s) && /gesture: wiggle[\s\S]*gesture: nod/.test(s), '* * * * 6: adding to slot 6 with a wiggle and a nod');
check(/step 4: wait 2 s/.test(s) && /step 5: diagonal left 15 cm/.test(s) && /^P,6,f20,t90,n4,p2,q15$/m.test(s), 'added a pause and a diagonal; slot 6 lists all five');
check(/program: slot 7 = 5 steps/.test(s) && /^P,7,q15,e15,p1,n2,c60$/m.test(s), 'p7=q15 e15 p1 n2 c60 round-trips through the text form');

// 15 the circle
s = sections[15];
p = pwms(s);
check(/run: program 0 \(circle, 1 m across, facing in\)/.test(s) && /step 1: circle, facing in, 100 cm across/.test(s) && p.length && moving(p[0]) && /run: done/.test(s), '* 0 runs the 1 m circle to the end');

console.log(failures.length ? '\n' + failures.length + ' FAILED (see ' + path.join(build, 'output.txt') + ')' : '\nall passed');
process.exit(failures.length ? 1 : 0);
