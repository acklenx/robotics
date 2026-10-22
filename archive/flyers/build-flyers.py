#!/usr/bin/env python3
"""Builds six one-page parent flyers for the after-school robotics program.

Three for the junior track (grades 3-5), three for the senior track (6-12),
each with a different look AND a different argument, so you can see which one
gets phones ringing. Edit DETAILS below, run this, print.

    python3 build-flyers.py
"""

import os

# ---------------------------------------------------------------------------
# EDIT THIS BLOCK. Everything the flyers say about when, where and how much
# comes from here, so you change it once and regenerate all six.
# ---------------------------------------------------------------------------

DETAILS = {
    "org": "Maker Lab Kids",
    # The brand mark where a flyer shows the logo: the lockup image (gear +
    # wordmark), never the name set in type. White tab for the dark flyers.
    "orgmark": ('<img src="../img/lockup.png" alt="Maker Lab Kids" '
                'style="height:30pt;width:auto;display:inline-block;vertical-align:middle;'
                'background:#fff;border-radius:5pt;padding:2pt 5pt">'),
    "tagline": "Make. Learn. Share.",
    "mission": "Cultivating curiosity, critical thinking, and creative confidence",
    "junior_grades": "Grades 3&ndash;5",
    "senior_grades": "Grades 6&ndash;12",
    "weeks": "15 weeks",
    "session": "one hour a week",
    "day_time": "[ DAY &amp; TIME ]",
    "start_date": "[ START DATE ]",
    "location": "[ LOCATION ]",
    "price": "[ PRICE ]",
    "contact": "[ EMAIL / PHONE ]",
    "signup": "[ SIGN-UP LINK ]",
    "class_size": "[ N ]",
}

CSS_SHARED = """
  /* One page, every time. The dashed outline on screen shows where the paper
     ends, so nothing sneaks onto a second sheet without you noticing. */
  @page { size: letter portrait; margin: 0.4in; }
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { display: flex; justify-content: center; padding: 24px 12px; }
  .sheet {
    width: 7.7in; min-height: 10.2in; padding: 0.34in 0.4in;
    position: relative;
    /* Deliberately NOT overflow:hidden. If your real dates and prices run
       longer than the placeholders, the flyer should visibly spill onto a
       second page so you notice, rather than quietly swallowing a line. */
    overflow: visible;
  }
  @media screen { .sheet { outline: 1px dashed #bbb; outline-offset: 10px; } }
  @media print {
    body { padding: 0; display: block; }
    .sheet { width: auto; height: auto; outline: none; }
  }
  .fill { white-space: nowrap; }
"""


def page(title, style, body):
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>%s</title>
<style>%s%s</style>
</head>
<body>
<div class="sheet">
%s
</div>
</body>
</html>
""" % (title, CSS_SHARED, style, body)


# ===========================================================================
# JUNIOR 1 — the loud one. A poster, not a letter. Big shapes, few words.
# ===========================================================================

def junior_poster():
    style = """
  body { background: #f4f1ea; font-family: "Trebuchet MS", "Segoe UI", system-ui, sans-serif; }
  .sheet { background: #ffffff; color: #14161a; }
  .band {
    position: absolute; top: 0; left: 0; right: 0; height: 3.5in;
    background: linear-gradient(115deg, #e65300 0%, #f26a00 58%, #ffb04d 100%);
  }
  .band:after {
    content: ""; position: absolute; bottom: -0.75in; left: 0; right: 0; height: 1.1in;
    background: #fff; transform: skewY(-4.2deg);
  }
  .top { position: relative; padding-top: 0.16in; color: #fff; }
  /* Dark brown, like "THAT THINK": white is too faint on the pale end of the band. */
  .kicker { font-size: 13pt; font-weight: 800; letter-spacing: 0.22em; text-transform: uppercase; color: #2a1400; }
  h1 { font-size: 58pt; line-height: 0.92; font-weight: 900; letter-spacing: -0.02em; margin: 10px 0 6px; }
  h1 em { font-style: normal; display: block; color: #2a1400; }
  .sub { font-size: 15pt; font-weight: 700; max-width: 5.4in; color: #2a1400; }
  .row { display: flex; gap: 0.22in; margin-top: 1.55in; }
  .step {
    flex: 1; background: #fff6ec; border: 3px solid #ffb56b; border-radius: 16px;
    padding: 13px 12px; text-align: center;
  }
  .step .n {
    display: inline-block; width: 30px; height: 30px; line-height: 30px; border-radius: 50%;
    background: #e05500; color: #fff; font-weight: 900; font-size: 14pt; margin-bottom: 7px;
  }
  .step h3 { font-size: 13.5pt; margin-bottom: 3px; }
  .step p { font-size: 10pt; line-height: 1.35; color: #4a4038; }
  .big {
    margin-top: 0.24in; background: #14161a; color: #fff; border-radius: 18px;
    padding: 17px 20px; display: flex; align-items: center; gap: 18px;
  }
  .big .huge { font-size: 34pt; font-weight: 900; color: #ff9d29; line-height: 1; }
  .big p { font-size: 11.5pt; line-height: 1.4; }
  .facts { display: flex; gap: 0.2in; margin-top: 0.24in; }
  .fact { flex: 1; border-top: 5px solid #14161a; padding-top: 8px; }
  .fact b { display: block; font-size: 9pt; letter-spacing: 0.14em; text-transform: uppercase; color: #6b6158; }
  .fact span { font-size: 13pt; font-weight: 800; }
  .footer {
    position: absolute; left: 0.4in; right: 0.4in; bottom: 0.34in;
    border-top: 3px solid #ffb56b; padding-top: 10px;
    display: flex; justify-content: space-between; align-items: flex-end;
  }
  .footer .org { font-size: 15pt; font-weight: 900; }
  .footer .org small { display: block; font-size: 9.5pt; font-weight: 700; color: #b84c00; letter-spacing: 0.14em; }
  .footer .cta { text-align: right; font-size: 11pt; font-weight: 800; }
  .footer .cta span { display: block; font-size: 9.5pt; font-weight: 600; color: #4a4038; }
"""
    body = """  <div class="band"></div>
  <div class="top">
    <div class="kicker">After school &middot; %(junior_grades)s</div>
    <h1>ROBOTS<em>THAT THINK</em></h1>
    <div class="sub">Start with a buzzing brushbot. Finish racing a robot that
    finds its own way through a maze.</div>
  </div>

  <div class="row">
    <div class="step"><span class="n">1</span><h3>Brushbots</h3>
      <p>A motor, a battery, a toothbrush head. It moves in week one and it
      goes home with you.</p></div>
    <div class="step"><span class="n">2</span><h3>Light chasers</h3>
      <p>Add eyes. Now it hunts a flashlight across the floor &mdash; and you
      can explain exactly why.</p></div>
    <div class="step"><span class="n">3</span><h3>Maze racers</h3>
      <p>Build the maze. Pick the robot's strategy. Find out whose idea
      actually wins.</p></div>
  </div>

  <div class="big">
    <div class="huge">SENSE<br>THINK<br>DO</div>
    <p>Every robot on earth runs the same loop, and by week three your kid can
    spot it in a dishwasher, a drone and a self&#8209;driving car. That is the
    real thing we are teaching. The robots are how we make it stick.</p>
  </div>

  <div class="facts">
    <div class="fact"><b>When</b><span class="fill">%(day_time)s</span></div>
    <div class="fact"><b>Starts</b><span class="fill">%(start_date)s</span></div>
    <div class="fact"><b>Where</b><span class="fill">%(location)s</span></div>
    <div class="fact"><b>Cost</b><span class="fill">%(price)s</span></div>
  </div>

  <div class="footer">
    <div class="org">%(orgmark)s<small>%(tagline)s</small></div>
    <div class="cta">Save a seat: %(signup)s<span>%(contact)s &middot; %(weeks)s, %(session)s</span></div>
  </div>
""" % DETAILS
    return page("Robots That Think — Grades 3-5", style, body)


# ===========================================================================
# JUNIOR 2 — the calm one. For the parent who wants specifics, not adjectives.
# ===========================================================================

def junior_infosheet():
    style = """
  body { background: #eef0f2; font-family: Georgia, "Times New Roman", serif; }
  .sheet { background: #fff; color: #1b1f24; }
  header { border-bottom: 3px double #1b3a5c; padding-bottom: 11px; margin-bottom: 15px; }
  .org { font-size: 11pt; letter-spacing: 0.2em; text-transform: uppercase; color: #1b3a5c; font-weight: bold; }
  h1 { font-size: 27pt; line-height: 1.12; margin: 5px 0 5px; }
  .standfirst { font-size: 11.5pt; line-height: 1.5; color: #3d4650; max-width: 6.2in; }
  h2 {
    font-family: "Segoe UI", system-ui, sans-serif; font-size: 9.5pt; letter-spacing: 0.16em;
    text-transform: uppercase; color: #1b3a5c; border-bottom: 1px solid #c9d3dd;
    padding-bottom: 4px; margin-bottom: 8px;
  }
  .two { display: flex; gap: 0.3in; }
  .two > div { flex: 1; }
  section { margin-bottom: 14px; }
  p, li { font-size: 10.5pt; line-height: 1.5; }
  ul { margin-left: 17px; }
  li { margin-bottom: 4px; }
  table { width: 100%; border-collapse: collapse; font-size: 10.5pt; }
  td { padding: 5px 0; border-bottom: 1px solid #e3e8ed; vertical-align: top; }
  td:first-child { width: 32%; color: #5a6672; font-family: "Segoe UI", system-ui, sans-serif; font-size: 9.5pt; }
  .weeks { background: #f4f7fa; border-left: 4px solid #1b3a5c; padding: 11px 13px; }
  .weeks b { font-family: "Segoe UI", system-ui, sans-serif; font-size: 9.5pt; }
  .qa b { display: block; font-family: "Segoe UI", system-ui, sans-serif; font-size: 10pt; margin-top: 8px; }
  .qa p { color: #3d4650; }
  footer {
    position: absolute; left: 0.4in; right: 0.4in; bottom: 0.34in;
    border-top: 3px double #1b3a5c; padding-top: 9px;
    font-size: 10pt; display: flex; justify-content: space-between;
  }
  footer b { font-family: "Segoe UI", system-ui, sans-serif; }
"""
    body = """  <header>
    <div class="org">%(orgmark)s &middot; After-School Program</div>
    <h1>Robotics for %(junior_grades)s</h1>
    <div class="standfirst">A weekly build class where children program real
    robots, test them, and find out why the answer that sounded right came
    third. %(weeks)s, %(session)s.</div>
  </header>

  <div class="two">
    <div>
      <section>
        <h2>What your child actually does</h2>
        <ul>
          <li>Builds a working brushbot in the first session and takes it home.</li>
          <li>Adds light sensors, then explains why the robot turns toward a torch.</li>
          <li>Drives a two-wheeled robot with a remote, then with a tablet.</li>
          <li>Reads live sensor numbers and predicts what the robot will do next.</li>
          <li>Chooses between ready-made programs to solve a maze &mdash; and
          measures which choice was faster.</li>
          <li>Builds maze sections that other teams' robots have to solve.</li>
        </ul>
      </section>

      <section>
        <h2>What they learn that lasts</h2>
        <p>Prediction before measurement. Testing an idea instead of defending
        it. Reading a number and deciding whether to trust it. These arrive
        wrapped in robots, which is the only reason a nine-year-old will sit
        still for them.</p>
      </section>
    </div>

    <div>
      <section>
        <h2>Practical details</h2>
        <table>
          <tr><td>Meets</td><td class="fill">%(day_time)s</td></tr>
          <tr><td>Starts</td><td class="fill">%(start_date)s</td></tr>
          <tr><td>Length</td><td>%(weeks)s, %(session)s</td></tr>
          <tr><td>Where</td><td class="fill">%(location)s</td></tr>
          <tr><td>Ages</td><td>%(junior_grades)s (homeschool ages vary &mdash; ask)</td></tr>
          <tr><td>Class size</td><td class="fill">%(class_size)s</td></tr>
          <tr><td>Cost</td><td class="fill">%(price)s</td></tr>
          <tr><td>Bring</td><td>Nothing. All parts and tools provided.</td></tr>
        </table>
      </section>

      <section class="weeks">
        <b>The shape of the term</b>
        <p>Weeks 1&ndash;2 brushbots &middot; 3&ndash;4 light-seeking brushbots
        &middot; 5&ndash;12 the two-wheeled robot, its sensors and its programs
        &middot; 13&ndash;15 build the maze, run the race.</p>
      </section>

      <section class="qa">
        <h2>Questions parents ask</h2>
        <b>Does my child need to know how to code?</b>
        <p>No. Nothing is typed. Programs are coloured blocks, and in this
        group they mostly choose and adjust ones we provide.</p>
        <b>Is it just following instructions?</b>
        <p>The building is guided. The thinking is not: every session ends with
        a question that has more than one defensible answer.</p>
        <b>What if they miss a week?</b>
        <p>Each session stands alone well enough to walk back into.</p>
      </section>
    </div>
  </div>

  <footer>
    <div><b>Register:</b> <span class="fill">%(signup)s</span></div>
    <div><span class="fill">%(contact)s</span></div>
  </footer>
""" % DETAILS
    return page("Robotics for Grades 3-5 — Information Sheet", style, body)


# ===========================================================================
# JUNIOR 3 — the story one. A journey down the page, warm and illustrated.
# ===========================================================================

def junior_journey():
    style = """
  body { background: #efe7d8; font-family: "Verdana", "Segoe UI", system-ui, sans-serif; }
  .sheet { background: #fffaf0; color: #2c2418; }
  h1 { font-size: 33pt; line-height: 1.05; color: #17706b; letter-spacing: -0.01em; }
  .hello { font-size: 11pt; letter-spacing: 0.2em; text-transform: uppercase; color: #a24a26; font-weight: bold; }
  .intro { font-size: 11pt; line-height: 1.55; margin: 9px 0 16px; max-width: 6.3in; }
  .track { position: relative; padding-left: 0.62in; }
  .track:before {
    content: ""; position: absolute; left: 0.19in; top: 6px; bottom: 22px;
    width: 4px; background: repeating-linear-gradient(#17706b 0 9px, transparent 9px 17px);
  }
  .stop { position: relative; margin-bottom: 13px; }
  .stop:before {
    content: ""; position: absolute; left: -0.55in; top: 2px;
    width: 22px; height: 22px; border-radius: 50%; background: #17706b;
    border: 4px solid #fffaf0; box-shadow: 0 0 0 3px #9ccfc9;
  }
  .stop h3 { font-size: 13pt; color: #17706b; margin-bottom: 2px; }
  .stop .when { font-size: 8.5pt; letter-spacing: 0.14em; text-transform: uppercase; color: #a24a26; font-weight: bold; }
  .stop p { font-size: 10pt; line-height: 1.45; color: #4a3f30; }
  .quote {
    background: #17706b; color: #fffaf0; border-radius: 14px; padding: 14px 18px;
    margin: 4px 0 14px; font-size: 12pt; line-height: 1.45; font-style: italic;
  }
  .quote b { font-style: normal; display: block; font-size: 9pt; letter-spacing: 0.14em;
    text-transform: uppercase; color: #d4f0ec; margin-top: 6px; }
  .details {
    display: flex; gap: 12px; background: #fdeedd; border: 2px dashed #c2603a;
    border-radius: 14px; padding: 12px 14px;
  }
  .details div { flex: 1; }
  .details b { display: block; font-size: 8.5pt; letter-spacing: 0.14em; text-transform: uppercase; color: #a24a26; }
  .details span { font-size: 11pt; font-weight: bold; }
  footer {
    position: absolute; left: 0.4in; right: 0.4in; bottom: 0.34in; text-align: center;
    font-size: 10pt; border-top: 2px solid #9ccfc9; padding-top: 9px;
  }
  footer .org { font-size: 13pt; font-weight: bold; color: #17706b; }
"""
    body = """  <div class="hello">%(org)s &middot; After school &middot; %(junior_grades)s</div>
  <h1>Fifteen weeks,<br>one very smart robot.</h1>
  <p class="intro">Nobody starts by programming a robot. You start by making
  something buzz across a table, and then you keep asking &ldquo;what if it
  could&hellip;&rdquo; until one day it is solving a maze without you.</p>

  <div class="track">
    <div class="stop">
      <div class="when">Week 1</div>
      <h3>It moves!</h3>
      <p>A toothbrush head, a tiny motor, a battery. Fifteen minutes in, it is
      skittering across the floor, and it goes home in a pocket that night.</p>
    </div>
    <div class="stop">
      <div class="when">Weeks 3&ndash;4</div>
      <h3>It notices things</h3>
      <p>We add light sensors. Now it chases a torch beam &mdash; and your child
      can tell you exactly which sensor saw what, and why that made it turn.</p>
    </div>
    <div class="stop">
      <div class="when">Weeks 5&ndash;9</div>
      <h3>It listens to you</h3>
      <p>A bigger robot, with wheels, a remote and a tablet app. Distance
      sensors, line sensors, a scanning head. They drive it, then read what it
      senses, then start telling it what to do about it.</p>
    </div>
    <div class="stop">
      <div class="when">Weeks 10&ndash;15</div>
      <h3>It thinks for itself</h3>
      <p>Teams build maze sections and swap them, so nobody can memorise the
      answer. Then each robot runs it alone, on the strategy its team chose.
      Fast and risky, or slow and certain? That argument is the whole point.</p>
    </div>
  </div>

  <div class="quote">
    We are not raising a room of engineers. We are raising people who take the
    back off things.
    <b>%(mission)s</b>
  </div>

  <div class="details">
    <div><b>When</b><span class="fill">%(day_time)s</span></div>
    <div><b>Starts</b><span class="fill">%(start_date)s</span></div>
    <div><b>Where</b><span class="fill">%(location)s</span></div>
    <div><b>Cost</b><span class="fill">%(price)s</span></div>
  </div>

  <footer>
    <div class="org">%(orgmark)s - %(tagline)s</div>
    <span class="fill">%(signup)s &middot; %(contact)s</span>
  </footer>
""" % DETAILS
    return page("Fifteen Weeks, One Very Smart Robot — Grades 3-5", style, body)


# ===========================================================================
# SENIOR 1 — the loud one. Workshop-floor energy, technical swagger.
# ===========================================================================

def senior_buildbay():
    style = """
  body { background: #0d0f12; font-family: "Segoe UI", system-ui, sans-serif; }
  .sheet {
    background: #14181d; color: #e8ecef;
    background-image: linear-gradient(#1b2027 1px, transparent 1px),
                      linear-gradient(90deg, #1b2027 1px, transparent 1px);
    background-size: 26px 26px;
  }
  .tag {
    display: inline-block; background: #35ff9b; color: #0d0f12; font-weight: 900;
    font-size: 9.5pt; letter-spacing: 0.2em; padding: 5px 11px; text-transform: uppercase;
  }
  h1 { font-size: 47pt; line-height: 0.95; font-weight: 900; margin: 12px 0 8px; letter-spacing: -0.02em; }
  h1 span { color: #35ff9b; }
  .sub { font-size: 12.5pt; line-height: 1.45; color: #9fb0bd; max-width: 6.1in; margin-bottom: 16px; }
  .grid { display: flex; gap: 0.2in; margin-bottom: 0.2in; }
  .cell { flex: 1; border: 1px solid #2b333d; background: #191e25; padding: 12px 13px; }
  .cell h3 {
    font-family: ui-monospace, "Cascadia Mono", Consolas, monospace;
    font-size: 10pt; color: #35ff9b; letter-spacing: 0.1em; margin-bottom: 6px;
  }
  .cell p { font-size: 9.8pt; line-height: 1.45; color: #c3ceD7; }
  .strip {
    border-left: 4px solid #35ff9b; background: #191e25; padding: 13px 16px; margin-bottom: 0.2in;
  }
  .strip h2 { font-size: 13pt; margin-bottom: 5px; }
  .strip p { font-size: 10.2pt; line-height: 1.5; color: #c3ced7; }
  .mono { font-family: ui-monospace, "Cascadia Mono", Consolas, monospace; }
  .specs { display: flex; gap: 0.2in; }
  .spec { flex: 1; }
  .spec b {
    display: block; font-family: ui-monospace, Consolas, monospace; font-size: 8.5pt;
    color: #8b9ba8; letter-spacing: 0.12em; text-transform: uppercase; margin-bottom: 3px;
  }
  .spec span { font-size: 12pt; font-weight: 800; }
  footer {
    position: absolute; left: 0.4in; right: 0.4in; bottom: 0.34in;
    border-top: 1px solid #2b333d; padding-top: 11px; display: flex;
    justify-content: space-between; align-items: flex-end;
  }
  footer .org { font-size: 14pt; font-weight: 900; }
  footer .org small { display: block; font-size: 9pt; color: #35ff9b; letter-spacing: 0.16em; }
  footer .cta { text-align: right; }
  footer .cta b { font-size: 11.5pt; }
  footer .cta span { display: block; font-size: 9.5pt; color: #9fb0bd; }
"""
    body = """  <div class="tag">%(senior_grades)s &middot; After school</div>
  <h1>YOU DON'T GET<br>A KIT.<br><span>YOU GET A LAB.</span></h1>
  <div class="sub">Most robotics classes hand you a robot and a worksheet. Here
  you get a robot, its source code, a soldering iron, and permission to change
  any of it.</div>

  <div class="grid">
    <div class="cell"><h3>&gt; WRITE IT</h3>
      <p>Your own programs, in blocks that compile to the robot's real command
      language. See both. Change either.</p></div>
    <div class="cell"><h3>&gt; WIRE IT</h3>
      <p>Add sensors the robot did not ship with. Solder them. Find out what
      the datasheet did not mention.</p></div>
    <div class="cell"><h3>&gt; BUILD IT</h3>
      <p>Design a mount, an arm, a bumper. If your idea needs a sensor 30 cm
      out front, build the thing that puts it there.</p></div>
  </div>

  <div class="strip">
    <h2>Then prove it works</h2>
    <p>Every claim gets tested on the floor. Teams build maze sections, swap
    them so nobody can memorise the answer, and race robots that have to solve
    a maze they have never seen. The team that says <span class="mono">&ldquo;we
    tuned it until it worked&rdquo;</span> loses to the team that can say
    <span class="mono">&ldquo;we follow the right-hand wall and accept it is
    slower&rdquo;</span> &mdash; and everyone watches it happen.</p>
  </div>

  <div class="grid">
    <div class="cell"><h3>&gt; REAL TOOLS</h3>
      <p>Soldering, multimeters, hand tools, 3D-printed and laser-cut parts.
      Supervised, and genuinely theirs to use.</p></div>
    <div class="cell"><h3>&gt; REAL CODE</h3>
      <p>Blocks first. Then the text underneath. Students who want it leave
      heading straight for Python.</p></div>
    <div class="cell"><h3>&gt; REAL FAILURE</h3>
      <p>Sensors lie, batteries sag, wheels slip. Debugging that is the
      engineering &mdash; the rest is assembly.</p></div>
  </div>

  <div class="specs">
    <div class="spec"><b>Runs</b><span>%(weeks)s / %(session)s</span></div>
    <div class="spec"><b>When</b><span class="fill">%(day_time)s</span></div>
    <div class="spec"><b>Where</b><span class="fill">%(location)s</span></div>
    <div class="spec"><b>Cost</b><span class="fill">%(price)s</span></div>
  </div>

  <footer>
    <div class="org">%(orgmark)s<small>%(tagline)s</small></div>
    <div class="cta"><b class="fill">%(signup)s</b><span class="fill">%(contact)s &middot; starts %(start_date)s</span></div>
  </footer>
""" % DETAILS
    return page("You Don't Get A Kit, You Get A Lab — Grades 6-12", style, body)


# ===========================================================================
# SENIOR 2 — the practical one. Reads like a syllabus, because some parents
# want to know precisely what is covered before they pay for it.
# ===========================================================================

def senior_syllabus():
    style = """
  body { background: #eceff2; font-family: "Segoe UI", system-ui, sans-serif; }
  .sheet { background: #fff; color: #1c2126; }
  header { display: flex; justify-content: space-between; align-items: flex-end;
    border-bottom: 4px solid #24506e; padding-bottom: 10px; margin-bottom: 14px; }
  h1 { font-size: 24pt; line-height: 1.1; }
  h1 small { display: block; font-size: 10pt; letter-spacing: 0.18em; text-transform: uppercase;
    color: #24506e; font-weight: 700; margin-bottom: 4px; }
  .meta { text-align: right; font-size: 9.5pt; line-height: 1.6; color: #4d5a66; }
  .meta b { color: #1c2126; }
  h2 { font-size: 9.5pt; letter-spacing: 0.16em; text-transform: uppercase; color: #24506e;
    margin-bottom: 7px; padding-bottom: 3px; border-bottom: 1px solid #d3dae1; }
  .two { display: flex; gap: 0.28in; }
  .two > div { flex: 1; }
  section { margin-bottom: 13px; }
  p, li { font-size: 10pt; line-height: 1.48; }
  ul { margin-left: 16px; }
  li { margin-bottom: 3px; }
  table { width: 100%; border-collapse: collapse; font-size: 9.6pt; }
  th { text-align: left; font-size: 8.5pt; letter-spacing: 0.1em; text-transform: uppercase;
    color: #6b7883; padding-bottom: 4px; }
  td { padding: 4px 0; border-top: 1px solid #e6ebef; vertical-align: top; }
  td:first-child { width: 22%; font-weight: 700; color: #24506e; }
  .note { background: #f2f6f9; border-left: 4px solid #24506e; padding: 10px 12px; font-size: 9.8pt; line-height: 1.5; }
  .bio { font-size: 9.5pt; line-height: 1.5; color: #4d5a66; }
  .bio b { color: #1c2126; }
  footer { position: absolute; left: 0.4in; right: 0.4in; bottom: 0.34in;
    border-top: 4px solid #24506e; padding-top: 9px; display: flex;
    justify-content: space-between; font-size: 10pt; }
"""
    body = """  <header>
    <h1><small>%(orgmark)s &middot; After-School Program</small>Robotics &amp; Engineering<br>%(senior_grades)s</h1>
    <div class="meta">
      <b class="fill">%(day_time)s</b><br>
      %(weeks)s &middot; %(session)s<br>
      Starts <span class="fill">%(start_date)s</span><br>
      <span class="fill">%(location)s</span> &middot; <span class="fill">%(price)s</span>
    </div>
  </header>

  <div class="two">
    <div>
      <section>
        <h2>Course arc</h2>
        <table>
          <tr><td>Weeks 1&ndash;2</td><td>Brushbots. Motors, batteries, and why a
          circuit that works on the bench fails on carpet.</td></tr>
          <tr><td>Weeks 3&ndash;4</td><td>Light-seeking brushbots. First sensor,
          first feedback loop, first argument about what the robot &ldquo;knows&rdquo;.</td></tr>
          <tr><td>Weeks 5&ndash;7</td><td>The two-wheeled platform. Assembly,
          wiring, motor control, and reading real sensor data.</td></tr>
          <tr><td>Weeks 8&ndash;11</td><td>Programming: sequences, loops,
          conditionals. Calibration and measurement. Debugging by evidence.</td></tr>
          <tr><td>Weeks 12&ndash;13</td><td>Extensions. Students design and build
          their own additions &mdash; new sensors, mounts, arms, bumpers.</td></tr>
          <tr><td>Weeks 14&ndash;15</td><td>Maze build and race, then a showcase
          where each team explains its design and its failures.</td></tr>
        </table>
      </section>

      <section>
        <h2>Skills practised</h2>
        <ul>
          <li>Soldering, wiring, and reading a simple schematic</li>
          <li>Using a multimeter to answer a question, not just take a reading</li>
          <li>Block programming with the generated code visible alongside</li>
          <li>Measurement and calibration, including repeat trials and spread</li>
          <li>Mechanical design and fabrication for a part they specified</li>
          <li>Explaining a design decision, including the trade-off it cost</li>
        </ul>
      </section>
    </div>

    <div>
      <section>
        <h2>What students design themselves</h2>
        <p>The second half is not scripted. A student who wants a distance
        sensor on a 30 cm arm has to mount it, wire it, decide what the robot
        should do with the reading, and defend why the arm was worth the extra
        weight. Some builds fail. Those weeks teach the most.</p>
      </section>

      <section class="note">
        <b>No experience needed.</b> Nothing assumes prior coding or
        electronics. Students who arrive already programming are not held back
        &mdash; the robot's own source code is open and readable, and there is
        always more depth available than any student exhausts.
      </section>

      <section>
        <h2>Homeschool notes</h2>
        <ul>
          <li>Grade bands are a guide. Place by readiness and interest.</li>
          <li>Siblings across the band often work well as a team.</li>
          <li>Documentation-friendly: students keep a build log, and the final
          showcase works as a portfolio artefact.</li>
        </ul>
      </section>

      <section>
        <h2>Who teaches it</h2>
        <p class="bio">Run by <b>%(org)s</b>. Instructor background: Marine Corps
        electronics test and calibration, a career in cybersecurity leadership,
        founder of a community hackerspace, and a parent who taught his own kids
        this way first.</p>
      </section>
    </div>
  </div>

  <footer>
    <div><b>Register:</b> <span class="fill">%(signup)s</span></div>
    <div><span class="fill">%(contact)s</span> &middot; class size <span class="fill">%(class_size)s</span></div>
  </footer>
""" % DETAILS
    return page("Robotics & Engineering, Grades 6-12 — Course Outline", style, body)


# ===========================================================================
# SENIOR 3 — the editorial one. One argument, made well, for the parent who
# is choosing between this and three other activities.
# ===========================================================================

def senior_editorial():
    style = """
  body { background: #e8e4dc; font-family: Georgia, "Times New Roman", serif; }
  .sheet { background: #fbf9f4; color: #1a1714; }
  .rule { height: 7px; background: #b3311f; margin-bottom: 13px; }
  .kicker { font-family: "Segoe UI", system-ui, sans-serif; font-size: 9pt;
    letter-spacing: 0.24em; text-transform: uppercase; color: #b3311f; font-weight: 700; }
  h1 { font-size: 36pt; line-height: 1.04; margin: 8px 0 10px; letter-spacing: -0.015em; }
  .deck { font-size: 12.5pt; line-height: 1.5; color: #453e36; font-style: italic;
    border-bottom: 1px solid #d8d0c4; padding-bottom: 12px; margin-bottom: 13px; }
  .cols { column-count: 2; column-gap: 0.32in; }
  .cols p { font-size: 10.2pt; line-height: 1.55; margin-bottom: 8px; }
  .cols p:first-of-type:first-letter {
    float: left; font-size: 34pt; line-height: 0.82; padding: 3px 6px 0 0; color: #b3311f; font-weight: bold;
  }
  .pull {
    break-inside: avoid; border-top: 3px solid #1a1714; border-bottom: 3px solid #1a1714;
    padding: 9px 0; margin: 4px 0 9px; font-size: 13pt; line-height: 1.32; font-weight: bold;
  }
  .box {
    background: #1a1714; color: #fbf9f4; padding: 13px 16px; margin-top: 10px;
    display: flex; gap: 16px;
  }
  .box div { flex: 1; }
  .box b { font-family: "Segoe UI", system-ui, sans-serif; display: block; font-size: 8.5pt;
    letter-spacing: 0.14em; text-transform: uppercase; color: #e2a79c; margin-bottom: 2px; }
  .box span { font-size: 11.5pt; font-weight: bold; }
  footer {
    position: absolute; left: 0.4in; right: 0.4in; bottom: 0.34in;
    border-top: 1px solid #d8d0c4; padding-top: 9px;
    font-family: "Segoe UI", system-ui, sans-serif; font-size: 9.5pt;
    display: flex; justify-content: space-between; align-items: center;
  }
  footer .org { font-size: 12pt; font-weight: 800; }
  footer .org small { display: block; font-size: 8.5pt; letter-spacing: 0.16em; color: #b3311f; }
"""
    body = """  <div class="rule"></div>
  <div class="kicker">%(org)s &middot; After school &middot; %(senior_grades)s</div>
  <h1>The robot is not<br>the point.</h1>
  <div class="deck">Fifteen weeks of building, wiring and programming &mdash;
  aimed squarely at the habit underneath: figuring out why something did not
  work, and finding out for yourself.</div>

  <div class="cols">
    <p>Everyone's robot drives in week six. That part is not hard, and it is
    not what we are selling. What happens next is: it drifts to the left. It
    stops seeing a wall that is obviously there. It runs perfectly on Tuesday
    and badly on Thursday.</p>

    <div class="pull">A student who can find out why is doing engineering. One
    who tries settings until it works is doing something else.</div>

    <p>So we teach the finding out. Measure twice and compare the numbers.
    Change one thing. Ask what the sensor is actually detecting, which is never
    quite what you assumed. Discover that the batteries were the problem all
    along and that four sessions of clever theories were wasted &mdash; which
    stings once and is remembered for years.</p>

    <p>By the second half students are designing their own additions: a sensor
    on an arm, a bumper, a new behaviour that nobody demonstrated. They wire it,
    program it, and explain what it costs. Every design trades something away,
    and being able to name the trade is the skill.</p>

    <p>The term ends with a maze race on a course nobody has seen, and a
    showcase where each team explains its strategy, its results, and what still
    beats it. Explaining it to someone else is the last test of understanding
    it, which is why we make it the finale.</p>

    <p>It starts smaller than you would expect. Week one is a brushbot: a
    motor, a battery, a toothbrush head, and a machine that moves. We build up
    from there, because the students who get furthest are the ones who were
    never once told to just follow the steps.</p>
  </div>

  <div class="box">
    <div><b>Meets</b><span class="fill">%(day_time)s</span></div>
    <div><b>Starts</b><span class="fill">%(start_date)s</span></div>
    <div><b>Runs</b><span>%(weeks)s</span></div>
    <div><b>Where</b><span class="fill">%(location)s</span></div>
    <div><b>Cost</b><span class="fill">%(price)s</span></div>
  </div>

  <footer>
    <div class="org">%(orgmark)s<small>%(tagline)s</small></div>
    <div><span class="fill">%(signup)s</span> &middot; <span class="fill">%(contact)s</span></div>
  </footer>
""" % DETAILS
    return page("The Robot Is Not The Point — Grades 6-12", style, body)


# ===========================================================================

FLYERS = [
    ("junior-1-poster.html", junior_poster, "Grades 3&ndash;5",
     "Poster", "Loud and visual. Big type, three steps, almost no reading. For a table at a fair or a noticeboard."),
    ("junior-2-infosheet.html", junior_infosheet, "Grades 3&ndash;5",
     "Information sheet", "Calm and specific. Serif, two columns, a full details table and the questions parents actually ask."),
    ("junior-3-journey.html", junior_journey, "Grades 3&ndash;5",
     "The journey", "Warm and narrative. A dotted track down the page, week by week, ending on why it matters."),
    ("senior-1-buildbay.html", senior_buildbay, "Grades 6&ndash;12",
     "Build bay", "Dark, technical, high-energy. Blueprint grid and terminal green. Aimed at the teenager as much as the parent."),
    ("senior-2-syllabus.html", senior_syllabus, "Grades 6&ndash;12",
     "Course outline", "Reads like a syllabus: week-by-week arc, skills list, homeschool notes, instructor background."),
    ("senior-3-editorial.html", senior_editorial, "Grades 6&ndash;12",
     "Editorial", "One argument, made properly. Magazine layout with a drop cap and a pull quote. For the parent choosing between four activities."),
]


def preview_index():
    cards = []
    for filename, _, band, name, note in FLYERS:
        cards.append("""
    <a class="card" href="%s" target="_blank">
      <span class="band">%s</span>
      <span class="name">%s</span>
      <span class="note">%s</span>
      <span class="open">Open &amp; print &rarr;</span>
    </a>""" % (filename, band, name, note))
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Flyers &mdash; %(org)s</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body { background: #15181c; color: #e8ecef; font-family: system-ui, "Segoe UI", sans-serif;
         padding: 28px 20px 60px; line-height: 1.55; }
  .wrap { max-width: 860px; margin: 0 auto; }
  h1 { font-size: 26px; margin-bottom: 6px; }
  .sub { color: #8a949e; margin-bottom: 8px; }
  .todo { background: #241a12; border: 1px solid #ff7a1a; border-radius: 10px;
          padding: 12px 14px; font-size: 14px; margin: 16px 0 22px; }
  .todo b { color: #ff7a1a; }
  code { font-family: ui-monospace, Consolas, monospace; background: #12151a;
         padding: 1px 5px; border-radius: 4px; }
  .grid { display: grid; gap: 12px; }
  @media (min-width: 700px) { .grid { grid-template-columns: repeat(3, 1fr); } }
  .card { display: block; text-decoration: none; color: inherit; background: #1e2329;
          border: 1px solid #2b323b; border-radius: 12px; padding: 15px; }
  .card:hover { border-color: #ff7a1a; }
  .band { font-size: 11px; letter-spacing: 0.14em; text-transform: uppercase; color: #ff7a1a; font-weight: 700; }
  .name { display: block; font-size: 19px; font-weight: 800; margin: 3px 0 6px; }
  .note { display: block; font-size: 13px; color: #8a949e; }
  .open { display: block; margin-top: 10px; font-size: 13px; font-weight: 700; color: #ff7a1a; }
  h2 { font-size: 13px; letter-spacing: 0.14em; text-transform: uppercase; color: #8a949e;
       margin: 26px 0 10px; }
  .top { display: flex; align-items: center; gap: 14px; margin-bottom: 18px; }
  .mlk-lockup { display: inline-flex; background: #fff; border-radius: 8px; padding: 3px 8px; line-height: 0; }
  .mlk-lockup img { height: 34px; width: auto; display: block; }
  .back { color: #8a949e; font-size: 13px; text-decoration: none; }
</style>
</head>
<body>
<div class="wrap">
  <p class="top"><a class="mlk-lockup" href="https://makerlabkids.com/"><img src="../img/lockup.png" alt="Maker Lab Kids" width="300" height="99"></a>
  <a class="back" href="../index.html">&larr; Robot Workshop</a></p>
  <h1>Flyers for the after-school program</h1>
  <p class="sub">Six one-pagers: three for %(junior_grades)s, three for
  %(senior_grades)s. Different looks and different arguments, so you can see
  which one works before printing a hundred.</p>

  <div class="todo">
    <b>Before printing:</b> every bracketed field &mdash; day and time, start
    date, location, price, contact, sign-up link, class size &mdash; is filled in
    from one place. Edit <code>DETAILS</code> at the top of
    <code>build-flyers.py</code> and run <code>python3 build-flyers.py</code>, or
    just edit the HTML by hand if you prefer.
  </div>

  <h2>Grades 3&ndash;5 &mdash; run the programs, build the mazes</h2>
  <div class="grid">%(junior_cards)s</div>

  <h2>Grades 6&ndash;12 &mdash; write the programs, extend the robot</h2>
  <div class="grid">%(senior_cards)s</div>

  <h2>Printing</h2>
  <p class="sub">Each flyer is sized for US Letter with a dashed outline on
  screen showing where the paper ends. Print at 100%%, background graphics on
  (Chrome: More settings &rarr; Background graphics), otherwise the coloured
  panels vanish. After you fill in the real details, open Chrome's print
  preview once per flyer: if a long location or price pushes content past the
  outline it will spill to a second sheet rather than disappear, which is
  deliberate &mdash; you want to see it.</p>

  <h2>What differs between the two tracks</h2>
  <p class="sub">The junior flyers promise <b>running and choosing</b> programs:
  kids explore what the ready-made behaviours do, build maze sections, and
  discover that picking the right strategy beats picking the fastest one. The
  senior flyers promise <b>writing</b> programs and <b>changing the robot</b>:
  their own block programs, their own sensors, their own mounts and arms, and
  defending the trade-offs. Both start on brushbots for the first month, which
  is worth saying out loud to parents &mdash; the first thing their child makes
  moves on day one.</p>
</div>
</body>
</html>
""" % {
        "org": DETAILS["org"],
        "junior_grades": DETAILS["junior_grades"],
        "senior_grades": DETAILS["senior_grades"],
        "junior_cards": "".join(c for c, f in zip(cards, FLYERS) if f[2] == DETAILS["junior_grades"]),
        "senior_cards": "".join(c for c, f in zip(cards, FLYERS) if f[2] == DETAILS["senior_grades"]),
    }


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    for filename, builder, _band, _name, _note in FLYERS:
        with open(os.path.join(here, filename), "w") as handle:
            handle.write(builder())
        print("  " + filename)
    with open(os.path.join(here, "index.html"), "w") as handle:
        handle.write(preview_index())
    print("  index.html")


if __name__ == "__main__":
    main()
