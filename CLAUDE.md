# CLAUDE.md

This repository is robotics.makerlabkids.com: the robot workshop pages and firmware
that used to live under /abot/ on makerlabkids.com. The site root here is what
/abot/ was there, so `mecanum/ir.html` is https://robotics.makerlabkids.com/mecanum/ir.html.
Links to the main site are absolute (https://makerlabkids.com/...).

Static site, no build. Cloudflare Pages deploys main from the GitHub repository
acklenx/robotics to https://robotics.makerlabkids.com. `_headers` sets a
Content-Security-Policy without unsafe-eval: never add eval or new Function to a page.

## Mecanum calibration sketch

`calibration/calibration.ino` is a small sketch for calibrating BY HAND,
one number at a time: `o<mode>,<power>,<ms>;` measuring runs (all wheels at
one power, no curve/trims/ramp) and `m...` moves exactly as the robot drives
them. Each measured number goes into the CONFIG constants of BOTH
`mecanum_holonomic.ino` and `calibration.ino`; flash, rerun the test, then the
next number. No EEPROM numbers: `mecanum_holonomic.ino` has
`CALIBRATION_IN_EEPROM 0`. The code both use (motor shield, speed curve, wheel
math, move timing, the constants) must stay identical:

```bash
node scripts/check-mecanum-shared.js   # fails if the shared functions or constants differ
```

**Firmware and app versions.** The middle number is the protocol line: every
0.10.x sketch speaks the same language to the app. A sketch change that breaks
the app <-> robot protocol starts a new line (0.11.0) - ask the user first.
Before changing the app for it, freeze the outgoing app so robots still on
the old line get the app that matches them:

```bash
node scripts/snapshot-mecanum-app.js          # copies the app to mecanum/v/<version>/, lists it in versions.json
# ...change the app, raise FIRMWARE_COMPAT to the new line...
node scripts/snapshot-mecanum-app.js --list   # refresh the current app's entry and latestFirmware
```

At connect the app compares the robot's firmware with its FIRMWARE_COMPAT
line and, if it does not fit, offers the matching app from
`mecanum/versions.json`. Run `--list` after any firmware version bump.

`rename/rename.ino` + `mecanum/rename.html` name a robot: the HC-06
module gets the robot's name (AT+NAME, no line end). Over USB it renames on
the spot; over Bluetooth it saves the name (EEPROM 1040) and renames at the
next power-on. Flash rename.ino, rename, flash mecanum_holonomic.ino back.

`mecanum/calibration.html` is the step-by-step page for calibration.ino
(written for a second grader): one step open at a time (connect, name + floor,
smallest push, how fast, drive straight, distance check), no sensors needed
(the gyro is an optional extra), each step shows its numbers and whether the
robot is using them, with Undo (one level) and Reset (fleet defaults: keep its
FLEET_DEFAULTS in step with the sketch's MOTION_DEFAULTS; calpage-e2e checks).

The two programs share the motion calibration through EEPROM (uploads keep
it). Simulator suites: `npm run test:mecanum` (sensors, program, sticks; USB and Bluetooth, 4 at
a time; `MECANUM_JOBS=n` changes that; failures rerun alone once and a pass on
the rerun is reported as flaky).

## Remote-only robot (ir.ino)

`ir/ir.ino` is the standalone sketch for running the mecanum robot from the kit's
17-key IR remote with no phone: hold arrows / 1 3 to drive and turn, `* * *` enters
program mode (wiggle), `* <direction> <digits>` per step (nod; 7 9 diagonals, 5 pause s,
8 do-it-all-again times), `* * *` ends, slot digit + `* *` saves, `* <digit>` runs, `* * * * <digit>`
adds to a saved slot, `#` cancels, OK after `* * *` tries the draft. Slot text letters: f b l r q e t p n. `# # #` is a
3-step calibration from the remote (start power by pulses, 1 m distance + drift, four 90s;
saved to the shared EEPROM motion set, no strafe). HC-SR04
(A2/A3 as shipped) stops forward motion under 250 mm. Over USB/Bluetooth it takes m moves,
v keep-driving, u sonar, p slot lines, i remote keys and the k motion commands, so the Blocks page
and Sticks drive it (no IMU, line or light). Receiver on any pin
(`IR_RECEIVER_PIN`, default D10 = the shield's SER1 servo header): the decoder samples the
pin from a Timer1 tick because SoftwareSerial owns the pin-change interrupts. Bluetooth
output is buffered and sent only while the IR line is quiet (SoftwareSerial TX blocks
interrupts and would lose keys). It shares the motor/motion code with the other two
sketches: `node scripts/check-mecanum-shared.js` compares all three. Test without
hardware: `node scripts/mecanum-sim/ir-e2e.js` (script-mode simavr, in run-all). Page:
`mecanum/ir.html` (kid instructions + instructor console, version in its title).
Simulator note: simavr forces a pulled-up INPUT high on every PORT write, so the IR pin is
a plain INPUT (the receiver drives its line).

## Writing Style

- **Never use emdashes anywhere** - not in HTML content, commit messages, comments, or any text. Use a regular dash (-) if needed. Emdashes are an obvious AI tell. Applies to `&mdash;`, `—`, `&#8212;`, and any other form.
- **Never use left/right (smart/curly) quotes** - use straight quotes (`"` and `'`) instead. They render reliably everywhere. Applies to `&ldquo;`, `&rdquo;`, `&lsquo;`, `&rsquo;`, `"`, `"`, `'`, `'`, `&#8220;`-`&#8221;`, `&#8216;`-`&#8217;`, and any other form.

## Working Style

- Push directly to main when asked - no need to confirm. Commit + push in one step.
- Don't ask clarifying questions when the intent is clear - just do it and let the user correct.
- Apply changes across all affected files (all 6 launcher pages share patterns).
- Large multi-feature commits are fine for this project.
- No co-author bylines in commits.

## Bash Style: Be Boring

The sandbox auto-allows simple single commands and prompts for compound shell programs. The user also reads tool calls when reviewing or after an interrupt. So default to boring:

- Avoid `while`/`for`/`if` loops in Bash tool calls. They prompt regardless of what the body does.
- Avoid command substitution `$(cmd)` and process substitution `<(cmd)` / `>(cmd)`. They prompt and are fragile on edge cases.
- Prefer single-flag solutions: `grep -L` over a loop that filters out grep matches; `grep -rl` over chained finds.
- For cheap reads, **run the command twice** rather than write a temp file. `grep` over the repo is milliseconds; a `>` redirection is a write that prompts. Re-running is faster than the temp-file dance.
- When a temp file is genuinely needed (reused 3+ times, or combining two outputs via `comm`/`diff`), put it in `temp/` (in-project, gitignored). **Not** `tmp/` (deprecated) and **not** `/tmp/` (outside project = harder sandbox prompt).
- If the task genuinely needs iteration or conditionals, write a node or python script to `temp/*.js` and run that. Don't try to inline logic into shell.
- Boring is also auditable: a sequence of plain `grep`/`comm`/`diff` calls is instantly legible; a chained pipeline with `<()` and `$()` requires mentally parsing shell precedence.

## Git Conventions

- No co-author bylines in commits
- No file over 20 MB in git: `git config core.hooksPath .githooks` once per clone
