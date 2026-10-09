# robotics.makerlabkids.com

The Maker Lab Kids robot workshop, on its own site: the mecanum robot's
Workshop app, Blocks editor, Remote page and printable guide, the calibration
and rename pages, the two-stick controller, the lesson pages, the 2WD robot's
app and lessons, the maze simulator, and the robots' firmware (ir.ino,
mecanum_holonomic.ino, calibration.ino, rename.ino).

It is a static site with no build step. The source lives in this GitHub
repository (acklenx/robotics); Cloudflare Pages pulls every push to main and
serves it at https://robotics.makerlabkids.com (build command: none; output
directory: /). `_headers` carries the Content-Security-Policy, which has no
unsafe-eval: the Blocks page runs its programs in an interpreter for that reason.

The pages that need a robot (Web Serial, Web Bluetooth) only work over https
or on localhost. `npm run serve` serves the site on port 8191.

## Tests

```
npm install
node scripts/check-mecanum-shared.js   # the three sketches share one motion engine
node scripts/check-mecanum-blocks.js   # the Blocks page under the real CSP (headless Chrome)
npm run test:mecanum                   # every simulator suite (needs arduino-cli + simavr)
npm run test:site                      # every page parses, every internal link resolves
```

Enable the 20 MB guard once per clone: `git config core.hooksPath .githooks`.

This repository is staged from the main site's `abot/` folder by
`scripts/stage-robots-site.js` in the makerlabkids repository until the move
is final; after that it is edited here directly.

---


Everything for the robotics club: the phone/desktop app that talks to the
robot, fifteen lesson pages, the maze simulator, and the robot's firmware.

## Putting it on your site

Copy this whole folder up as-is. Nothing needs building, installing, or
compiling — it is plain HTML, CSS, and JavaScript with no dependencies.

```
index.html            the hub kids land on: the 4WD mecanum robot (hand-written)
2wd.html              the 2WD robot's hub (built by build-site.py)
mecanum/              the mecanum Workshop app, two-stick controller, lessons
workshop.html         the 2WD app that talks to the robot
MazeLab.html          maze simulator, six built-in lessons, no robot needed
lessons/              fifteen club sessions, one page each
cheatsheet.html       every command on one printable page
css/                  two stylesheets
js/                   the app, split into readable pieces
vendor/               Blockly, self-hosted (bundle + its media)
kernel/               robot_kernel.ino — the firmware
build-site.py         regenerates the lesson pages from data
test-workshop.js      headless test of the app and block editor
test-site.js          headless test of every page and link
```

**Web Bluetooth needs a secure context.** `workshop.html` must be served over
`https://` (or opened at `http://localhost`) or the browser will refuse to
talk to the robot. The lesson pages and the Maze Lab work from anywhere,
including a USB stick.

On Android Chrome, both Bluetooth **and** Location must be switched on.

## Blockly, served from your own site

`vendor/blockly.min.js` is Blockly 13.2.1, copied into this folder. The Blocks
tab loads it from there, never from a CDN — which is the whole fix for the
"Blockly didn't load (offline?)" message you used to get on every page load.
`vendor/blockly-media/` holds its icons, cursors and click sounds, and
`js/blockly-editor.js` points Blockly at that folder explicitly; otherwise
Blockly fetches its own media and the CDN problem comes back wearing a hat.

To update Blockly later:

```
npm install blockly@latest
cp node_modules/blockly/blockly.min.js  vendor/blockly.min.js
cp -r node_modules/blockly/media/*      vendor/blockly-media/
node test-workshop.js            # confirms blocks still generate the same language
```

### The second editor

There is also a **Tap blocks** view (`js/blocks.js`), written here, with no
dependencies. It exists because drag-and-drop is miserable on a phone in a
noisy room: you tap a palette block to place it, tap a block to aim, use the
arrows to reorder, and −/+ to change numbers. Containers have a dotted
"inside" area you tap to aim into a loop.

Both editors write the same robot language into the same text box, so a
program built on a laptop opens on a phone and vice versa. Desktop-width
screens start in Blockly; phones start in the tap editor; the switch at the
top of the Blocks tab changes it any time. If `vendor/blockly.min.js` is ever
missing, the tap editor takes over and says so plainly.

## How the code is arranged

The app used to be one 2,400-line file. It is now nine pieces, each with one
job, loaded in order by `workshop.html`:

| File | What it owns |
|---|---|
| `js/app-state.js` | Shared constants and the handles to on-screen elements |
| `js/console-log.js` | The scrolling console, and touch feedback on every button |
| `js/link.js` | Bluetooth: connecting, sending, receiving |
| `js/telemetry.js` | Turning the robot's numbers into bars and sentences |
| `js/battery.js` | Rail voltage, pack voltage, the cutoff banner |
| `js/runner.js` | Sending programs and waiting for the robot to acknowledge |
| `js/program.js` | The program model: blocks in, robot language out, and back |
| `js/blocks.js` | The tap block editor's buttons and appearance |
| `js/blockly-editor.js` | Blockly: our robot blocks, and both directions of translation |
| `js/drive.js` | Joystick, D-pad, mode buttons, sliders, tilt |
| `js/calibrate.js` | Every calibration measurement |
| `js/app.js` | Tabs and start-up |

Two rules keep it readable for a curious eleven-year-old:

1. **Names say what they mean.** `flushBatteryWarningIfSafe` beats a comment
   explaining what `fbw()` does.
2. **One file, one job.** If you want to know how the robot's battery warning
   works, there is exactly one file to open.

## Editing the lessons

Do not edit the HTML in `lessons/`. Those files are generated. Edit the
`LESSONS` list at the top of `build-site.py` — each lesson is a block of plain
data (goal, ideas, steps, sample program, what-goes-wrong table, teacher
notes) — then run:

```
python3 build-site.py
```

That rewrites all fifteen lesson pages, the lesson index, and the 2WD hub (2wd.html). Adding a
sixteenth lesson means adding one entry to that list.

## Calibration measures speed, not launches

Step 1 asks for **two** runs — one second and three seconds — because the robot
spends its first second speeding up. Both runs contain exactly the same
acceleration, so subtracting them cancels it and leaves pure cruising distance
over a known time. A single short run would measure the launch and call it the
speed, and every maze program built on it would come up short.

The tests cover this: a robot cruising at 40 cm/s that loses 12 cm to its ramp
travels 28 cm in one second and 108 cm in three, and the maths has to recover
40 cm/s from those two numbers rather than the 28 cm/s the naive method gives.

## Printing

`css/site.css` has a print stylesheet. Lesson pages and the cheat sheet print
as ink on white with navigation removed, cards kept whole across page breaks,
and the teacher notes expanded — they are collapsed on screen and printed in
full, since the person holding the paper is the one who wants them.

## Testing

Both test files run headlessly, no browser and no robot required:

```
npm install jsdom      # once
node test-workshop.js  # boots the app, drives the block editor, checks output
node test-site.js      # every page parses, every link resolves, samples valid
```

`test-workshop.js` builds programs by simulating taps and then checks the exact
robot language produced, including calibration-aware blocks and the
text → blocks → text round trip. `test-site.js` also parses every sample
program on every lesson page through the real parser, so a typo in a lesson
fails the build rather than confusing a kid.

## The robot end

`kernel/robot_kernel.ino` is the firmware. Before flashing a robot, change the
name at the top:

```cpp
#define ROBOT_BLUETOOTH_NAME "Aqua Kitten"
```

That single line is usually a kid's first ever code edit, and their robot
announces itself by name in the Bluetooth picker forever after.

Battery monitoring constants live just below it. `BATTERY_SENSE_PIN` is `A5`
when the divider is wired, `-1` when it is not.
