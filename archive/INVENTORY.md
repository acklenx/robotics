# Everything, in one list

Two folders. `mlk-site/` is the thing you host; `flyers/` is the thing you
print. Nothing depends on the internet once it is on your server.

---

## Start here

| Do this | Open |
|---|---|
| See the flyers side by side and pick one | `flyers/index.html` |
| See the site as a kid sees it | `mlk-site/index.html` |
| Set up a robot from scratch | `mlk-site/setup.html` |
| Flash the robot | `mlk-site/kernel/robot_kernel.ino` (v0.1.13) |
| Print the command reference | `mlk-site/cheatsheet.html` |

**Hosting:** copy `mlk-site/` up as-is. No build step. `workshop.html` must be
served over `https://` (or `http://localhost`) because browsers only allow
Bluetooth from a secure page; everything else works from anywhere, including a
USB stick.

---

## `mlk-site/` — the club site (66 files)

### Pages
| File | What it is |
|---|---|
| `index.html` | Hub. Three tiles: Workshop, Lessons, Maze Lab. |
| `workshop.html` | The app that talks to the robot: drive, sensors, blocks, calibrate. |
| `setup.html` | Box to driving in five steps. Read this before club night. |
| `cheatsheet.html` | Every command on one printable page. |
| `MazeLab.html` | Maze simulator with six built-in lessons. No robot needed. |
| `lessons/index.html` | Lesson index. |
| `lessons/01-…` → `15-…` | Fifteen session pages. |

### The fifteen lessons
1. Robot or Not? — what makes a machine a robot
2. Driving & the Remote — commands, control, who is in charge
3. What the Robot Can Feel — reading real sensors
4. Every Robot Is Different — calibration, and differential measurement
5. Your First Program — blocks, and what the robot receives
6. Doing It Again — loops
7. Making Decisions — if, else, and faking else
8. Following the Line — a feedback loop you can watch
9. Avoiding & Seeking — strategies for not knowing everything
10. The Maze Race — everything at once, against the clock
11. Getting Off the Line — acceleration, slip, why fast starts are slow
12. When Sensors Lie — noise, blind spots, confident wrong answers
13. Two Robots, One Room — sharing space, control, airwaves
14. Design Your Own Behaviour — one sentence before any blocks
15. Show & Tell — explaining it to someone who was not there

### Code
| File | Owns |
|---|---|
| `js/app-state.js` | Shared constants and element handles. Loads first. |
| `js/console-log.js` | The console, and touch feedback on every button. |
| `js/link.js` | Bluetooth: connect, chunked send, line-buffered receive. |
| `js/telemetry.js` | Sensor displays, motor bars, plain-language analysis. |
| `js/battery.js` | Rail voltage, pack gauge, cutoff banner, safe-moment warnings. |
| `js/runner.js` | Sending programs, RUN acknowledgement, step echoes, read-back. |
| `js/program.js` | Program model: blocks in, robot language out, and back. |
| `js/blocks.js` | The tap block editor (phone default). |
| `js/blockly-editor.js` | Blockly: robot blocks, both directions of translation. |
| `js/drive.js` | Joystick, D-pad, mode buttons, sliders, tilt. |
| `js/calibrate.js` | Acceleration, distance, turns, drift trim, servo zero. |
| `js/app.js` | Tabs and start-up. Loads last. |
| `js/lesson-page.js` | Copy buttons on lesson pages. |
| `css/workshop.css` | The app. |
| `css/site.css` | Hub, lessons, cheat sheet, and the print styles. |
| `vendor/blockly.min.js` | Blockly 13.2.1, self-hosted. |
| `vendor/blockly-media/` | Its icons, cursors and sounds — also self-hosted. |
| `kernel/robot_kernel.ino` | The firmware, v0.1.13. |

### Tools
| File | What it does |
|---|---|
| `build-site.py` | Regenerates all lesson pages, the index, hub, cheat sheet and setup page from the `LESSONS` data at the top. Edit data, not HTML. |
| `test-workshop.js` | Boots the real app headlessly and drives both block editors. 33 checks. |
| `test-site.js` | Every page parses, every link resolves, nothing off-site, every sample program valid. 7 checks. |
| `README.md` | Hosting, module map, how to update Blockly, how to edit lessons. |

Run the tests with `npm install jsdom` once, then `node test-workshop.js` and
`node test-site.js`.

---

## `flyers/` — parent recruitment (8 files)

| File | Track | Treatment |
|---|---|---|
| `index.html` | — | Preview all six side by side. **Open this first.** |
| `junior-1-poster.html` | Grades 3–5 | Loud poster. Big type, three steps, ~160 words. |
| `junior-2-infosheet.html` | Grades 3–5 | Calm information sheet with a details table and parent FAQ. |
| `junior-3-journey.html` | Grades 3–5 | Warm narrative timeline, week by week. |
| `senior-1-buildbay.html` | Grades 6–12 | Dark, technical, high energy. Aimed at the teenager too. |
| `senior-2-syllabus.html` | Grades 6–12 | Course outline: weekly arc, skills, homeschool notes, your background. |
| `senior-3-editorial.html` | Grades 6–12 | Magazine editorial. One argument, made properly. |
| `build-flyers.py` | — | Regenerates all six from one `DETAILS` block. |

**Before printing:** fill in day/time, start date, location, price, contact,
sign-up link and class size — all in `DETAILS` at the top of
`build-flyers.py`, then run `python3 build-flyers.py`. Print at 100% with
background graphics enabled, or the coloured panels vanish.

All six were rendered to PDF and confirmed to be one page each.

---

## What is still open

- **Acceleration numbers.** The seed/growth sliders are at deliberately slow
  defaults (about 1.2 s to full speed) so you can watch a launch. Find the
  fastest clean setting and those become the firmware defaults.
- **Recalibrate after that.** Step 1 now takes two runs, one second and three,
  and subtracts them so the ramp cancels out.
- **The sonar.** On battery power press `d` and read the end of the line:
  `NOECHO` climbing means no echoes at all; a non-zero `SWAPTEST` means the
  trigger and echo wires are crossed.
- **Battery calibration.** Optional. Your gauge reads 0.65% low against the
  Fluke at the VIN pin; `BATTERY_CALIBRATION_PERMILLE 1007` would trim it, and
  leaving it at 1000 is fine.
