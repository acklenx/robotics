# Writes mecanum/square.html: a one-page, step by step guide to programming a square
# from the IR remote, with a picture of what each step makes the robot do.
# Run: python3 scripts/square-figs.py && node scripts/site-nav.js
import math
import os

from robot_svg import bot_top

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
ORANGE, GRAY = "#e65100", "#9aa1a9"
CORNERS = {"A": (58, 128), "B": (58, 46), "C": (142, 46), "D": (142, 128)}   # robot centers; A is the start
SCALE = 0.23


def robot(at, heading, uid, ghost=False):
    x, y = CORNERS[at]
    svg = bot_top(x, y, heading, SCALE, ldrs=False, uid=uid)
    return f'<g opacity="0.28">{svg}</g>' if ghost else svg


def line(a, b, color, width, dash=None, arrow=False, shorten=0):
    (x1, y1), (x2, y2) = CORNERS[a], CORNERS[b]
    length = math.hypot(x2 - x1, y2 - y1)
    ux, uy = (x2 - x1) / length, (y2 - y1) / length
    x2, y2 = x2 - ux * shorten, y2 - uy * shorten
    d = f' stroke-dasharray="{dash}"' if dash else ""
    out = f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{color}" stroke-width="{width}" stroke-linecap="round"{d}/>'
    if arrow:
        out += arrowhead(x2, y2, ux, uy, color)
    return out


def arrowhead(x, y, ux, uy, color, size=8):
    nx, ny = -uy, ux
    pts = [(x + ux * size, y + uy * size), (x - ux * 2 + nx * size * 0.75, y - uy * 2 + ny * size * 0.75),
           (x - ux * 2 - nx * size * 0.75, y - uy * 2 - ny * size * 0.75)]
    return '<polygon points="' + " ".join(f"{px:.1f},{py:.1f}" for px, py in pts) + f'" fill="{color}"/>'


def turn_arrow(at, heading):
    """A clockwise arrow around the robot: a right turn of 90 degrees from heading."""
    cx, cy = CORNERS[at]
    r = 34
    t1, t2 = math.radians(heading - 35), math.radians(heading + 55)
    x1, y1 = cx + r * math.sin(t1), cy - r * math.cos(t1)
    x2, y2 = cx + r * math.sin(t2), cy - r * math.cos(t2)
    out = f'<path d="M{x1:.1f} {y1:.1f} A{r} {r} 0 0 1 {x2:.1f} {y2:.1f}" fill="none" stroke="{ORANGE}" stroke-width="3" stroke-linecap="round"/>'
    out += arrowhead(x2, y2, math.cos(t2), math.sin(t2), ORANGE)
    lx, ly = cx + 46 * math.sin(math.radians(heading + 135)), cy - 46 * math.cos(math.radians(heading + 135))   # inside the square
    out += text(lx, ly + 4, "90°", ORANGE, 13, "middle", bold=True)
    return out


def text(x, y, words, color="#1c1f24", size=11, anchor="middle", bold=False):
    w = ' font-weight="700"' if bold else ""
    return f'<text x="{x:.1f}" y="{y:.1f}" text-anchor="{anchor}" font-size="{size}" fill="{color}"{w}>{words}</text>'


def gesture(at, kind):
    """Wiggle (side to side) or nod (forward and back) marks around the robot."""
    cx, cy = CORNERS[at]
    out = ""
    if kind == "wiggle":
        for side in (-1, 1):
            x = cx + side * 30
            out += f'<path d="M{x} {cy - 12} Q{x + side * 7} {cy} {x} {cy + 12}" fill="none" stroke="{ORANGE}" stroke-width="2.5" stroke-linecap="round"/>'
            out += f'<path d="M{x + side * 6} {cy - 8} Q{x + side * 12} {cy} {x + side * 6} {cy + 8}" fill="none" stroke="{ORANGE}" stroke-width="2" stroke-linecap="round"/>'
    else:
        for end in (-1, 1):
            y = cy + end * 34
            out += f'<path d="M{cx - 9} {y} Q{cx} {y + end * 6} {cx + 9} {y}" fill="none" stroke="{ORANGE}" stroke-width="2.5" stroke-linecap="round"/>'
    return out


SIDES = [("A", "B"), ("B", "C"), ("C", "D"), ("D", "A")]
HEADINGS = {"A": 0, "B": 90, "C": 180, "D": 270}       # the way it faces after turning at each corner


def picture(kind, n, label):
    parts = [f'<rect width="200" height="180" rx="8" class="pic-bg" fill="#f4f1ea"/>',
             '<rect x="58" y="46" width="84" height="82" fill="none" stroke="#e2ddd2" stroke-width="1.5"/>']
    uid = f"sq{kind}{n}"
    if kind == "start":
        parts.append(robot("A", 0, uid))
        parts.append(gesture("A", "wiggle"))
        parts.append(text(100, 170, "wiggle = listening", ORANGE, 12))
    elif kind == "forward":          # n = 0..3: which side
        a, b = SIDES[n]
        for s in SIDES[:n]:
            parts.append(line(*s, GRAY, 3, "5 5"))
        start_heading = 0 if n == 0 else HEADINGS[a] if a != "A" else 0
        heading = [0, 90, 180, 270][n]
        parts.append(robot(a, heading, uid + "g", ghost=True))
        parts.append(line(a, b, ORANGE, 4, arrow=True, shorten=34))
        parts.append(robot(b, heading, uid))
        mx, my = [(84, 92), (100, 76), (116, 92), (100, 112)][n]
        parts.append(text(mx, my, "30 cm", ORANGE, 12, bold=True))
    elif kind == "turn":             # n = 0..3: at which corner
        at = ["B", "C", "D", "A"][n]
        for s in SIDES[:n + 1]:
            parts.append(line(*s, GRAY, 3, "5 5"))
        heading = [0, 90, 180, 270][n]
        parts.append(robot(at, heading + 90, uid))
        parts.append(turn_arrow(at, heading))
    elif kind == "finish":
        for s in SIDES:
            parts.append(line(*s, GRAY, 3, "5 5"))
        parts.append(robot("A", 0, uid))
        parts.append(gesture("A", "wiggle"))
        parts.append(text(100, 170, "wiggle = which slot?", ORANGE, 12))
    elif kind == "save":
        parts = [f'<rect width="200" height="180" rx="8" class="pic-bg" fill="#f4f1ea"/>']
        parts.append('<rect x="56" y="26" width="88" height="104" rx="10" fill="#fff" stroke="#5b6670" stroke-width="2"/>')
        parts.append(text(100, 52, "slot 1", "#1c1f24", 16, bold=True))
        parts.append('<rect x="78" y="64" width="44" height="44" fill="none" stroke="#e65100" stroke-width="3"/>')
        for x, y, ux, uy in ((78, 86, 0, -1), (100, 64, 1, 0), (122, 86, 0, 1), (100, 108, -1, 0)):
            parts.append(arrowhead(x, y, ux, uy, ORANGE, 6))
        parts.append(text(100, 160, "nod = saved", ORANGE))
    elif kind == "run":
        for a, b in SIDES:
            parts.append(line(a, b, ORANGE, 4, arrow=True, shorten=34))
        parts.append(robot("A", 0, uid))
        parts.append(text(100, 100, "", ORANGE))
    return f'<svg viewBox="0 0 200 180" role="img" aria-label="{label}">' + "".join(parts) + "</svg>"


def keys(*names):
    cls = {"*": "star", "#": "pound", "up": "arrow", "3t": "turn"}
    shown = {"up": "&#9650;", "3t": "3"}
    return "".join(f'<span class="key {cls.get(k, "")}">{shown.get(k, k)}</span>' for k in names)


FWD = ("*", "up", "3", "0")
TURN = ("*", "3t")
STEPS = [
    ("Start a program", ("*", "*", "*"), ("start", 0), "The robot <b>wiggles</b>: it is listening.",
     "The robot wiggles from side to side: it is listening."),
    ("Forward 30 cm", FWD, ("forward", 0), "It <b>nods</b>: step saved.", "The robot will drive forward 30 cm."),
    ("Turn right", TURN, ("turn", 0), "It <b>nods</b>. 3 turns right, 90&deg;.", "The robot will turn right 90 degrees."),
    ("Forward 30 cm", FWD, ("forward", 1), "It <b>nods</b>.", "The robot will drive forward 30 cm, along the top of the square."),
    ("Turn right", TURN, ("turn", 1), "It <b>nods</b>.", "The robot will turn right 90 degrees."),
    ("Forward 30 cm", FWD, ("forward", 2), "It <b>nods</b>.", "The robot will drive forward 30 cm, down the far side."),
    ("Turn right", TURN, ("turn", 2), "It <b>nods</b>.", "The robot will turn right 90 degrees."),
    ("Forward 30 cm", FWD, ("forward", 3), "It <b>nods</b>.", "The robot will drive forward 30 cm, back to the start."),
    ("Turn right", TURN, ("turn", 3), "It <b>nods</b>. Facing the start way again.", "The robot will turn right 90 degrees and face the way it started."),
    ("Finish", ("*", "*", "*"), ("finish", 0), "It <b>wiggles</b>: which slot?", "The program is done: the robot wiggles to ask which slot to save it in."),
    ("Save in slot 1", ("1", "*", "*"), ("save", 0), "It <b>nods</b>: saved.", "The program is saved in slot 1."),
    ("Run it!", ("*", "1"), ("run", 0), "It drives the square.", "The robot drives all the way around the square and ends where it started."),
]

cards = []
for i, (title, ks, (kind, n), say, label) in enumerate(STEPS, 1):
    cards.append(f'''    <div class="step">
      <div class="step-head"><span class="num">{i}</span>{title}</div>
      <div class="keys">{keys(*ks)}</div>
      {picture(kind, n, f"Step {i}: {label}")}
      <p>{say}</p>
    </div>''')

page = f'''<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Program a Square 0.1.0 - Maker Lab Kids</title>
<meta name="description" content="Program the mecanum robot to drive a square with the IR remote, one key press at a time, with a picture of what each step does.">
<!-- written by scripts/square-figs.py: edit that, then run it and node scripts/site-nav.js -->
<style>
  :root {{ --ink: #1c1f24; --dim: #5b6670; --orange: #c24400; --blue: #0b6fb3; --red: #c62828; --purple: #5b47c9; --mono: ui-monospace, "Cascadia Mono", Menlo, Consolas, monospace; }}
  * {{ box-sizing: border-box; }}
  html, body {{ margin: 0; background: #e9ecef; }}
  body {{ color: var(--ink); font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; font-size: 14px; line-height: 1.35; }}
  .sheet {{ background: #fff; max-width: 8.5in; margin: 16px auto; padding: 0.4in 0.45in; box-shadow: 0 2px 14px rgba(0,0,0,0.15); }}
  .top {{ display: flex; align-items: center; gap: 14px; margin-bottom: 6px; }}
  .top img {{ height: 64px; width: auto; }}
  .top h1 {{ font-size: 24px; margin: 0; line-height: 1.1; }}
  .top .sub {{ color: var(--dim); font-size: 13px; }}
  .note {{ background: #fff4e5; border-left: 4px solid var(--orange); padding: 6px 10px; margin: 8px 0 12px; font-size: 13px; }}
  .steps {{ display: grid; grid-template-columns: repeat(4, 1fr); grid-auto-rows: 1fr; gap: 10px; }}
  .step {{ border: 1.5px solid #c9ced4; border-radius: 10px; padding: 7px 8px; display: flex; flex-direction: column; }}
  .step-head {{ font-weight: 700; font-size: 14px; display: flex; align-items: center; gap: 6px; }}
  .num {{ display: inline-flex; align-items: center; justify-content: center; width: 22px; height: 22px; border-radius: 50%; background: var(--orange); color: #fff; font-size: 13px; flex: none; }}
  .keys {{ margin: 5px 0 4px; }}
  .key {{ display: inline-flex; align-items: center; justify-content: center; min-width: 1.75em; height: 1.75em; padding: 0 0.35em; margin: 1px;
          border-radius: 6px; background: #fff; border: 1.5px solid #9aa4ae; color: var(--ink); font-family: var(--mono); font-weight: 700; font-size: 14px; }}
  .key.star {{ border-color: var(--orange); color: var(--orange); }}
  .key.arrow {{ border-color: var(--blue); color: var(--blue); }}
  .key.turn {{ border-color: var(--purple); color: var(--purple); }}
  .step svg {{ width: 100%; height: auto; display: block; }}
  .step p {{ margin: 5px 0 0; font-size: 13px; }}
  .tips {{ display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; margin-top: 12px; font-size: 12.5px; }}
  .sheet a {{ color: var(--blue); }}
  .tips div {{ background: #f4f6f8; border-radius: 8px; padding: 6px 9px; }}
  @media (max-width: 760px) {{ .steps {{ grid-template-columns: repeat(2, 1fr); }} .tips {{ grid-template-columns: 1fr; }} .sheet {{ padding: 16px; }} }}
  @media print {{
    @page {{ size: letter; margin: 0.3in 0.3in 0.45in;
      @bottom-center {{ content: "Maker Lab Kids \\00B7  robotics.makerlabkids.com"; font: 10px system-ui, sans-serif; color: #555; }} }}
    .sheet a[href^="/"]::after {{ content: " (robotics.makerlabkids.com" attr(href) ")"; }}
    .sheet a {{ color: inherit; }}
    .step svg {{ max-height: 1.32in; width: auto; margin: 0 auto; }}
    .top img {{ height: 52px; }}
    .note {{ margin: 4px 0 8px; }}
    html, body {{ background: #fff; }}
    .sheet {{ box-shadow: none; margin: 0; padding: 0; max-width: none; }}
    .pic-bg {{ fill: #fff; }}
    .note {{ background: none; }}
    .tips div {{ background: none; border: 1px solid #c9ced4; }}
  }}
</style>
</head>
<body>
<div class="sheet">
  <div class="top">
    <img src="../img/lockup.png" alt="Maker Lab Kids" width="300" height="99">
    <div><h1>Program a square</h1><div class="sub">With the IR remote, one key at a time. ir.ino on the robot.</div></div>
  </div>
  <div class="note">While you program, the robot <b>does not move</b>. It only wiggles or nods to say it got your keys.
  The pictures show what it will do when you <b>run</b> the program in step 12.</div>
  <div class="steps">
{chr(10).join(cards)}
  </div>
  <div class="tips">
    <div><b>Point and go slow.</b> Aim the remote at the robot's black eye. Press one key at a time and wait for the wiggle or nod.</div>
    <div><b>Made a mistake?</b> Press <span class="key" style="border-color:var(--red);color:var(--red)">#</span> to cancel, then start again at step 1.</div>
    <div><b>Not quite a square?</b> Every robot is a little different. Calibrate it: see the <a href="/mecanum/remote-guide#calibrate">Remote guide</a>.</div>
  </div>
</div>
</body>
</html>
'''
open(os.path.join(ROOT, "mecanum", "square.html"), "w").write(page)
print("wrote mecanum/square.html")
