# Mecanum Workshop (4WD holonomic robot)

Separate web app for the 4WD mecanum kit. The 2WD kit keeps its own apps
(`/workshop.html`, and the older `/arduinobot/remote.html`); nothing here
touches them. Everything here moved from `/arduinobot/mecanum/` (old URLs
redirect).

| Robot | URL |
|---|---|
| Robot hub (mecanum, links to everything) | `/` |
| 2WD LAFVIN (TB6612 shield) | `/2wd.html` (hub), `/workshop.html`, `/arduinobot/remote.html` |
| 4WD mecanum | `/mecanum/` |
| 4WD mecanum, two-stick controller | `/mecanum/sticks.html` |
| 4WD mecanum lesson plans (10, noindex) | `/mecanum/lessons/` |
| 4WD mecanum from the IR remote alone (ir.ino): instructions + instructor console | `/mecanum/ir.html` |
| 4WD mecanum Blocks editor (tabs, subroutines, simulator, slot export; Blockly 13.2.1 and JS-Interpreter in `vendor/`: programs run in an interpreter, no eval, because the site's CSP forbids it; `node scripts/check-mecanum-blocks.js` tests it under that CSP) | `/mecanum/blocks.html` |

`index.html` is self-contained (no build, no dependencies). It talks to
`../mecanum_holonomic/mecanum_holonomic.ino` (v0.10.8) over either:

- **Bluetooth** via Web Bluetooth. The kit's module is silk-screened HC-06 and
  is dual-mode: the LAFVIN app (classic SPP, per the APK) uses one side, this
  page uses the BLE side: service FFE0, notify on FFE1, writes on FFE2. The
  page discovers the UART service (FFE0 or Nordic UART), picks a writable and
  a notify characteristic from their properties, chooses write-with or
  without response accordingly and flips once if the first write fails, and
  lists everything it found in the console. Writing to FFE1 (notify-only) is
  what produced "GATT operation failed for unknown reason" before.
- **Serial** via Web Serial (desktop Chrome/Edge): the USB cable, or the
  HC-06's classic side once paired in the OS (PIN 1234). Opening the USB port
  resets the Uno, so the first thing in the console is the firmware banner.

The sketch reads commands from USB and from a SoftwareSerial port on A0/A1,
where the factory sketch has the Bluetooth module, and echoes every reply to
both, so it works over the phone right after flashing with no rewiring.

Both need the page served from `http://localhost` or `https://`, never `file://`.

## Tabs

- **Drive**: 3x3 hold-to-drive pad (w/s/a/d translate, q/e spin, x stop),
  keyboard equivalents, speed 1-9. Release always sends `x` twice because the
  firmware latches manual drives.
- **Watch** (`lw` / `bw`, Sensors tab) streams `LR,raw...` and `L,...` (or
  `BR` / `B`) 5 times a second with the robot standing still, for checking
  wiring: tape under a channel should move its raw number by 100 or more. If
  following stops because it never saw the line, the robot says so and the
  app points to Watch.
- **Drive** also has Follow line (`lf`) and Seek light (`bf`) buttons. While
  the robot drives itself (demo, calibration run, following, seeking) a red
  STOP bar is pinned to the bottom of the screen on every tab. STOP resends
  `x` every 250 ms until the robot confirms (telemetry can swallow a single
  stop over Bluetooth), and any new command cancels those resends.
- **Demos**: one card per firmware demo. Every build has pk (circle strafe
  facing in), pc (crab zigzag), pd (J-turn), pj (parallel park) and pu (turn
  around), shown first; the other 16 (p1-p9, pa pb pe-pi) need
  `USE_EXTRA_DEMOS 1` plus one other feature off to fit. The robot
  lists its demos (`demos kcdju` in its help, `K,demos` in a settings read)
  and the app greys out the cards it does not have. A size slider (25-200 %) scales every demo distance and
  orbit radius; the app sends `z1`..`z8` right before each `p<key>`, and the
  previews and floor sizes redraw at that size. The path previews are computed
  in the page from the same primitives the sketch uses (`PathSim`), so they show
  the shape, the heading over time, and the floor space needed. Keep
  `DEMO_SPEED_MM_PER_SEC`, `DEMO_SQUARE_SIDE_MM` and `DEFAULT_SPIN_DEG_PER_SEC`
  in the script in step with the sketch's CONFIG block if those change.
- **Sensors**: sets up the line follower and the light seeker with no
  re-flash. Presets for common wirings, a row per channel (pin, board output
  D1-D8 / bearing, offset mm), polarity, and tuning sliders (speed, slide and
  turn correction, thresholds, lost-stop time). Every change is sent at once
  as a `k<key>=<value>;` command, so gains can be tuned while it follows;
  **Save to robot** (`kw;`) keeps them in EEPROM. A top-view map shows where
  each sensor sits and draws the live line offset and light direction; a pin
  map shows A0-A5 owners and clashes. On connect the app reads the robot
  (`k?;`) once its help text has finished. Commands go one at a time, each
  waiting for its `K,` echo, because the Bluetooth port is SoftwareSerial and
  drops incoming bytes while the robot is sending. Also generates the
  sketch's CONFIG defaults block for flashing more robots the same way.
  Two layouts are kept: **wired** (Uno pins / mux) and **ADC** (ADS1115
  inputs). A checkbox (`au`) lets the robot run the ADC layout when every
  ADS1115 it uses answers at start-up; the panel shows the boards found and
  which layout is running, and a switch picks which layout the wiring panels
  edit. Probe again sends `kp;`. A distance panel shows whether a VL53L0X
  was found, with Laser (`rs`), Sonar (`ru`) and Watch (`rw`, both); the
  Drive tab shows the distance ahead when a laser is fitted.
- **Calibrate**: step 0 twitches shield channels M1-M4 (`t0`-`t3`) so the
  `wheelWiring[]` table (which corner each channel drives, and whether it is
  inverted) can be filled in. Calibration is its own program:
  `calibration/calibration.ino` (calibration only: tape runs, twitch,
  moves, the gyro; the code it shares with the robot sketch is kept identical,
  `node scripts/check-mecanum-shared.js` compares it). Flash it, calibrate, flash `mecanum_holonomic.ino`
  back: the numbers live in EEPROM (address 920: mark, layout byte, 18
  floats), which uploads never touch, and the robot sketch reads them at
  power-on (`K,cal,...,e`); never calibrated, it runs the fleet defaults in
  its CONFIG (`,d`). `CALIBRATION_IN_EEPROM 0` bakes a pasted set into one
  build instead (`,c`, ~0.5 KB smaller). The app writes live
  (`km<i>=<value>;` answers `K,cal,...`, `kw;` saves) and knows which program
  runs from `K,role`. On the robot sketch only the re-check (step 3) and
  surface switching are offered. Pick the surface (carpet, rug, hardwood,
  tile, concrete; each keeps its own numbers, per robot name, in this
  browser), then: 1 speed curve (spins; a short search for the PWM where the
  motors start, then 128 / 192 / 255 and the coast), 2 drive straight
  (forward / back / strafe both ways, slow and fast, heading hold off; the
  gyro's turn rate gives each wheel a start offset and a gain, repeated until
  the sides are within 3 %), 3 distance (drive, measure along and sideways,
  Apply scales every speed and evens the diagonal pairs). The tape-measure
  runs (`cf` / `ch` / `cs` / `cr`) and a block to paste into the sketch (to
  flash more robots) are still there, folded away.

## Two-stick controller (`sticks.html`)

A mode 2 controller, separate from the Workshop app (same Bluetooth / Serial
transports, needs firmware 0.10.8+). Every 50 ms it turns both sticks into one
body velocity and sends `v<forward>,<right>,<spin>;` (each -100..100 % of full
wheel speed): on change at most every 80 ms, and again every 150 ms while
held. The command has no reply, and the robot stops by itself if none
arrives for 500 ms, so a lost link or a hidden tab cannot leave it driving.
Release sends `v0,0,0;` twice; STOP sends `xx`.

- **Analog**: left Y up = speed (below centre = 0), left X = turn (on its
  own, up to 60 % of full speed), right stick = which way to slide, scaled
  by the speed. A touch inside 60 % of a stick's radius becomes that stick's
  centre (drawn as a dashed ring); further out it deflects at once. Lifting
  a thumb returns that stick to centre. 8 % dead zone, then a 1.5 power
  curve.
- **Digital**: right stick = six crab wedges centred on 30, 90, 150, 210,
  270 and 330 degrees, so there is no straight forward or back. Left stick =
  forward / back / turn, and its corners pick the speed (top left 25 %, top
  right 50 %, bottom right 75 %, bottom left 100 %, remembered). Both sticks
  add up.
- The mode and the digital speed are kept in localStorage.
- `node scripts/mecanum-sim/sticks-e2e.js [bt]` drives it with real
  multi-touch (DevTools touch events) against the firmware in simavr and
  reads the motor PWM back from the simulated timers: re-centring, speed
  without direction, forward, turning in place, the wedges and corners,
  STOP, the 500 ms dead-man after the link goes, and the old-firmware warning.

## Hardware the sketch assumes

- Motor driver: the L293D motor shield (two L293D + one 74HC595), the Adafruit
  Motor Shield v1 layout. Shift register on 8/4/12/7, PWM M1-M4 on 11/3/6/5.
  Free pins: 2, 13, A0-A5, plus the servo headers on 9/10.
- Chassis: wheelbase 150 mm, track 130 mm, 60 mm wheels.
- Optional IMU on A4/A5 (I2C): a BNO055 (0x28/0x29, the demo unit) or an
  MPU-6050 (0x68/0x69, the class robots). Found at boot, no flag to set; heading
  hold is then on: the motion engine tracks the gyro heading, every move ends
  with a settle step, and `cr` prints a ready-to-paste `SPIN_EFFICIENCY`. The
  MPU-6050's heading integrates its gyro after measuring the offset for 1 s at
  power-on (keep the robot still). `i` reports status, `in` / `if` toggle it.
- I2C is driven straight on the TWI registers (no Wire library: about 1.9 KB
  of flash and 200 bytes of RAM saved), polled, with a timeout on every wait.
- Optional VL53L0X time-of-flight laser (0x29) on the same I2C lines, found at
  boot (`USE_TOF`). `rs` one reading, `rw` watch until `x`, both `R,<mm>`
  (-1 = nothing in range, it sees 30-1200 mm). Forward manual driving and line
  following stop `TOF_STOP_MM` (120) from a wall with `tof: wall at N mm`. A
  BNO055 must then stay at 0x28.
- Optional IR remote (`USE_IR_REMOTE`): a 38 kHz receiver (VS1838B / HX1838)
  on D2, NEC decoded on the INT0 interrupt, timed on Timer1 (free-running,
  4 us ticks; the servo headers lose hardware PWM, which nothing uses; Timer2
  keeps the M1/M2 PWM, which is why the IRremote library is not used). The
  kit's 17-key and 21-key remotes: arrows hold-to-drive (release stops after
  250 ms), 1 / 3 spin, OK stops, 4 / 6 speed down / up, 7 8 9 0 run pd pc pk
  pj, 2 turns around (pu), * / # follow the line / seek the light. Any key
  aborts a demo. Keys not
  in `IR_KEYS[]` print `ir: key 0x.. not mapped`. A 74HC4067 mux (which wants
  D2 for S3) and the IR receiver clash; use the 8-channel mux.
- Optional ADS1115 boards (0x48-0x4B) on the same I2C lines, found at boot.
  Sources `i<chip><input>` (e.g. `i12` = 0x49 AIN2), scaled to 0..1023. The
  ADC layout defaults to line D1-D4 on 0x48, D5-D8 on 0x49, LDRs on 0x4A
  AIN0/AIN1. When nothing answers on I2C the sketch releases A4/A5.
- Optional HC-SR04 sonar (`USE_SONAR`), the kit's own, on A2 echo / A3 trig
  as shipped: `ru` one reading, `U,<mm>` (-1 = no echo), and `rw` shows it
  with the laser. It only drives A2/A3 while no line or light sensor (or the
  mux common) is wired to them in the running layout; `K,sonar` says which.
- `v<f>,<r>,<s>;` drives forward / right / clockwise at once (-100..100 % of
  full speed, no reply, `VECTOR_TIMEOUT_MS` 500 dead-man). With the laser
  seeing a wall the forward part is dropped and the rest still runs.
- Motor PWM is written straight to the timer compare registers (OC2A/OC2B/
  OC0A/OC0B for M1-M4) instead of `analogWrite()`; same timer modes.
- Flash: the Uno has 32 KB and the default build uses about 31.7 KB. Every
  optional part is a `#define` at the top of the sketch; approximate costs:
  IMU 1.9 KB, ToF 1.3 KB, IR 0.8 KB, extra demos 0.65 KB, mux 0.34 KB,
  sonar 0.3 KB, ADS1115 0.35 KB. The mux is off by default since v0.10.8
  (ADS1115 boards are the way to more inputs); extra demos fit with
  `USE_SONAR 0`.
  The demos are a table of steps (`DEMO_STEPS`, `LINE` / `ORBIT` / `FACE` /
  `SPIN` / `PAUSE` / `POLYGON` / `REPEAT`...`NEXT`), not code.
- Analog budget: the Uno has six analog inputs. Bluetooth has A0/A1 as
  shipped, and the IMU (if compiled in) takes A4/A5, so without a mux A2-A5
  are free. Every analog sensor is an `AnalogSource`: a direct pin or a
  channel of a 74HC4051 multiplexer (common on A2, selects D9/D10/D13; set
  `MUX_CHANNELS 16` for a 74HC4067 with S3 on D2), or an ADS1115 input. The
  mux selects are only driven while the running layout uses a mux channel.
- Line follower: 8-channel IR line sensor array (analog outputs, 8 mm
  pitch), up to 8 channels. `ls` one-shot reading, `lc` calibration (the robot
  strafes across the line sampling min/max per channel, saved to EEPROM),
  `lf` follow until `x`. Following streams `L,seen,offset,v1..vN` at 5 Hz.
  Tape polarity is a setting (`lh`): whether the raw reading goes up or down
  over the tape.
- Light seeker: up to 8 LDR + resistor dividers, each with a bearing. The
  normalised readings weight unit vectors along those bearings and the sum
  is the direction of the light, so with three or more LDRs the holonomic
  drive slides straight toward it. `bs` reading, `bc` 5 s calibration (cover
  each, then light each; saved), `bf` seek until `x`. Streams
  `B,direction,brightest,v1..vN`.
- Both are always compiled in and set up at runtime (Sensors tab, or the
  `k` commands); a fresh flash has zero sensors of either kind. `k?;` dumps
  `K,<key>,<value>` lines (plus `K,taken`, `K,mux`, `K,lcal`, `K,bcal`,
  `K,adc`, `K,layout`, `K,tof`, `K,sonar`, `K,demos`, `K,end`); `k<key>=<value>;` sets one and echoes it
  (layout keys take an `e` for the ADC layout: `len`, `lep0`, `beb1`);
  `kw;` saves, `ku;` undoes, `kr;` restores the sketch defaults, `kp;`
  probes I2C again. Editing the defaults in the
  sketch and re-flashing makes the robot ignore the old saved settings (a
  hash of the defaults is stored with them). Calibration is reused only
  while the wired pins match.

The kit's Bluetooth module is read with SoftwareSerial, which cannot receive
while the robot is sending a reply. So the app never sends a command and its
follow-up in one burst when the first one answers: a demo is `z<n>`, wait for
`demo size`, then `p<key>`, wait for `demo pX: start` (one retry each; the
card only shows RUNNING after the robot confirms), and sensor settings go one
at a time. The firmware also waits for a burst to finish arriving (3 ms of
quiet) before handling it. `node scripts/mecanum-sim/sensors-e2e.js bt`
runs the whole test through the simulated Bluetooth pin.

While a demo or calibration run is in progress the firmware blocks and discards
every byte except `x`, so the app greys out everything that would be thrown
away and shows a BUSY chip until the robot reports `demo: done`.

## Sensor end-to-end test

`node scripts/mecanum-sim/sensors-e2e.js` builds the sketch with arduino-cli,
runs it in the simavr AVR simulator (`uno-sim.c`, needs `libsimavr-dev` and
`libelf-dev`; it also simulates ADS1115, MPU-6050, BNO055 and VL53L0X boards
on I2C, an NEC IR remote on D2 and an HC-SR04 on A2/A3), and drives the Sensors tab in headless
Chrome through a fake `link` wired to the simulated UART: presets, sliders,
save, power cycle, clashes, undo, a line reading with simulated ADC voltages,
calibration, offline edits, the defaults block, the laser range and wall
stop, IR remote keys, and the greyed-out demos. The simulator also runs
plain scripts (`send`, `wait`, `adc`, `ir <hex> [ms]`, `dev tof`, `tof <mm>`,
`sonar <mm>`, `reboot`) for firmware-only checks. simavr 1.6 (Ubuntu's) never visibly
clears TWINT and posts TWI status one byte-time late, which is why the
sketch waits 100 us before polling and accepts 0x28 after SLA+W.

## Smoke test

Serve the repo root (`python3 -m http.server 8191 --bind 0.0.0.0`)
and load `/mecanum/` in headless Puppeteer; it must render 21 demo cards (16 greyed out once a default-build robot connects) with
no JS errors. The script used during development lives outside the repo; the
checks are: 21 `.demo-card` elements each with a `polyline.path`, pad presses
and calibration inputs never throw, generated config contains all three
constants.

## LAFVIN source files

`lafvin/` holds the kit's own sketch, Android app and manual. It is gitignored
so the vendor APK and PDF are never published with the site.
