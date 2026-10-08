#!/usr/bin/env python3
"""Builds the 2WD robot's lesson pages, lesson index, hub (2wd.html), cheat
sheet and setup page. /index.html is the mecanum robot's hub and is not
built here.

Everything a lesson says lives in LESSONS below, so fixing a typo or adding a
week means editing data, not HTML. Run:  python3 build-site.py
"""

import html as html_escape
import os

SITE_TITLE = "Maker Lab Kids &middot; Robot Workshop"
TAGLINE = "Make. Learn. Share."

# The Maker Lab Kids lockup at the top of every page (styled in css/site.css).
# Its own copy in img/ keeps this folder self-contained (it works copied
# anywhere, offline); up = "../" from the lessons folder.
def lockup(up=""):
    return ('<a class="mlk-lockup" href="https://makerlabkids.com/"><img src="%simg/lockup.png" '
            'alt="Maker Lab Kids" width="300" height="99"></a>' % up)

# ---------------------------------------------------------------------------
# The curriculum. One entry per lesson.
#   goal      - one sentence a kid can repeat back
#   concepts  - the ideas introduced, in kid words
#   sensethink- how this lesson maps to Sense / Think / Do
#   steps     - what happens in the session, in order
#   program   - sample robot-language program (or None)
#   program_note - what the sample does and why
#   challenges- "try this" extensions, hardest last
#   watchfor  - what usually goes wrong, and what it means
#   teacher   - notes for the adult in the room
# ---------------------------------------------------------------------------

LESSONS = [
    {
        "slug": "01-robot-or-not",
        "title": "Robot or Not?",
        "subtitle": "What makes a machine a robot",
        "minutes": 60,
        "goal": "Decide for yourself whether something is a robot, using a test you can defend.",
        "concepts": [
            "Sense &rarr; Think &rarr; Do: the loop every robot runs",
            "A toaster senses and does, but barely thinks",
            "A remote-control car does, but you are doing the thinking",
            "Autonomy is a slider, not a switch",
        ],
        "sensethink": "Today the robot is mostly DO. You are its brain. That is the point: by the end of the club, you will hand the thinking over to the robot one piece at a time.",
        "steps": [
            "Argue about five machines: toaster, dishwasher, drone, Roomba, self-driving car. Robot or not? Write down the reason, not just the verdict.",
            "Build the test as a class: it senses something, it decides something, it acts on the world. Two out of three is not a robot.",
            "Meet the robot. Turn it on and watch the servo wiggle &mdash; that is the robot saying 'my parts are connected'.",
            "Drive it with the IR remote. Ask: who is sensing? Who is thinking? Who is doing?",
            "Press 5 (obstacle avoidance) and put your hand in front of it. Ask the same three questions again. Something changed.",
        ],
        "program": None,
        "program_note": None,
        "challenges": [
            "Find a machine at home that senses but does not think. Bring the argument next week.",
            "Is a thermostat a robot? Defend either answer &mdash; both can be right if the reason is good.",
            "Where on the autonomy slider is our robot with the remote? Without the remote?",
        ],
        "watchfor": [
            ("The servo does not wiggle at startup", "The servo is unplugged or in the wrong header. Nothing else today will work either."),
            ("The robot ignores the remote", "Check the remote's battery tab is pulled out, and that you are within a few metres, pointing roughly at the front."),
        ],
        "teacher": "Resist settling the argument. The disagreement is the lesson &mdash; you want them to leave with a test they built, because they will use it against you in week 10 when the robot does something clever and they want to know whether it 'really' decided.",
    },
    {
        "slug": "02-driving-and-the-remote",
        "title": "Driving &amp; the Remote",
        "subtitle": "Commands, control, and who is in charge",
        "minutes": 60,
        "goal": "Drive the robot three different ways and explain what a command actually is.",
        "concepts": [
            "A command is a message, not a wish &mdash; something has to receive it",
            "Two controllers, one robot: first one to act owns it",
            "STOP always wins, from anybody",
            "Dead-man control: hold to go, release to stop",
        ],
        "sensethink": "Still mostly DO, but now you can see the SENSE half on the phone while you drive.",
        "steps": [
            "IR remote first: arrows drive, OK stops, numbers pick modes. No phone needed &mdash; this is the controller that works when the wifi does not.",
            "Connect a phone to the Workshop app. Watch the console: every button you press writes a line.",
            "Drive with the joystick. Notice the motor bars move as you steer.",
            "Two kids, one robot: one on the remote, one on the phone. Whoever acts first owns it; the other gets BUSY. Try to break it.",
            "Press 0 on the remote to release control, then let the other person take over.",
        ],
        "program": None,
        "program_note": None,
        "challenges": [
            "Drive a figure-8 around two chairs without touching either.",
            "Have a partner shout directions while you drive with your eyes closed. Notice how bad the delay makes you.",
            "Find the slowest speed the robot will still move at. Why does it stall below that?",
        ],
        "watchfor": [
            ("The phone will not connect", "Bluetooth AND location must be on for Android Chrome. The page must be https or localhost."),
            ("Robot drives, then stops on its own after a moment", "That is the dead-man safety: manual driving expires if the robot stops hearing from you. Hold the button, do not tap it."),
        ],
        "teacher": "The arbitration demo is worth the time. Kids fighting over one robot is a management problem right up until it becomes the lesson.",
    },
    {
        "slug": "03-sensors",
        "title": "What the Robot Can Feel",
        "subtitle": "Reading real sensors, including the broken ones",
        "minutes": 60,
        "goal": "Name every sensor on the robot and predict what its number does before you test it.",
        "concepts": [
            "A sensor turns something physical into a number",
            "Predict first, then measure &mdash; being wrong on purpose is the fastest way to learn",
            "Sensors lie: sunlight, dark carpet, and shiny walls all fool them",
            "Raw values versus meaning: 720 is raw, 'bright' is interpretation",
        ],
        "sensethink": "All SENSE today. No driving at all, so nothing distracts from the numbers.",
        "steps": [
            "Open the Workshop app, press <b>Read sensors</b>. Every display lights up with live numbers.",
            "Line sensors: slide a black strip under each one. Predict which dot fills first.",
            "Proximity: bring a hand in slowly. Measure the distance where it first triggers &mdash; it is only a few inches.",
            "Light sensors: cover one with a thumb. Which bar shrinks? Now shine a phone torch on it.",
            "Distance sensor: hold a book at 10 cm, 30 cm, 100 cm. Write down the readings and the errors.",
            "Break it on purpose: point the distance sensor at a soft jumper, then at an angled wall. Watch it fail. That failure is real and it will bite you in the maze.",
        ],
        "program": None,
        "program_note": None,
        "challenges": [
            "Find a surface the distance sensor cannot see at all. Explain why sound bounces away from it.",
            "Make the light sensors read equal without turning the robot &mdash; move the light instead.",
            "Guess a distance, then measure. Get within 5 cm three times in a row.",
        ],
        "watchfor": [
            ("Distance always reads 'clear'", "The sonar is not answering. Press Read sensors and look for NOECHO climbing &mdash; that is the robot telling you the sensor never replied."),
            ("Proximity triggers at 3&ndash;4 inches only", "That is normal for these sensors. They are whiskers, not eyes."),
        ],
        "teacher": "Have them write predictions on paper before each test. The gap between prediction and measurement is where the learning lands, and it is invisible if they only ever read the answer off a screen.",
    },
    {
        "slug": "04-calibration",
        "title": "Every Robot Is Different",
        "subtitle": "Measuring your own robot's numbers",
        "minutes": 60,
        "goal": "Produce two numbers &mdash; milliseconds per room and milliseconds per quarter turn &mdash; that are true for YOUR robot, and explain why one measurement was not enough.",
        "concepts": [
            "Identical robots are not identical: wheels, motors, batteries, floor",
            "Measure, do not guess",
            "Repeat a measurement to see how much it wobbles",
            "Differential measurement: subtract two runs to cancel out what they share",
            "Multiply the error to see it: four turns show a mistake four times bigger",
        ],
        "sensethink": "THINK, with a stopwatch. The robot does the same thing every time; you are measuring what 'the same thing' actually means.",
        "steps": [
            "Open the <b>Calibrate</b> tab. Start at Step 0 and watch a launch: it should creep off the line, not chirp.",
            "Step 1: two runs from the same start line &mdash; one second, then three seconds. Measure both in centimetres and add them as one trial. Do it three times.",
            "Why two runs? Both start from a standstill, so both contain the same speeding-up. Subtract them and the speeding-up cancels out, leaving pure cruising. This trick has a name in science: a differential measurement.",
            "Look at the spread. If the three trials disagree by a lot, something is inconsistent &mdash; find out what before continuing.",
            "Step 2: the quadruple spin. Four quarter turns should land the arrow back where it started. Read the miss like a clock face.",
            "Step 3: the drift test. Three seconds straight, then measure how far off the line it ended up, and apply the trim.",
            "Step 4: victory lap. A square, using YOUR numbers. If it comes home, you are calibrated.",
            "Name and save your robot. Write the two numbers on tape and stick it to the chassis.",
        ],
        "program": "V 5\nW 200\nP 4\nF 450\nL 350\nQ",
        "program_note": "The victory lap: repeat four times &mdash; drive half a room, quarter turn left. If your numbers are right, the robot ends where it started, facing the way it started.",
        "challenges": [
            "Run the same test on a hard floor and on carpet. Which numbers change, and why?",
            "Swap in a fresh battery pack and re-measure. How much did speed change?",
            "Predict the distance for a 2.5-second drive, then test your prediction. (Careful: the ramp is in there too.)",
            "Work out how far the robot loses to speeding up. You have the numbers: how far it went in one second, and how far it would have gone at full speed for one second.",
        ],
        "watchfor": [
            ("Trials disagree by more than about 120 ms", "Something is loose, the battery is tired, or the surface changed. The app warns you about this."),
            ("Robot swerves during the very first moment", "Acceleration is too sharp &mdash; one wheel is breaking traction. Slow the launch on Step 0 first, then re-measure everything."),
        ],
        "teacher": "This is the most important lesson in the whole club and the least exciting. Sell it by refusing to accept a maze run later that was not calibrated &mdash; the kids who skip today will fail visibly in week 10 and re-learn it the hard way.",
    },
    {
        "slug": "05-first-program",
        "title": "Your First Program",
        "subtitle": "Blocks, and what the robot actually receives",
        "minutes": 60,
        "goal": "Build a program from blocks, send it, run it, and read the same program back off the robot.",
        "concepts": [
            "A program is a list of instructions, run in order, with nobody watching",
            "The robot has no eyes on your intentions &mdash; only your steps",
            "Blocks and text are two views of the same thing",
            "The robot remembers: programs survive being switched off",
        ],
        "sensethink": "THINK, written down in advance. The robot does exactly what you wrote, which is the whole joy and the whole problem.",
        "steps": [
            "Open the <b>Blocks</b> tab. Tap 'set speed', then 'drive rooms'. Watch the text box fill in underneath.",
            "Look at the robot language: <code>V 5</code> then <code>F 900</code>. Every block is one line.",
            "Press <b>Send to robot</b>. Watch for SAVED steps=2 &mdash; that number must match your block count.",
            "Press <b>Run</b>. Watch each step announce itself as it happens.",
            "Now press <b>1</b> on the IR remote. The same program runs, with no phone at all.",
            "Save a different program to Slot 2 and switch between them from the remote.",
        ],
        "program": "V 5\nF 900\nL 350\nF 900",
        "program_note": "Drive one room, turn a quarter turn left, drive one more room. An L-shaped path &mdash; the smallest program that goes somewhere interesting.",
        "challenges": [
            "Write a program that draws a triangle. What angle do you need, and how do you get it from quarter turns?",
            "Write the shortest program that returns the robot exactly to its starting spot.",
            "Put a program in each of the three slots so a partner can run them from the remote without knowing what they do.",
        ],
        "watchfor": [
            ("SAVED steps= does not match your block count", "Some of the program was lost on the way. Send it again; if it keeps happening, say so &mdash; that is a real bug, not your mistake."),
            ("Robot runs the wrong program", "Check which slot is selected. The slot button glows."),
        ],
        "teacher": "The read-back is the moment worth pausing on: press Read back and watch their program appear from the robot's own memory. That is when 'the robot stored it' stops being a claim and becomes a fact.",
    },
    {
        "slug": "06-loops",
        "title": "Doing It Again",
        "subtitle": "Loops, and why repetition is not copy-paste",
        "minutes": 60,
        "goal": "Use a repeat block to make a square, then change the square's size by editing one number.",
        "concepts": [
            "A loop says 'do this N times' instead of writing it N times",
            "One number to change instead of many &mdash; fewer chances to be wrong",
            "Loops can hold loops",
            "The robot's memory is finite: 100 steps, so loops buy you room",
        ],
        "sensethink": "THINK, compressed. The same behaviour in fewer instructions.",
        "steps": [
            "Build a square the long way: eight blocks, drive-turn-drive-turn-drive-turn-drive-turn. Run it.",
            "Now build it with a repeat block: repeat 4 times, containing drive and turn. Two blocks inside one.",
            "Compare the robot language. Same behaviour, a third of the lines.",
            "Change the repeat count to 3. Predict the shape before running it.",
            "Change the drive distance to 2 rooms. One number, bigger square.",
            "Put a repeat inside a repeat and see what happens. Predict first.",
        ],
        "program": "V 5\nP 4\nF 900\nL 350\nQ",
        "program_note": "A square. P 4 starts 'repeat four times', Q ends it. Everything between them happens four times.",
        "challenges": [
            "Make a staircase: forward, turn, forward, turn back &mdash; repeated. Get the turns to alternate.",
            "Make a spiral where each side is longer than the last. Can a loop do that, or do you need something else?",
            "What is the fewest blocks that can make a hexagon? Is it even possible with quarter turns?",
        ],
        "watchfor": [
            ("Blocks land after the repeat instead of inside it", "Tap the dotted area inside the repeat block first, then tap the palette. The dotted area glows when it is aimed."),
            ("The square drifts into a spiral", "Turns are not exactly 90&deg;. Back to calibration &mdash; or use it: a deliberate spiral is a fine challenge."),
        ],
        "teacher": "The hexagon challenge is a good trap: quarter turns cannot make 60&deg;, so they have to notice the limitation and ask for timed spins instead. Let them hit the wall before offering the tool.",
    },
    {
        "slug": "07-if-sensor",
        "title": "Making Decisions",
        "subtitle": "If, else, and the robot thinking for itself",
        "minutes": 60,
        "goal": "Write a program that behaves differently depending on what the sensor sees.",
        "concepts": [
            "A condition is a question with a yes/no answer",
            "'If' runs a block only when the answer is yes",
            "Our robot has no 'else', so we ask the opposite question instead",
            "Now the robot is doing the thinking &mdash; this is the autonomy slider moving",
        ],
        "sensethink": "SENSE feeding THINK feeding DO, for the first time, all inside the robot.",
        "steps": [
            "Look at the sensor list in the if-block: line sensors, proximity, wall-closer-than-25cm, and their opposites.",
            "Build: if wall closer than 25 cm, then back up. Run it while holding a book in front.",
            "Notice sensors 6, 7 and 8 are the opposites of 5, 4 and 3. That is how we fake an 'else'.",
            "Build a pair: if wall close, spin left; if path clear, drive forward. Two ifs, opposite questions.",
            "Wrap the pair in a repeat so it keeps checking. Now it is a behaviour, not a one-shot.",
        ],
        "program": "V 5\nP 10\nI 5\nL 350\nZ\nI 6\nF 400\nZ\nQ",
        "program_note": "Repeat ten times: IF a wall is closer than 25 cm, turn left; IF the way ahead is clear, drive forward. Two opposite questions do the job of if/else.",
        "challenges": [
            "Make the robot follow a wall on its right without touching it.",
            "Make it stop dead if BOTH line sensors see black (a finish line).",
            "The robot only checks the sensor when it reaches the if-block. What does it miss in between? Design an experiment to prove it.",
        ],
        "watchfor": [
            ("The robot ignores the sensor completely", "The check happens at the instant the if-block runs, not continuously. A long drive before it means a stale answer."),
            ("Both branches run", "That cannot happen if the questions are true opposites. Check you used 5 and 6, not 5 and 5."),
        ],
        "teacher": "The last challenge is the deep one: sampling versus continuous awareness. It is the same reason a self-driving car needs to check often, and it is a genuinely hard idea, so leave time.",
    },
    {
        "slug": "08-line-following",
        "title": "Following the Line",
        "subtitle": "A feedback loop you can see",
        "minutes": 60,
        "goal": "Explain, while watching the robot, why it wiggles down the line instead of driving straight along it.",
        "concepts": [
            "Feedback: measure, correct, measure again",
            "Correcting always means overshooting a little",
            "Two sensors give you four possible situations",
            "The wiggle is not a bug &mdash; it is the loop working",
        ],
        "sensethink": "A tight SENSE-THINK-DO loop running many times a second, right in front of you.",
        "steps": [
            "Lay a black tape line with a gentle curve. Press <b>4</b> on the remote or Line tracking in the app.",
            "Watch the two dots on the phone while the robot drives. Say out loud what it sees and what it does.",
            "Fill in the table: left black &rarr; turn left; right black &rarr; turn right; neither &rarr; straight; both &rarr; stop.",
            "Make the curve sharper until the robot loses the line. Find the tightest curve it can hold.",
            "Change the surface or the lighting. Notice the sensors are reading reflection, not colour.",
        ],
        "program": None,
        "program_note": None,
        "challenges": [
            "Design a track the robot definitely cannot follow, then explain exactly why in sensor terms.",
            "Add a gap in the line. What does the robot do? What SHOULD it do?",
            "Could you write the line follower yourself with if-blocks? Try it, then compare with the built-in one.",
        ],
        "watchfor": [
            ("The robot runs off a black line on a dark floor", "The sensors compare reflection. Dark floor, dark line, no contrast, no chance."),
            ("It wiggles violently", "The correction is strong relative to the speed. Lower the speed and watch the wiggle shrink."),
        ],
        "teacher": "The wiggle IS the lesson. Every control system overshoots; a thermostat does it in temperature and you cannot see it. Here it is right there on the floor.",
    },
    {
        "slug": "09-avoid-and-seek",
        "title": "Avoiding &amp; Seeking",
        "subtitle": "Strategies for not knowing everything",
        "minutes": 60,
        "goal": "Compare two robot behaviours and describe what each one does when it loses track of the world.",
        "concepts": [
            "Scanning: turning the sensor to learn more before deciding",
            "A plan for being lost is part of a plan",
            "Follow waits when it loses the target; Seek goes looking",
            "Giving up is a feature &mdash; searching forever is a bug",
        ],
        "sensethink": "THINK gets a memory: the robot now stores what it saw a moment ago and uses it.",
        "steps": [
            "Press <b>5</b> (avoid). Corner the robot and watch it scan left, scan right, then commit.",
            "Corner it harder &mdash; block every direction. Watch it rotate 45&deg; and rescan, again and again, then stop and say it is boxed in.",
            "Press <b>6</b> (follow). Hold a book in front, then take it away. It waits.",
            "Press <b>8</b> (seek). Same trick. It surveys, locks on, chases, and when it loses you, searches in widening arcs.",
            "Discuss: which behaviour would you want on a robot vacuum? On a rescue robot? Why are they different?",
        ],
        "program": None,
        "program_note": None,
        "challenges": [
            "Build a corner the avoider cannot escape. Is it possible if the room has an exit?",
            "Time how long seek takes to find you from behind. Explain the number using the search pattern.",
            "Write your own avoider with if-blocks. Where does it fall short of the built-in one?",
        ],
        "watchfor": [
            ("Follow and seek do nothing at all", "No distance readings. Check NOECHO with Read sensors &mdash; the sonar is not answering."),
            ("Seek spins and gives up quickly", "It surveys three times, finds no echoes, and stops rather than pirouetting forever. That is deliberate."),
        ],
        "teacher": "The vacuum-versus-rescue question is the real content. Same sensors, same code structures, different priorities &mdash; engineering is choosing which failure you prefer.",
    },
    {
        "slug": "10-maze-race",
        "title": "The Maze Race",
        "subtitle": "Everything at once, against the clock",
        "minutes": 60,
        "goal": "Get your robot through a maze it has never seen, using a strategy you can explain.",
        "concepts": [
            "Dead reckoning: fast, but every error adds up",
            "Wall following: slower, but solves mazes you have not seen",
            "The right-hand rule always escapes a simple maze &mdash; and here is why",
            "Choosing a strategy is engineering: fast versus reliable",
        ],
        "sensethink": "The full loop, under time pressure, with a maze nobody has calibrated for.",
        "steps": [
            "Teams assemble their 2&times;2 room modules. Modules get rearranged between runs, so nobody can memorise the answer.",
            "Open the <b>Maze Lab</b> and try both strategies in simulation before touching a robot.",
            "Load a dead-reckoning program into Slot 1 and a wall-follower into Slot 2.",
            "Practice run. Note where each one fails and why.",
            "Race. Pick your slot at the start line by pressing 1 or 2 on the remote &mdash; no phone allowed on the floor.",
            "Debrief: whose strategy won, and would it win again on a different maze?",
        ],
        "program": "V 5\nP 20\nR 350\nI 5\nL 350\nZ\nI 5\nL 350\nZ\nI 5\nL 350\nZ\nF 900\nQ",
        "program_note": "The right-hand rule. Turn right, then check up to three times whether a wall is in the way and turn back left if so, then drive forward. Repeat. This escapes any maze whose walls are all connected &mdash; slowly, but it always gets out.",
        "challenges": [
            "Hybrid: blast down the first known straight, then switch to wall following. Where do you put the switch?",
            "Add a finish detector so the robot stops when it sees the exit line instead of driving on.",
            "Prove the right-hand rule cannot work on a maze with a free-standing island in the middle. Draw the counter-example.",
        ],
        "watchfor": [
            ("The dead-reckoning run drifts more each room", "That is accumulated error. It is not fixable by trying harder &mdash; it is why sensors exist."),
            ("Wall follower loops forever in a circle", "Check the maze actually has an exit reachable from the start. A sealed maze defeats a correct algorithm."),
        ],
        "teacher": "Insist every team can state their strategy in one sentence before racing. The team that says 'we tuned the numbers until it worked' has learned less than the team that says 'we follow the right wall and accept it is slow', and the maze rearrangement will prove it in public.",
    },
    {
        "slug": "11-speed-and-traction",
        "title": "Getting Off the Line",
        "subtitle": "Acceleration, slip, and why fast starts are slow",
        "minutes": 60,
        "goal": "Find the fastest launch your robot can make without slipping, and prove it is faster overall than a harder launch.",
        "concepts": [
            "Traction is a budget: spend it all on acceleration and none is left for steering",
            "A wheel that slips is doing no useful work",
            "Fastest to full speed is not the same as fastest to the far wall",
            "Acceleration curves: gentle at the start, brisk once rolling",
        ],
        "sensethink": "DO, examined closely. Same command, different ramp, different outcome.",
        "steps": [
            "Open <b>Calibrate</b> &rarr; Step 0. Set the seed slider low and watch a launch. The robot should creep, then build.",
            "Turn the seed up until you can hear a chirp or see the nose twitch sideways at the start. That is slip.",
            "Back off one notch. Write down the two numbers.",
            "Race two settings over three metres: the slipping launch versus the clean one. Time both.",
            "Explain the result. Most groups find the clean launch wins or ties &mdash; the slipping robot spent its first moments going nowhere and pointing wrong.",
        ],
        "program": "V 9\nF 3000\nW 500\nB 3000",
        "program_note": "Full speed to the far end and back. Run it at a gentle ramp and at a harsh one, and time both round trips.",
        "challenges": [
            "Does the best setting change on carpet? Predict first, then measure.",
            "Add a passenger (a book taped on top). Does the extra weight help traction or hurt acceleration?",
            "Time a straight sprint and a slalom with the same settings. Does the best launch differ for the two?",
        ],
        "watchfor": [
            ("The robot lurches sideways at the start", "One wheel gripped before the other. Lower the seed until both wheels start together."),
            ("It takes forever to reach speed", "Growth is too low. The seed controls the first push; growth controls how quickly it builds after."),
        ],
        "teacher": "The counter-intuitive result is the point: the aggressive setting feels faster and loses. This is the same trade-off as a dragster wheelie or a sprinter's start, and kids who have raced anything will recognise it immediately.",
    },
    {
        "slug": "12-when-sensors-lie",
        "title": "When Sensors Lie",
        "subtitle": "Noise, blind spots, and trusting the wrong number",
        "minutes": 60,
        "goal": "Make each sensor give a confidently wrong answer, and explain to somebody else why it happened.",
        "concepts": [
            "Sensors do not measure what you think &mdash; they measure a proxy for it",
            "Sonar measures echo time, not distance to the thing you care about",
            "Line sensors measure reflection, not colour",
            "A robot cannot tell a wrong reading from a right one; only you can",
        ],
        "sensethink": "SENSE, interrogated. Every later lesson relies on sensors, so knowing their failure modes is knowing their limits.",
        "steps": [
            "Sonar versus a soft jumper: hold cloth in front. The reading jumps or reads clear. Sound is absorbed, not reflected.",
            "Sonar versus an angled wall: turn a book to 45&deg;. The echo bounces away and never comes home.",
            "Sonar versus a table edge: drive along a bench so the beam clears it. The robot is blind to something obviously there.",
            "Line sensors versus a dark floor: contrast, not colour, is what they see.",
            "Proximity versus sunlight: take it near a window. Infrared from the sun swamps the sensor.",
            "For each one, write the sentence: 'It reported X because it actually measures Y.'",
        ],
        "program": "V 4\nP 15\nI 6\nF 300\nZ\nI 5\nW 200\nZ\nQ",
        "program_note": "Creep forward only while the way ahead is clear. Run it toward a soft jumper and watch it drive straight into something it says is not there.",
        "challenges": [
            "Build an obstacle course the robot's sonar cannot see at all. How would you fix the robot &mdash; better sensor, or more of them?",
            "Two sensors disagree. Write a rule for which to believe.",
            "Where would a self-driving car have these same blind spots?",
        ],
        "watchfor": [
            ("Readings jump around wildly near soft things", "Weak echoes arrive late or not at all. This is the sensor's honest limit, not a fault."),
            ("Everything reads 400 cm", "No echoes at all. Check NOECHO in Read sensors &mdash; that means the sensor never answered, which is different from seeing nothing."),
        ],
        "teacher": "This lesson prevents a whole class of frustration later: when a maze run fails, the first question becomes 'did the sensor see it?' rather than 'is the code wrong?'. That is real debugging instinct.",
    },
    {
        "slug": "13-two-robots-one-room",
        "title": "Two Robots, One Room",
        "subtitle": "Sharing space, control, and the airwaves",
        "minutes": 60,
        "goal": "Run two robots at once without either one interfering with the other, and explain what could go wrong.",
        "concepts": [
            "Every robot has its own name, and names matter when there are twelve",
            "IR is a broadcast: one remote can hit several robots",
            "Bluetooth is a conversation: one phone, one robot",
            "Shared space needs rules, whether the drivers are robots or people",
        ],
        "sensethink": "The same loop, twice, in one room &mdash; and now the environment includes another robot.",
        "steps": [
            "Two robots, one IR remote. Press 5. Watch both react. Explain why.",
            "Now connect each robot to its own phone by name. Notice you must pick the right name.",
            "Give them opposite modes: one avoiding, one seeking. Watch them interact.",
            "Set them nose-to-nose in follow mode. What happens, and why is it unstable?",
            "Agree a room rule for the maze race: how do teams avoid running two robots into each other?",
        ],
        "program": None,
        "program_note": None,
        "challenges": [
            "Make two robots follow each other in a line without colliding.",
            "Can you make one robot deliberately confuse another's sonar? Should you?",
            "If twelve robots run at once, which of our systems would break first? Argue for one.",
        ],
        "watchfor": [
            ("Both robots react to one remote", "IR is a broadcast with no addressing. That is a property of the medium, not a bug."),
            ("A phone connects to the wrong robot", "Names come from the firmware. Check ROBOT_BLUETOOTH_NAME at the top of the kernel."),
        ],
        "teacher": "The nose-to-nose follow demo is worth doing deliberately: two followers create a feedback loop with no damping, and they oscillate. It is a genuinely surprising and memorable result.",
    },
    {
        "slug": "14-design-your-own-behaviour",
        "title": "Design Your Own Behaviour",
        "subtitle": "From an idea to a program the robot can run",
        "minutes": 60,
        "goal": "Invent a behaviour nobody has demonstrated, write it, test it, and describe how it fails.",
        "concepts": [
            "A behaviour is a rule you can state in one sentence before you write it",
            "Write the sentence first, then the blocks",
            "Every behaviour has failure modes; finding yours is finishing the work",
            "Testing means trying to break it, not proving it works",
        ],
        "sensethink": "You design the whole loop this time.",
        "steps": [
            "Pitch: each group writes their behaviour in one sentence on paper before touching a robot.",
            "Sketch the sense-think-do loop: what does it check, what does it decide, what does it do?",
            "Build it in blocks. Keep it under fifteen blocks &mdash; short programs fail in ways you can see.",
            "Test it. Then try to break it on purpose and write down what broke.",
            "Demo to the group: the sentence, the demo, and the failure. All three.",
        ],
        "program": "V 5\nP 12\nI 3\nR 250\nZ\nI 8\nL 250\nZ\nF 400\nQ",
        "program_note": "A starting point: a shy robot that steers away from whichever side something is near, and edges forward when both sides are clear. Take it and make it yours.",
        "challenges": [
            "A robot that finds the darkest corner in the room and parks there.",
            "A guard robot that stays still until something comes close, then chases it.",
            "A robot that traces the edge of a table without falling off &mdash; which sensor tells you where the edge is?",
        ],
        "watchfor": [
            ("The behaviour works once, then does something odd", "Programs run steps in order without re-checking. A loop makes it keep checking."),
            ("Nothing happens at all", "Check the sensor really triggers, using Read sensors, before blaming the program."),
        ],
        "teacher": "Insisting on the sentence before the blocks is the whole lesson. Groups that start by dragging blocks build something that half-works and cannot say what it is; groups that write 'it turns away from whichever side is blocked' debug in minutes.",
    },
    {
        "slug": "15-show-and-tell",
        "title": "Show &amp; Tell",
        "subtitle": "Explaining your robot to somebody who has never seen it",
        "minutes": 60,
        "goal": "Explain what your robot does, how it knows, and what still beats it &mdash; to an audience that was not in the club.",
        "concepts": [
            "If you can explain it, you understand it",
            "The interesting part of engineering is the trade-off, not the success",
            "Demos fail; a good presenter has a plan for that",
            "Share: this is the third word of Make. Learn. Share.",
        ],
        "sensethink": "Everything, narrated by the people who built it.",
        "steps": [
            "Plan the demo: one behaviour, ninety seconds, one clear thing to point at.",
            "Prepare the sentence: 'It senses ___, decides ___, does ___.'",
            "Prepare the honest bit: 'It fails when ___, because ___.'",
            "Rehearse a failure: what will you say if the robot does nothing on the day?",
            "Present to families, another class, or each other. Take questions.",
        ],
        "program": None,
        "program_note": None,
        "challenges": [
            "Explain your robot to somebody younger than you without using the word 'code'.",
            "Answer honestly: which parts did the robot decide, and which did you decide in advance?",
            "What would you build next if the club ran another ten weeks?",
        ],
        "watchfor": [
            ("The robot misbehaves in front of an audience", "Fresh batteries, calibration re-run on the actual demo surface, and a rehearsed sentence about why demos are hard."),
            ("A group cannot say what their robot does", "Send them back to the one-sentence rule from lesson 14. It is never too late to write it."),
        ],
        "teacher": "Invite people. The difference between a club activity and a piece of real work is an audience, and the questions from a parent who has never seen the robot are exactly the questions that reveal understanding.",
    },
]

PAGE_CSS = "../css/site.css"

def nav(active):
    items = [("../2wd.html", "2WD home"), ("index.html", "Lessons"),
             ("../workshop.html", "Workshop"), ("../MazeLab.html", "Maze Lab"),
             ("../cheatsheet.html", "Cheat sheet"), ("../index.html", "Mecanum robot")]
    parts = []
    for href, label in items:
        cls = ' class="active"' if label == active else ""
        parts.append('<a href="%s"%s>%s</a>' % (href, cls, label))
    return '<nav class="site-nav">' + "".join(parts) + "</nav>"


def lesson_page(index, lesson):
    previous_link = ""
    next_link = ""
    if index > 0:
        previous_link = '<a class="page-turn" href="%s.html">&larr; %s</a>' % (
            LESSONS[index - 1]["slug"], LESSONS[index - 1]["title"])
    if index < len(LESSONS) - 1:
        next_link = '<a class="page-turn" href="%s.html">%s &rarr;</a>' % (
            LESSONS[index + 1]["slug"], LESSONS[index + 1]["title"])

    program_section = ""
    if lesson["program"]:
        program_section = """
  <section class="card">
    <h2>Sample program</h2>
    <p>%s</p>
    <pre class="program" id="samplePlain">%s</pre>
    <button class="copy-button" data-copy="samplePlain">Copy to clipboard</button>
    <p class="hint">Paste it into the Workshop's <b>Robot language</b> box, then press
    <b>Text &rarr; blocks</b> to see it as blocks &mdash; or just send it straight to the robot.</p>
  </section>""" % (lesson["program_note"], html_escape.escape(lesson["program"]))

    watch_rows = "".join(
        "<tr><td>%s</td><td>%s</td></tr>" % (symptom, meaning)
        for symptom, meaning in lesson["watchfor"])

    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>%(number)02d. %(title)s &mdash; Maker Lab Kids</title>
<link rel="stylesheet" href="%(css)s">
</head>
<body>
<header class="site-header">
  %(lockup)s
  <div class="wordmark">Robot Workshop<small>%(tagline)s</small></div>
  %(nav)s
</header>

<main class="page">
  <p class="eyebrow">Lesson %(number)02d &middot; %(minutes)d minutes</p>
  <h1>%(title)s</h1>
  <p class="subtitle">%(subtitle)s</p>

  <section class="card goal">
    <h2>Today's goal</h2>
    <p>%(goal)s</p>
  </section>

  <section class="card">
    <h2>Ideas in this lesson</h2>
    <ul>%(concepts)s</ul>
  </section>

  <section class="card sense">
    <h2>Sense &rarr; Think &rarr; Do</h2>
    <p>%(sensethink)s</p>
  </section>

  <section class="card">
    <h2>How the session runs</h2>
    <ol>%(steps)s</ol>
  </section>
%(program)s
  <section class="card">
    <h2>Try this</h2>
    <ul>%(challenges)s</ul>
  </section>

  <section class="card">
    <h2>When it goes wrong</h2>
    <table class="watch">
      <tr><th>What you see</th><th>What it means</th></tr>
      %(watch)s
    </table>
  </section>

  <details class="card teacher">
    <summary>Notes for the grown-up</summary>
    <p>%(teacher)s</p>
  </details>

  <div class="page-turns">%(previous)s%(next)s</div>
</main>

<script src="../js/lesson-page.js"></script>
</body>
</html>
""" % {"lockup": lockup("../"), 
        "number": index + 1,
        "title": lesson["title"],
        "subtitle": lesson["subtitle"],
        "minutes": lesson["minutes"],
        "goal": lesson["goal"],
        "css": PAGE_CSS,
        "tagline": TAGLINE,
        "nav": nav("Lessons"),
        "concepts": "".join("<li>%s</li>" % c for c in lesson["concepts"]),
        "sensethink": lesson["sensethink"],
        "steps": "".join("<li>%s</li>" % s for s in lesson["steps"]),
        "program": program_section,
        "challenges": "".join("<li>%s</li>" % c for c in lesson["challenges"]),
        "watch": watch_rows,
        "teacher": lesson["teacher"],
        "previous": previous_link,
        "next": next_link,
    }


def lessons_index():
    cards = []
    for index, lesson in enumerate(LESSONS):
        cards.append("""
    <a class="lesson-card" href="%s.html">
      <span class="lesson-number">%02d</span>
      <span class="lesson-title">%s</span>
      <span class="lesson-sub">%s</span>
      <span class="lesson-goal">%s</span>
    </a>""" % (lesson["slug"], index + 1, lesson["title"], lesson["subtitle"], lesson["goal"]))

    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Lessons &mdash; Maker Lab Kids Robot Workshop</title>
<link rel="stylesheet" href="%(css)s">
</head>
<body>
<header class="site-header">
  %(lockup)s
  <div class="wordmark">Robot Workshop<small>%(tagline)s</small></div>
  %(nav)s
</header>

<main class="page">
  <h1>Lessons</h1>
  <p class="subtitle">%(count)d sessions, an hour each. Every one adds a piece of the
  robot's thinking &mdash; by the last lesson the robot solves a maze nobody
  has seen before.</p>
  <div class="lesson-grid">%(cards)s</div>

  <section class="card">
    <h2>Also in here</h2>
    <ul>
      <li><a href="../workshop.html">The Workshop app</a> &mdash; drive, read sensors, build block programs, calibrate.</li>
      <li><a href="../MazeLab.html">The Maze Lab</a> &mdash; six built-in maze lessons you can run with no robot at all.</li>
      <li><a href="../cheatsheet.html">The cheat sheet</a> &mdash; every command on one printable page.</li>
    </ul>
  </section>
</main>
</body>
</html>
""" % {"lockup": lockup("../"), "css": PAGE_CSS, "tagline": TAGLINE, "nav": nav("Lessons"),
       "cards": "".join(cards), "count": len(LESSONS)}


def site_hub():
    lesson_links = "".join(
        '<li><a href="lessons/%s.html">%02d. %s</a></li>' % (l["slug"], i + 1, l["title"])
        for i, l in enumerate(LESSONS))
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>2WD Robot &middot; %(site)s</title>
<link rel="stylesheet" href="./css/site.css">
</head>
<body>
<header class="site-header">
  %(lockup)s
  <div class="wordmark">Robot Workshop<small>%(tagline)s</small></div>
  <nav class="site-nav"><a href="2wd.html" class="active">2WD home</a><a href="lessons/index.html">Lessons</a><a href="workshop.html">Workshop</a><a href="MazeLab.html">Maze Lab</a><a href="cheatsheet.html">Cheat sheet</a><a href="index.html">Mecanum robot</a></nav>
</header>

<main class="page">
  <h1>The 2WD robot</h1>
  <p class="subtitle">The club's first robot: two wheels, the TB6612 shield, and
  block programs. It all works from a phone, and it all works with no internet
  once the page has loaded. The club's main robot is now the
  <a href="index.html">4WD mecanum robot</a>.</p>

  <div class="tile-grid">
    <a class="tile tile-primary" href="workshop.html">
      <span class="tile-title">Workshop</span>
      <span class="tile-note">Connect to your robot. Drive it, watch its sensors,
      build block programs, and calibrate it.</span>
      <span class="tile-go">Open &rarr;</span>
    </a>
    <a class="tile" href="lessons/index.html">
      <span class="tile-title">Lessons</span>
      <span class="tile-note">%(count)d club sessions with goals, sample programs, and
      what to do when it goes wrong.</span>
      <span class="tile-go">Browse &rarr;</span>
    </a>
    <a class="tile" href="MazeLab.html">
      <span class="tile-title">Maze Lab</span>
      <span class="tile-note">A maze simulator with six built-in lessons. No robot
      required &mdash; copy the program out when it works.</span>
      <span class="tile-go">Experiment &rarr;</span>
    </a>
  </div>

  <section class="card">
    <h2>All %(count)d lessons</h2>
    <ol class="plain-list">%(lessons)s</ol>
  </section>

  <section class="card">
    <h2>Before club night</h2>
    <p><a href="setup.html">Setting up a robot</a> &mdash; naming it, flashing it,
    checking every sensor, and calibrating it, in the order that avoids surprises.</p>
    <p><a href="cheatsheet.html">The cheat sheet</a> &mdash; every command the robot
    understands, on one page meant to be printed and taped to the table.</p>
  </section>

</main>
</body>
</html>
""" % {"lockup": lockup(), "site": SITE_TITLE, "tagline": TAGLINE, "lessons": lesson_links,
       "count": len(LESSONS)}


def cheatsheet():
    """A one-page reference meant to be printed and taped to the table."""
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Robot Cheat Sheet &mdash; Maker Lab Kids</title>
<link rel="stylesheet" href="./css/site.css">
</head>
<body>
<header class="site-header no-print">
  %(lockup)s
  <div class="wordmark">Robot Workshop<small>%(tagline)s</small></div>
  <nav class="site-nav">
    <a href="2wd.html">2WD home</a><a href="lessons/index.html">Lessons</a>
    <a href="workshop.html">Workshop</a><a href="MazeLab.html">Maze Lab</a>
    <a href="cheatsheet.html" class="active">Cheat sheet</a><a href="index.html">Mecanum robot</a>
  </nav>
</header>

<main class="page sheet">
  <h1 class="sheet-title">Robot Cheat Sheet</h1>
  <p class="subtitle no-print">Print this and tape it to the table. Everything the
  robot understands is on this page.</p>
  <button class="copy-button no-print" onclick="window.print()">Print this page</button>

  <div class="sheet-columns">

    <section class="card">
      <h2>IR remote</h2>
      <p class="sheet-note">Works the moment the robot is switched on. No phone needed.</p>
      <table class="watch">
        <tr><td>&#9650; &#9660; &#9664; &#9654;</td><td>drive while held</td></tr>
        <tr><td>OK</td><td>STOP &mdash; always works</td></tr>
        <tr><td>1 / 2 / 3</td><td>run stored program 1 / 2 / 3</td></tr>
        <tr><td>4</td><td>line tracking</td></tr>
        <tr><td>5</td><td>obstacle avoidance</td></tr>
        <tr><td>6</td><td>follow</td></tr>
        <tr><td>7</td><td>light seek</td></tr>
        <tr><td>8</td><td>seek (radar lock)</td></tr>
        <tr><td>9</td><td>speed up a notch (wraps)</td></tr>
        <tr><td>0</td><td>let go of control</td></tr>
      </table>
    </section>

    <section class="card">
      <h2>Program language</h2>
      <p class="sheet-note">One instruction per line. Upper or lower case, both fine.</p>
      <table class="watch">
        <tr><td>F 900</td><td>forward for 900 ms</td></tr>
        <tr><td>B 900</td><td>backward</td></tr>
        <tr><td>L 350</td><td>spin left</td></tr>
        <tr><td>R 350</td><td>spin right</td></tr>
        <tr><td>W 500</td><td>wait, wheels stopped</td></tr>
        <tr><td>V 5</td><td>speed 0&ndash;9</td></tr>
        <tr><td>E 90</td><td>point sonar, 0&ndash;180&deg;</td></tr>
        <tr><td>P 4 &hellip; Q</td><td>repeat 4 times</td></tr>
        <tr><td>I 5 &hellip; Z</td><td>if sensor 5 is true</td></tr>
        <tr><td>X</td><td>end the program</td></tr>
      </table>
    </section>

    <section class="card">
      <h2>Sensor numbers for I</h2>
      <table class="watch">
        <tr><td>1</td><td>left line sees black</td></tr>
        <tr><td>2</td><td>right line sees black</td></tr>
        <tr><td>3</td><td>something close on the left</td></tr>
        <tr><td>4</td><td>something close on the right</td></tr>
        <tr><td>5</td><td>wall closer than 25 cm</td></tr>
        <tr><td>6</td><td>the way ahead is clear</td></tr>
        <tr><td>7</td><td>the right side is clear</td></tr>
        <tr><td>8</td><td>the left side is clear</td></tr>
      </table>
      <p class="sheet-note">6, 7 and 8 are the opposites of 5, 4 and 3. Pair them to
      get an if/else.</p>
    </section>

    <section class="card">
      <h2>Typed commands</h2>
      <p class="sheet-note">Same over USB or Bluetooth.</p>
      <table class="watch">
        <tr><td>f b l r s</td><td>drive / stop</td></tr>
        <tr><td>0&ndash;9</td><td>speed (0 is slowest, NOT release)</td></tr>
        <tr><td>t a w p y</td><td>line / avoid / follow / light / seek</td></tr>
        <tr><td>d</td><td>read all sensors once</td></tr>
        <tr><td>h1 / h0</td><td>live sensor stream on / off</td></tr>
        <tr><td>*1 *2 *3</td><td>pick program slot</td></tr>
        <tr><td>[ &hellip; ]</td><td>save a program into the slot</td></tr>
        <tr><td>g / k</td><td>run / list the slot</td></tr>
        <tr><td>z z</td><td>set servo centre (twice within 6 s)</td></tr>
        <tr><td>j&lt;n&gt;</td><td>straight-drive trim, &minus;15&hellip;15</td></tr>
      </table>
    </section>

    <section class="card">
      <h2>When something is wrong</h2>
      <table class="watch">
        <tr><td>No servo wiggle at power-on</td><td>servo not connected</td></tr>
        <tr><td>Distance always &ldquo;clear&rdquo;</td><td>press <b>d</b>: NOECHO climbing means the sonar never answered</td></tr>
        <tr><td>SAVED steps= is too small</td><td>part of the upload was lost; send it again</td></tr>
        <tr><td>Robot stops by itself</td><td>manual driving expires after 0.4 s; hold, do not tap</td></tr>
        <tr><td>Flashing red banner</td><td>battery cutoff &mdash; charge before anything else</td></tr>
        <tr><td>Turns are wrong after a change</td><td>re-run calibration; the numbers moved</td></tr>
      </table>
    </section>

    <section class="card">
      <h2>Healthy numbers</h2>
      <table class="watch">
        <tr><td>5V rail</td><td>4.90&ndash;5.10 V</td></tr>
        <tr><td>Battery pack (2 cells)</td><td>8.4 V full, 7.0 V get ready, 6.6 V cutoff</td></tr>
        <tr><td>One room</td><td>30.5 cm (12 inches)</td></tr>
        <tr><td>Program slots</td><td>3, up to 100 steps each</td></tr>
      </table>
    </section>

  </div>
</main>
</body>
</html>
""" % {"lockup": lockup(), "tagline": TAGLINE}

def setup_page():
    """Box-to-driving in one page. The thing a helper reads on club night."""
    return """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Set Up a Robot &mdash; Maker Lab Kids</title>
<link rel="stylesheet" href="./css/site.css">
</head>
<body>
<header class="site-header no-print">
  %(lockup)s
  <div class="wordmark">Robot Workshop<small>%(tagline)s</small></div>
  <nav class="site-nav">
    <a href="2wd.html">2WD home</a><a href="lessons/index.html">Lessons</a>
    <a href="workshop.html">Workshop</a><a href="MazeLab.html">Maze Lab</a>
    <a href="cheatsheet.html">Cheat sheet</a><a href="index.html">Mecanum robot</a>
  </nav>
</header>

<main class="page">
  <h1>Set up a robot</h1>
  <p class="subtitle">Box to driving. Do this once per robot, before the club
  starts &mdash; not during it.</p>

  <section class="card">
    <h2>1. Give it a name</h2>
    <p>Open <code>kernel/robot_kernel.ino</code> in the Arduino IDE and change one line
    near the top:</p>
    <pre class="program">#define ROBOT_BLUETOOTH_NAME "Aqua Kitten"</pre>
    <p>Every robot needs a different name, or twelve identical entries appear in the
    Bluetooth list and nobody can find theirs. Let the kids choose; it is usually
    their first ever code edit.</p>
  </section>

  <section class="card">
    <h2>2. Flash it</h2>
    <ul>
      <li>Board: <b>Arduino Uno</b>. Port: the one that appears when you plug in.</li>
      <li>On Linux you may need to be in the <code>dialout</code> group &mdash; log out and
      back in after adding yourself, or the port stays invisible.</li>
      <li>Upload. The Bluetooth module lives on pins 8 and 11, not 0 and 1, so you do
      <em>not</em> need to unplug anything first.</li>
      <li>First boot after changing the name: power-cycle once so the module takes it.</li>
    </ul>
  </section>

  <section class="card">
    <h2>3. Watch it wake up</h2>
    <p>Switch it on and watch the sonar servo: it should wiggle once, left, right,
    centre. That wiggle is the robot telling you the servo is connected. No wiggle
    means it is plugged into the wrong header &mdash; fix that now, because half the
    later behaviours depend on it.</p>
  </section>

  <section class="card">
    <h2>4. Check the sensors before the kids do</h2>
    <p>Open the <a href="workshop.html">Workshop</a>, press <b>Connect</b>, pick the
    robot by name, then press <b>Read sensors</b>. You want:</p>
    <table class="watch">
      <tr><td>Distance</td><td>a sane number that changes when you move your hand</td></tr>
      <tr><td>NOECHO</td><td>0. A climbing number means the sonar never answered.</td></tr>
      <tr><td>Line dots</td><td>fill when you slide something black underneath</td></tr>
      <tr><td>Prox dots</td><td>fill when a hand is a few inches away</td></tr>
      <tr><td>Rail</td><td>4.90&ndash;5.10 V</td></tr>
    </table>
  </section>

  <section class="card">
    <h2>5. Calibrate, and write it on the robot</h2>
    <p>Run the <b>Calibrate</b> tab top to bottom on the surface the club will actually
    use. Save the robot under its name, then write the two numbers on a strip of tape
    and stick it to the chassis &mdash; the browser remembers them, but only on that one
    device, and club night is not the time to discover that.</p>
  </section>

  <section class="card">
    <h2>Serving these pages</h2>
    <p>The Workshop needs <code>https://</code> or <code>http://localhost</code>, because
    browsers only allow Bluetooth from a secure page. The lessons, the Maze Lab and the
    cheat sheet work from anywhere, including a USB stick with no internet at all.</p>
    <p>On Android Chrome, both Bluetooth <b>and</b> Location have to be switched on. This
    catches everybody once.</p>
  </section>
</main>
</body>
</html>
""" % {"lockup": lockup(), "tagline": TAGLINE}

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    lessons_directory = os.path.join(here, "lessons")
    if not os.path.isdir(lessons_directory):
        os.makedirs(lessons_directory)

    written = []
    for index, lesson in enumerate(LESSONS):
        target = os.path.join(lessons_directory, lesson["slug"] + ".html")
        with open(target, "w") as handle:
            handle.write(lesson_page(index, lesson))
        written.append(target)

    with open(os.path.join(lessons_directory, "index.html"), "w") as handle:
        handle.write(lessons_index())
    written.append("lessons/index.html")

    with open(os.path.join(here, "2wd.html"), "w") as handle:
        handle.write(site_hub())
    written.append("2wd.html")

    with open(os.path.join(here, "cheatsheet.html"), "w") as handle:
        handle.write(cheatsheet())
    written.append("cheatsheet.html")

    with open(os.path.join(here, "setup.html"), "w") as handle:
        handle.write(setup_page())
    written.append("setup.html")

    print("built %d files" % len(written))
    for path in written:
        print("  " + os.path.basename(path))


if __name__ == "__main__":
    main()
