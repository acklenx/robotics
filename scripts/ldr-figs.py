# Draws the light sensor build steps (resistor, LDR, Z fold, board, robot) and puts them
# between the <!-- ldr-steps --> markers in mecanum/light-build.html.
# Run: python3 scripts/ldr-figs.py
import os
import re
import subprocess

PITCH = 44            # px between connector centres
X1 = 110              # center of connector 1
XS = [X1, X1 + PITCH, X1 + 2 * PITCH]
HOUSING_TOP = 250
HOUSING_H = 110
HW = 38               # housing width
WIRES = [("#d42a2a", "1", "+5V", "red"), ("#161616", "2", "Gnd", "black"), ("#1f6fd1", "3", "A5", "blue")]
LEAD = "#9aa3ad"
BODY = "#e9d3a6"
BANDS = ["#e8c200", "#7b3fb8", "#d42a2a", None, "#b8962e"]   # yellow violet red, gap, gold = 4.7k


def connectors():
    out = []
    for x, (color, num, name, _) in zip(XS, WIRES):
        y0 = HOUSING_TOP + HOUSING_H
        out.append(f'<line x1="{x}" y1="{y0}" x2="{x}" y2="{y0 + 38}" stroke="{color}" stroke-width="13" stroke-linecap="round"/>')
        out.append(f'<rect x="{x - HW / 2}" y="{HOUSING_TOP}" width="{HW}" height="{HOUSING_H}" rx="3" fill="#202326" stroke="#4a5058" stroke-width="1.5"/>')
        out.append(f'<rect x="{x - HW / 2 + 4}" y="{HOUSING_TOP + 4}" width="{HW - 8}" height="10" rx="2" fill="#2e3237"/>')
        out.append(f'<rect x="{x - 5}" y="{HOUSING_TOP + 4}" width="10" height="10" fill="#050505" stroke="#6b737c" stroke-width="1"/>')
        out.append(f'<text x="{x}" y="{HOUSING_TOP + 70}" text-anchor="middle" font-size="22" font-weight="700" fill="#fff">{num}</text>')
        out.append(f'<text x="{x}" y="{y0 + 66}" text-anchor="middle" font-size="16" font-weight="700" fill="#222">{name}</text>')
    return "\n".join(out)


def bands(xr, top, reverse=False):
    out = [f'<rect x="{xr - 17}" y="{top}" width="34" height="96" rx="15" fill="{BODY}" stroke="#b89a62" stroke-width="1.5"/>']
    order = list(reversed(BANDS)) if reverse else BANDS
    y = top + 16
    for band in order:
        if band:
            out.append(f'<rect x="{xr - 17}" y="{y}" width="34" height="9" fill="{band}"/>')
        y += 15
    return out


def standing(drop):
    """The resistor standing on the connectors: the straight (short) lead over 3, the
    bent (long) lead over the top and down to 2. drop = 0 above, 95 pushed in."""
    xr = XS[2]
    top = 40 + drop
    bottom = top + 96
    straight_tip = bottom + 80
    bent_tip = bottom + 40
    out = [f'<path d="M{xr} {top} V{top - 14} Q{xr} {top - 30} {xr - 14} {top - 30} H{XS[1] + 14} Q{XS[1]} {top - 30} {XS[1]} {top - 16} V{bent_tip}" fill="none" stroke="{LEAD}" stroke-width="4" stroke-linecap="round"/>',
           f'<line x1="{xr}" y1="{bottom}" x2="{xr}" y2="{straight_tip}" stroke="{LEAD}" stroke-width="4" stroke-linecap="round"/>']
    return out + bands(xr, top), bent_tip, straight_tip


def folded():
    """Folded down over the front of connector 3: the body covers the 3, the bent lead
    runs from hole 2 down the front of 2 and round the bottom to the body."""
    x2, x3 = XS[1], XS[2]
    top = HOUSING_TOP + 14
    bottom = top + 96
    return [f'<path d="M{x2} {HOUSING_TOP - 4} V{bottom + 6} Q{x2} {bottom + 22} {x2 + 14} {bottom + 22} H{x3 - 14} Q{x3} {bottom + 22} {x3} {bottom + 6} V{bottom}" fill="none" stroke="{LEAD}" stroke-width="4" stroke-linecap="round"/>',
            f'<path d="M{x3} {HOUSING_TOP + 4} Q{x3} {HOUSING_TOP - 6} {x3} {top}" fill="none" stroke="{LEAD}" stroke-width="4" stroke-linecap="round"/>'] + bands(x3, top, reverse=True)


def ldr(drop):
    """A 5 mm LDR seen face on, legs spread to reach connectors 1 and 3.
    drop = 0 above the connectors, more = pushed in."""
    cx = XS[1]
    cy = 20 + drop
    r = 40
    base = cy + r - 6
    tip = base + 150
    out = []
    for side, x in ((-1, XS[0]), (1, XS[2])):
        out.append(f'<path d="M{cx + side * 20} {base} V{base + 14} Q{cx + side * 20} {base + 26} {cx + side * 32} {base + 30} L{x - side * 6} {base + 42} Q{x} {base + 46} {x} {base + 58} V{tip}" fill="none" stroke="{LEAD}" stroke-width="4" stroke-linecap="round"/>')
    out.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#f1e9d6" stroke="#b9a47a" stroke-width="2"/>')
    out.append(f'<path d="M{cx - 30} {cy - 18} A{r - 4} {r - 4} 0 0 1 {cx + 30} {cy - 18} Z" fill="#c9cdd2"/>')
    out.append(f'<path d="M{cx - 30} {cy + 18} A{r - 4} {r - 4} 0 0 0 {cx + 30} {cy + 18} Z" fill="#c9cdd2"/>')
    zig = f"M{cx - 26} {cy - 18}"
    for i in range(7):
        y = cy - 12 + i * 5
        zig += f" L{cx + (24 if i % 2 == 0 else -24)} {y}"
    out.append(f'<path d="{zig}" fill="none" stroke="#c0541a" stroke-width="3" stroke-linejoin="round"/>')
    return out, tip


def tape():
    x0 = XS[1] - HW / 2 - 5
    x1 = XS[2] + HW / 2 + 5
    out = [f'<rect x="{x0}" y="{HOUSING_TOP + 26}" width="{x1 - x0}" height="64" rx="3" fill="#cfd8e3" fill-opacity="0.72" stroke="#8b9aab" stroke-width="1.5"/>']
    for i in range(1, 6):
        xx = x0 + i * (x1 - x0) / 6
        out.append(f'<line x1="{xx}" y1="{HOUSING_TOP + 28}" x2="{xx - 8}" y2="{HOUSING_TOP + 88}" stroke="#ffffff" stroke-opacity="0.45" stroke-width="2"/>')
    return out, x1


SX = 170          # side view: center of the connectors
ST = 180          # side view: top of the connectors
SH = 110


def side_view(state):
    """The three connectors seen from the side: the resistor on the front (left), the
    LDR on the back (right). side-folded, side-taped, side-z, side-more-tape."""
    left, right = SX - HW / 2, SX + HW / 2
    bottom = ST + SH
    out = ['<rect x="0" width="360" height="470" rx="10" class="fig-bg" fill="#f4f1ea"/>']
    lead = lambda d: f'<path d="{d}" fill="none" stroke="{LEAD}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>'
    tape_band = lambda x, y, w, h: f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="#cfd8e3" fill-opacity="0.72" stroke="#8b9aab" stroke-width="1.5"/>'
    head = lambda x, y: f'<rect x="{x}" y="{y}" width="26" height="88" rx="8" fill="#f1e9d6" stroke="#b9a47a" stroke-width="2"/>'
    folded_back = state in ("side-z", "side-more-tape")
    tape_end = ST + 46                   # the LDR tape: wrapped around from above the end down to here
    fold_y = tape_end + 14               # the bottom of the Z, just under that tape
    ldr_out = right + 28                 # the folded-back legs, outside the tape
    # the LDR: legs up out of the connector, over the top, down the back
    if folded_back:
        out.append(lead(f"M{SX + 6} {ST + 6} V{ST - 12} Q{SX + 6} {ST - 24} {right + 4} {ST - 14} V{fold_y} Q{right + 4} {fold_y + 18} {right + 16} {fold_y + 18} Q{ldr_out} {fold_y + 18} {ldr_out} {fold_y} V{ST - 50}"))
        out.append(head(ldr_out - 13, ST - 50 - 88))
    else:
        out.append(lead(f"M{SX + 6} {ST + 6} V{ST - 12} Q{SX + 6} {ST - 24} {right + 4} {ST - 14} V{ST + 150}"))
        out.append(head(right + 4, ST + 150))
    # wires
    for i, (color, *_rest) in enumerate(WIRES):
        out.append(f'<line x1="{SX - 8 + i * 8}" y1="{bottom}" x2="{SX - 14 + i * 14}" y2="{bottom + 70}" stroke="{color}" stroke-width="10" stroke-linecap="round"/>')
    # the resistor's leads: the short one over the top edge to the body's top end, the
    # long one down the front and round the bottom to the body's lower end
    body_top, body_bottom = ST + 14, ST + 110
    out.append(lead(f"M{SX - 6} {ST + 6} V{ST - 8} Q{SX - 6} {ST - 18} {left - 10} {ST - 10} V{body_top}"))
    out.append(lead(f"M{SX - 2} {ST + 6} V{ST - 4} Q{SX - 2} {ST - 12} {left - 3} {ST - 4} V{body_bottom + 12} Q{left - 3} {body_bottom + 22} {left - 10} {body_bottom + 22} V{body_bottom}"))
    out.append(f'<rect x="{left}" y="{ST}" width="{HW}" height="{SH}" rx="3" fill="#202326" stroke="#4a5058" stroke-width="1.5"/>')
    out.append(f'<rect x="{left - 20}" y="{body_top}" width="20" height="96" rx="8" fill="{BODY}" stroke="#b89a62" stroke-width="1.5"/>')
    out.append(tape_band(left - 22, ST + 30, HW + 26, 64))                      # step 4: round 2 and 3
    if state != "side-folded":   # step 8: wrapped around like step 4, but high, sticking up past the end
        out.append(tape_band(left - 26, ST - 40, HW + 40, tape_end - (ST - 40)))
    if folded_back:
        out.append(lead(f"M{ldr_out} {fold_y} V{ST - 50}"))                     # the folded-back legs lie over the tape
    if state == "side-more-tape":
        out.append(tape_band(left - 30, ST - 6, HW + 26 + 12 + 30, 104))       # step 10: around everything, the Z fold and the end tape too
    out.append(text(left - 26, ST + 140, "front:", "end", 13, "#555"))
    out.append(text(left - 26, ST + 156, "resistor", "end", 13, "#555"))
    out.append(text(SX - 110, 62, "side view", "start", 13, "#555"))
    if state == "side-folded":
        out.append(text(right + 40, ST + 190, "LDR folded"))
        out.append(text(right + 40, ST + 208, "onto the back"))
    if state == "side-taped":
        out.append(text(right + 32, ST - 30, "tape around,"))
        out.append(text(right + 32, ST - 12, "past the end"))
    if state == "side-z":
        out.append(text(ldr_out + 22, ST - 110, "LDR back up,"))
        out.append(text(ldr_out + 22, ST - 92, "out of the top"))
        out.append(text(ldr_out + 12, fold_y + 20, "Z fold"))
    if state == "side-more-tape":
        out.append(text(ldr_out + 22, ST + 40, "more tape,"))
        out.append(text(ldr_out + 22, ST + 58, "over the Z"))
    labels = {
        "side-folded": "Step 7, side view: the LDR's legs come out of the top, bend over and run down the back of the connectors, with the LDR lying against the back, the opposite side from the taped resistor.",
        "side-taped": "Step 8, side view: tape wrapped around the connectors like in step 4, but high up, sticking out past the end and covering where the LDR's legs bend, holding them against the back.",
        "side-z": "Step 9, side view: the LDR folded back up over the tape so it sticks out of the top again. Its legs make a Z: up out of the connector, down the back under the tape, and back up outside it.",
        "side-more-tape": "Step 10, side view: more tape around everything, over the Z fold and the high tape from step 8, with the LDR still sticking out of the top.",
    }
    return f'<svg viewBox="30 0 320 440" role="img" aria-label="{labels[state]}">\n' + "\n".join(out) + "\n</svg>"


def done_view():
    """Front view, finished: tape around all three connectors, the LDR standing up out
    of the top (its legs come up from the Z fold behind)."""
    parts = ['<rect y="20" width="360" height="420" rx="10" class="fig-bg" fill="#f4f1ea"/>']
    cx, cy = XS[1], HOUSING_TOP - 120
    for x in (cx - 20, cx + 20):
        parts.append(f'<line x1="{x}" y1="{cy + 30}" x2="{x}" y2="{HOUSING_TOP + 40}" stroke="{LEAD}" stroke-width="4" stroke-linecap="round"/>')
    parts.append(connectors())
    parts += folded()
    x0, x1 = XS[0] - HW / 2 - 6, XS[2] + HW / 2 + 6
    parts.append(f'<rect x="{x0}" y="{HOUSING_TOP - 2}" width="{x1 - x0}" height="100" rx="3" fill="#cfd8e3" fill-opacity="0.8" stroke="#8b9aab" stroke-width="1.5"/>')
    parts.append(f'<circle cx="{cx}" cy="{cy}" r="40" fill="#f1e9d6" stroke="#b9a47a" stroke-width="2"/>')
    parts.append(f'<path d="M{cx - 30} {cy - 18} A36 36 0 0 1 {cx + 30} {cy - 18} Z" fill="#c9cdd2"/>')
    parts.append(f'<path d="M{cx - 30} {cy + 18} A36 36 0 0 0 {cx + 30} {cy + 18} Z" fill="#c9cdd2"/>')
    zig = f"M{cx - 26} {cy - 18}"
    for i in range(7):
        zig += f" L{cx + (24 if i % 2 == 0 else -24)} {cy - 12 + i * 5}"
    parts.append(f'<path d="{zig}" fill="none" stroke="#c0541a" stroke-width="3" stroke-linejoin="round"/>')
    parts.append(text(x1 + 10, HOUSING_TOP + 40, "taped"))
    parts.append(text(x1 + 10, HOUSING_TOP + 58, "all round"))
    parts.append(text(cx + 54, cy - 6, "the right"))
    parts.append(text(cx + 54, cy + 12, "sensor (A5)"))
    label = "Step 11, done: the right light sensor (A5). From the front, tape around all three connectors (1 red 5V, 2 black Gnd, 3 blue A5) and the LDR standing up out of the top."
    return f'<svg viewBox="0 20 360 420" role="img" aria-label="{label}">\n' + "\n".join(parts) + "\n</svg>"


def text(x, y, words, anchor="start", size=14, color="#222"):
    return f'<text x="{x}" y="{y}" text-anchor="{anchor}" font-size="{size}" fill="{color}">{words}</text>'



def board_overview():
    """The motor shield from above, simplified: where the analog header is. The right
    motor connector overhangs the header's top right corner, as on the real board."""
    out = ['<rect width="360" height="300" rx="10" class="fig-bg" fill="#f4f1ea"/>',
           '<rect x="20" y="20" width="320" height="240" rx="12" fill="#1f5fae" stroke="#174a88" stroke-width="2"/>']
    out.append('<rect x="20" y="70" width="34" height="62" rx="3" fill="#2f86d6" stroke="#174a88"/>')
    out.append('<rect x="20" y="150" width="34" height="62" rx="3" fill="#2f86d6" stroke="#174a88"/>')
    for x in (95, 160, 225):                 # the three chips
        out.append(f'<rect x="{x}" y="60" width="44" height="90" rx="3" fill="#222"/>')
    out.append('<rect x="70" y="200" width="56" height="40" rx="3" fill="#2f86d6" stroke="#174a88"/>')
    out.append('<text x="98" y="256" text-anchor="middle" font-size="11" fill="#fff">power in</text>')
    # the analog header, low and right, tucked under the right motor connector
    pitch = 13
    a5x, top = 300, 214                      # A5 column x, +5 row y
    hx = a5x - 5 * pitch
    out.append(f'<rect x="{hx - 8}" y="{top - 8}" width="{5 * pitch + 16}" height="{2 * pitch + 16}" rx="2" fill="#f2d33c"/>')
    for row in range(3):
        for col in range(6):
            out.append(f'<rect x="{hx + col * pitch - 3}" y="{top + row * pitch - 3}" width="6" height="6" fill="#6b5a12"/>')
    out.append(f'<rect x="{a5x - 14}" y="80" width="54" height="{top - 8 - 80}" rx="3" fill="#2f86d6" stroke="#174a88"/>')   # right motor connector
    out.append(f'<text x="{a5x + 12}" y="140" text-anchor="middle" font-size="11" fill="#fff">motors</text>')
    out.append(f'<rect x="{a5x - 7}" y="{top - 7}" width="14" height="{2 * pitch + 14}" rx="3" fill="none" stroke="#e65100" stroke-width="3"/>')
    out.append(f'<text x="{a5x + 12}" y="{top + 4}" font-size="10" fill="#fff">+5V</text>')
    out.append(f'<text x="{a5x + 12}" y="{top + pitch + 4}" font-size="10" fill="#fff">Gnd</text>')
    out.append(f'<text x="{a5x + 12}" y="{top + 2 * pitch + 4}" font-size="10" fill="#fff">A0-5</text>')
    out.append(f'<path d="M230 288 Q{a5x} 290 {a5x} {top + 2 * pitch + 12}" fill="none" stroke="#e65100" stroke-width="2.5"/>')
    out.append(f'<path d="M{a5x - 6} {top + 2 * pitch + 22} L{a5x} {top + 2 * pitch + 11} L{a5x + 6} {top + 2 * pitch + 22}" fill="none" stroke="#e65100" stroke-width="2.5"/>')
    out.append('<text x="224" y="292" text-anchor="end" font-size="14" fill="#222">the A5 column</text>')
    label = ("Step 11: the motor shield from above. The analog header is at the bottom right, tucked under the right motor connector: "
             "three rows, +5V on top, Gnd in the middle and A0-5 along the board's edge, with A0 on the left and A5 on the right. "
             "The A5 column, the one furthest right, is circled.")
    return f'<svg viewBox="0 0 360 300" role="img" aria-label="{label}">\n' + "\n".join(out) + "\n</svg>"


SENSOR_NAMES = {3: "back", 4: "left", 5: "right"}


def header_closeup(columns, hints=()):
    """Close-up of the analog header from above. Each dupont plug stands on its pin
    with the wire coming straight up at you: a colored dot on the plug."""
    xs = [30 + i * 46 for i in range(6)]          # A0 .. A5
    ys = [92, 138, 184]                           # +5, Gnd, A0-5
    out = ['<rect y="20" width="360" height="250" rx="10" class="fig-bg" fill="#f4f1ea"/>',
           '<rect x="0" y="20" width="360" height="206" fill="#1f5fae"/>',
           '<line x1="0" y1="226" x2="360" y2="226" stroke="#174a88" stroke-width="3"/>',
           f'<rect x="{xs[0] - 23}" y="{ys[0] - 23}" width="{xs[5] - xs[0] + 46}" height="{ys[2] - ys[0] + 46}" rx="3" fill="#f2d33c"/>']
    for y in ys:
        for x in xs:
            out.append(f'<rect x="{x - 7}" y="{y - 7}" width="14" height="14" fill="#6b5a12"/>')
    # the right motor connector, overhanging A5's +5 pin and just touching A4's
    out.append(f'<rect x="{xs[4] + 8}" y="20" width="{360 - xs[4] - 8}" height="{ys[0] - 20 - 21}" fill="#2f86d6" stroke="#174a88" stroke-width="2"/>')
    out.append(f'<text x="{(xs[4] + 368) / 2}" y="50" text-anchor="middle" font-size="13" fill="#fff">motor connector</text>')
    for col in columns:
        for (color, *_rest), y in zip(WIRES, ys):
            out.append(f'<rect x="{xs[col] - 20}" y="{y - 20}" width="40" height="40" rx="3" fill="#202326" stroke="#4a5058" stroke-width="1.5"/>')
            out.append(f'<circle cx="{xs[col]}" cy="{y}" r="11" fill="{color}" stroke="#fff" stroke-width="1.5"/>')
    for label, y in (("+5V", ys[0]), ("Gnd", ys[1]), ("A0-5", ys[2])):
        out.append(f'<text x="{xs[5] + 28}" y="{y + 5}" font-size="13" font-weight="700" fill="#fff">{label}</text>')
    for i, x in enumerate(xs):
        used = i in columns
        out.append(f'<text x="{x}" y="246" text-anchor="middle" font-size="14" font-weight="{700 if used else 400}" fill="{"#c24400" if used else "#555"}">A{i}</text>')
        if used or i in hints:
            out.append(f'<text x="{x}" y="263" text-anchor="middle" font-size="12" fill="#555">{SENSOR_NAMES[i]}</text>')
    if columns == [5]:
        label = ("Step 12: close-up of the analog header from above, columns A0 to A5 from left to right, rows +5V on top, Gnd in the middle, "
                 "A0-5 at the board's edge, with the motor connector right above the A5 column. The three wire ends sit on the A5 column: "
                 "+5V on +5V, Gnd on Gnd, A5 on A5.")
    else:
        label = ("Step 14: the same header with both sensors plugged in: the right sensor on the A5 column and the left sensor on A4, "
                 "each with +5V on top, Gnd in the middle and the signal at the edge. A3 is marked back.")
    return f'<svg viewBox="0 20 360 250" role="img" aria-label="{label}">\n' + "\n".join(out) + "\n</svg>"



def robot_view():
    """Side view of the robot's front right corner: the sensor taped on the front, the
    LDR pointing down at a spot on the floor 1 inch in front of the wheel; a small top
    view shows which corner gets which sensor."""
    out = ['<rect y="20" width="360" height="300" rx="10" class="fig-bg" fill="#f4f1ea"/>']
    floor = 290
    wx, wr = 120, 46                      # wheel center x, radius
    wy = floor - wr
    wheel_front = wx + wr
    inch = 40                             # px per inch at this scale (the wheel is about 2.4 in)
    spot = wheel_front + inch
    out.append(f'<line x1="20" y1="{floor}" x2="340" y2="{floor}" stroke="#8a8f96" stroke-width="2"/>')
    out.append(f'<circle cx="{wx}" cy="{wy}" r="{wr}" fill="#f2c200" stroke="#9a7a00" stroke-width="2"/>')
    out.append(f'<circle cx="{wx}" cy="{wy}" r="{wr - 12}" fill="#3a3d42"/>')
    for i in range(8):
        out.append(f'<line x1="{wx - wr + 6 + i * 11}" y1="{wy - 30}" x2="{wx - wr + 14 + i * 11}" y2="{wy + 30}" stroke="#55595f" stroke-width="3"/>')
    # chassis plate and the shield's wires going back
    plate_y = wy - wr - 8
    out.append(f'<rect x="40" y="{plate_y}" width="{wheel_front + 12 - 40}" height="10" rx="2" fill="#1d1f23"/>')
    # the sensor taped on the front edge, LDR end forward, the LDR bent down at the spot
    bx = wheel_front - 34
    out.append(f'<line x1="{bx}" y1="{plate_y - 7}" x2="60" y2="{plate_y - 40}" stroke="#555" stroke-width="7" stroke-linecap="round"/>')
    out.append(f'<rect x="{bx}" y="{plate_y - 14}" width="44" height="14" rx="2" fill="#202326"/>')
    out.append(f'<rect x="{bx + 6}" y="{plate_y - 18}" width="30" height="30" rx="3" fill="#cfd8e3" fill-opacity="0.8" stroke="#8b9aab"/>')
    lx, ly = bx + 58, plate_y - 2
    out.append(f'<path d="M{bx + 44} {plate_y - 7} Q{lx - 4} {plate_y - 7} {lx} {ly}" fill="none" stroke="{LEAD}" stroke-width="4"/>')
    # the LDR head, tilted to look down at the spot
    out.append(f'<g transform="rotate(62 {lx} {ly + 10})"><rect x="{lx - 13}" y="{ly}" width="26" height="22" rx="7" fill="#f1e9d6" stroke="#b9a47a" stroke-width="2"/></g>')
    out.append(f'<line x1="{lx + 4}" y1="{ly + 18}" x2="{spot}" y2="{floor - 4}" stroke="#e65100" stroke-width="2" stroke-dasharray="5 4"/>')
    out.append(f'<path d="M{spot - 7} {floor - 7} L{spot + 7} {floor + 7} M{spot + 7} {floor - 7} L{spot - 7} {floor + 7}" stroke="#e65100" stroke-width="3"/>')
    # 1 inch from the front of the wheel
    out.append(f'<line x1="{wheel_front}" y1="{floor + 2}" x2="{wheel_front}" y2="{floor + 16}" stroke="#222" stroke-width="1.5"/>')
    out.append(f'<line x1="{spot}" y1="{floor + 2}" x2="{spot}" y2="{floor + 16}" stroke="#222" stroke-width="1.5"/>')
    out.append(f'<line x1="{wheel_front}" y1="{floor + 10}" x2="{spot}" y2="{floor + 10}" stroke="#222" stroke-width="1.5"/>')
    out.append(text((wheel_front + spot) / 2, floor + 28, "1 inch", "middle", 13))
    out.append(text(spot + 12, floor - 26, "point it", size=13))
    out.append(text(spot + 12, floor - 10, "at this spot", size=13))
    out.append(text(40, 44, "side view, front to the right", size=12, color="#555"))
    # top view: which corner
    tx, ty, tw, th = 250, 50, 70, 96
    out.append(text(tx + tw / 2, ty - 8, "from above", "middle", 12, "#555"))
    out.append(f'<rect x="{tx}" y="{ty + 14}" width="{tw}" height="{th - 14}" rx="6" fill="#1d1f23"/>')
    for cx, cy in ((tx - 6, ty + 30), (tx + tw - 6, ty + 30), (tx - 6, ty + th - 22), (tx + tw - 6, ty + th - 22)):
        out.append(f'<rect x="{cx}" y="{cy - 14}" width="12" height="28" rx="3" fill="#f2c200"/>')
    out.append(f'<path d="M{tx + tw / 2 - 8} {ty + 40} L{tx + tw / 2} {ty + 26} L{tx + tw / 2 + 8} {ty + 40}" fill="none" stroke="#fff" stroke-width="2.5"/>')
    out.append(text(tx + tw / 2, ty + 58, "front", "middle", 11, "#fff"))
    out.append(f'<circle cx="{tx + tw - 8}" cy="{ty + 16}" r="7" fill="#e65100"/>')
    out.append(f'<circle cx="{tx + 8}" cy="{ty + 16}" r="7" fill="none" stroke="#e65100" stroke-width="2"/>')
    out.append(text(tx + tw + 4, ty + 4, "A5", size=12))
    out.append(text(tx - 4, ty + 4, "A4", "end", 12))
    label = ("Step 13: side view of the robot's front right corner. The sensor is taped to the front of the robot with the LDR bent down, "
             "pointing at a spot on the floor 1 inch in front of the wheel. A small view from above shows the A5 sensor on the front right corner "
             "and the A4 sensor's place on the front left.")
    return f'<svg viewBox="0 20 360 300" role="img" aria-label="{label}">\n' + "\n".join(out) + "\n</svg>"


def bot_top(cx, cy, angle, scale=0.5, ldrs=True):
    """Our mecanum bot from above, nose up before rotating by angle degrees: the same
    drawing as the Blocks page's simulator (drawRobotPicture in mecanum/blocks.html,
    in millimeters), plus the two light sensors on the front corners."""
    P = dict(track=156, wheelbase=128, wheel_d=64, wheel_w=35, plate_w=116, plate_l=222,
             yellow="#f6c21c", roller="#15181c", plate="#262b33", teal="#0d8a99", shield="#1b4fa6",
             terminal="#3d8ce6", sonar="#1f62c4", battery="#5a46b8", metal="#cdd2d8", nose="#ff5a3c")
    g = []
    box = lambda x, y, w, h, r, fill, extra="": g.append(
        f'<rect x="{x - w / 2:.2f}" y="{y - h / 2:.2f}" width="{w}" height="{h}" rx="{r}" fill="{fill}"{extra}/>')
    dot = lambda x, y, r, fill: g.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="{r}" fill="{fill}"/>')
    defs = []
    for n, (side, end) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
        wx, wy = side * P["track"] / 2, end * P["wheelbase"] / 2
        slope = side * end
        box(side * (P["plate_w"] / 2 + 4), wy, 14, 16, 2, "#e3ad12")
        box(wx, wy, P["wheel_w"], P["wheel_d"], 7, P["yellow"])
        clip = f"botwheel{n}"
        defs.append(f'<clipPath id="{clip}"><rect x="{wx - P["wheel_w"] / 2}" y="{wy - P["wheel_d"] / 2}" width="{P["wheel_w"]}" height="{P["wheel_d"]}" rx="7"/></clipPath>')
        rollers = "".join(f'<line x1="{wx - 8.5}" y1="{wy + i * 15.5 - slope * 8.5}" x2="{wx + 8.5}" y2="{wy + i * 15.5 + slope * 8.5}"/>' for i in range(-3, 4))
        g.append(f'<g clip-path="url(#{clip})" stroke="{P["roller"]}" stroke-width="7.4" stroke-linecap="round">{rollers}</g>')
        box(wx, wy, P["wheel_w"], P["wheel_d"], 7, "none", ' stroke="#7a5c00" stroke-width="1.4"')
    pl = P["plate_l"] / 2
    g.append(f'<path d="M0 {-pl - 13} L-13 {-pl + 2} L13 {-pl + 2} Z" fill="{P["nose"]}"/>')
    box(0, 0, P["plate_w"], P["plate_l"], 16, P["plate"], ' stroke="#0c0e11" stroke-width="1.6"')
    for x, y in ((-46, -92), (46, -92), (-46, 0), (46, 0), (-46, 92), (46, 92)):
        box(x, y, 5, 20, 2.5, "#40484f")
    box(0, 76, 84, 46, 4, "#0f1216")
    box(0, 65, 70, 17, 8, P["battery"]); box(0, 87, 70, 17, 8, P["battery"])
    box(-33, 65, 5, 9, 1, P["metal"]); box(33, 87, 5, 9, 1, P["metal"])
    by = -22
    box(0, by, 54, 70, 3, P["teal"])
    box(-13, by + 37, 13, 17, 1.5, P["metal"]); box(14, by + 36, 10, 14, 1.5, "#111417")
    sy = by - 4
    box(0, sy, 54, 60, 3, P["shield"], ' stroke="#0d2c63" stroke-width="1"')
    box(-24.2, sy, 3.4, 50, 1, "#0b0d10"); box(24.2, sy, 3.4, 50, 1, "#0b0d10")
    box(-9, sy - 4, 8.5, 21, 1, "#0b0d10"); box(9, sy - 4, 8.5, 21, 1, "#0b0d10")
    box(0, sy + 15, 19, 7.5, 1, "#0b0d10")
    dot(-9, sy - 12, 1.3, "#555d66"); dot(9, sy - 12, 1.3, "#555d66")
    for tx, ty in ((-11, sy - 26), (11, sy - 26), (-11, sy + 26), (11, sy + 26)):
        box(tx, ty, 19, 8.5, 1, P["terminal"])
        dot(tx - 4.5, ty, 2.3, P["metal"]); dot(tx + 4.5, ty, 2.3, P["metal"])
    dot(18, sy + 15, 3, P["metal"]); dot(18, sy + 15, 1.6, "#d3312b"); dot(-18, sy + 15, 1.8, "#63f58a")
    for n, color in enumerate(("#e33b2e", "#f6c21c", "#f4f6f8", "#2fa866")):
        x0 = -24.2 + (0 if n < 2 else 48.4)
        y0 = sy - 20 + (n % 2) * 6
        g.append(f'<path d="M{x0} {y0} Q{x0 * 1.7:.2f} -78 {-9 + n * 6} -88" fill="none" stroke="{color}" stroke-width="1.7" stroke-linecap="round"/>')
    box(0, -84, 25, 13, 2, "#2d6fd0")
    box(0, -100, 48, 22, 2.5, P["sonar"])
    for ex in (-12.5, 12.5):
        dot(ex, -100, 8.8, P["metal"]); dot(ex, -100, 6, "#2a2f36"); dot(ex, -100, 2, "#4a525c")
    box(0, -92.5, 8, 3, 1.5, P["metal"])
    if ldrs:   # our light sensors, on the front corners of the plate
        for x in (-46, 46):
            dot(x, -104, 9, "#f1e9d6")
            g.append(f'<circle cx="{x}" cy="-104" r="9" fill="none" stroke="#b9a47a" stroke-width="2"/>')
            g.append(f'<path d="M{x - 5} -108 L{x + 5} -106 L{x - 5} -104 L{x + 5} -102 L{x - 5} -100" fill="none" stroke="#c0541a" stroke-width="1.6"/>')
    return (f'<defs>{"".join(defs)}</defs><g transform="translate({cx} {cy}) rotate({angle}) scale({scale})">' + "".join(g) + "</g>")


def chase_view():
    """Step 15: a flashlight makes a small spot ahead and to the left of the robot, and
    the robot turns toward it (screen); print shows a QR code instead."""
    out = ['<rect y="20" width="360" height="300" rx="10" class="fig-bg" fill="#f4f1ea"/>']
    spot = (136, 92)
    # the flashlight, bottom left, and its narrow beam to a small spot
    fx, fy = 46, 276
    out.append(f'<path d="M{fx + 26} {fy - 30} L{spot[0] - 9} {spot[1] + 4} L{spot[0] + 9} {spot[1] + 10} L{fx + 40} {fy - 18} Z" fill="#ffd84d" fill-opacity="0.45"/>')
    out.append(f'<ellipse cx="{spot[0]}" cy="{spot[1]}" rx="17" ry="12" fill="#ffd84d" fill-opacity="0.7"/>')
    out.append(f'<ellipse cx="{spot[0]}" cy="{spot[1]}" rx="8" ry="6" fill="#fff6c4"/>')
    out.append(f'<g transform="translate({fx} {fy}) rotate(-50)"><rect x="-26" y="-11" width="40" height="22" rx="5" fill="#3b4049"/>'
               f'<path d="M14 -11 L26 -16 L26 16 L14 11 Z" fill="#555b66"/><rect x="-18" y="-15" width="9" height="5" rx="2" fill="#e65100"/></g>')
    # the robot, part way through turning left toward the spot
    out.append(bot_top(236, 186, -28, 0.56))
    out.append('<path d="M296 112 A 92 92 0 0 0 196 82" fill="none" stroke="#e65100" stroke-width="3" stroke-linecap="round"/>')
    out.append('<path d="M206 74 L194 82 L206 92" fill="none" stroke="#e65100" stroke-width="3" stroke-linecap="round" stroke-linejoin="round"/>')
    out.append(text(250, 50, "turns toward the light", "middle", 13))
    out.append(text(spot[0], spot[1] - 22, "light spot", "middle", 12, "#555"))
    label = ("Step 15: our mecanum robot seen from above, with yellow wheels, a blue motor shield and a light sensor on each front corner. "
             "A flashlight makes a small spot of light on the floor ahead and to the left, and the robot turns toward it.")
    return f'<svg viewBox="0 20 360 300" role="img" aria-label="{label}">\n' + "\n".join(out) + "\n</svg>"


def use_page_qr():
    """The QR code to How to use the Light Bot, made by scripts/qr-svg.js."""
    here = os.path.dirname(os.path.abspath(__file__))
    svg = subprocess.run(["node", os.path.join(here, "qr-svg.js"), "https://robotics.makerlabkids.com/mecanum/light"],
                         capture_output=True, text=True, check=True).stdout
    return '<div class="print-only step-qr">' + svg + '</div>'


def figure(state):
    if state.startswith("side"):
        return side_view(state)
    if state == "done":
        return done_view()
    if state == "board":
        return board_overview()
    if state == "header":
        return header_closeup([5])
    if state == "header-all":
        return header_closeup([4, 5], hints=(3,))
    if state == "robot":
        return robot_view()
    if state == "chase":
        return chase_view()
    y0 = {"folded": 190, "taped": 190, "ldr-ready": -40, "ldr-in": 40}.get(state, 0)      # folded and taped: just the connectors
    parts = [f'<rect y="{y0}" width="360" height="{440 - y0}" rx="10" class="fig-bg" fill="#f4f1ea"/>']
    if state in ("ready", "plugged"):
        res, bent_tip, straight_tip = standing(95 if state == "plugged" else 0)
        parts += res               # leads first: the housings hide the parts pushed inside
        parts.append(connectors())
    else:
        if state in ("ldr-ready", "ldr-in"):
            legs, ldr_tip = ldr(0 if state == "ldr-ready" else 71)
            parts += legs          # before the housings, which hide the legs pushed in
        parts.append(connectors())   # folded: the resistor lies on top of the plastic
        parts += folded()
        if state != "folded":
            tape_parts, tape_right = tape()
            parts += tape_parts
    if state == "ready":
        for x, tip in ((XS[1], bent_tip), (XS[2], straight_tip)):
            parts.append(f'<line x1="{x}" y1="{tip + 8}" x2="{x}" y2="{HOUSING_TOP - 8}" stroke="#e65100" stroke-width="2" stroke-dasharray="4 4"/>')
            parts.append(f'<path d="M{x - 6} {HOUSING_TOP - 14} L{x} {HOUSING_TOP - 4} L{x + 6} {HOUSING_TOP - 14}" fill="none" stroke="#e65100" stroke-width="2"/>')
        parts.append(text(XS[1] - 14, bent_tip - 40, "long lead", "end"))
        parts.append(text(XS[1] - 14, bent_tip - 22, "(bent over)", "end"))
        parts.append(text(XS[2] + 14, straight_tip - 20, "short lead"))
        parts.append(text(XS[2] + 26, 80, "4.7k"))
    if state in ("plugged", "folded", "taped"):
        parts.append(text(XS[0], HOUSING_TOP - 28, "empty", "middle", 13, "#555"))
        parts.append(text(XS[0], HOUSING_TOP - 12, "for now", "middle", 13, "#555"))
    if state == "folded":
        parts.append(text(XS[2] + 28, HOUSING_TOP + 50, "resistor"))
        parts.append(text(XS[2] + 28, HOUSING_TOP + 68, "folded"))
        parts.append(text(XS[2] + 28, HOUSING_TOP + 86, "down on 3"))
    if state == "taped":
        parts.append(text(tape_right + 10, HOUSING_TOP + 52, "tape around"))
        parts.append(text(tape_right + 10, HOUSING_TOP + 70, "2 and 3"))
    if state == "ldr-ready":
        for x in (XS[0], XS[2]):
            parts.append(f'<line x1="{x}" y1="{ldr_tip + 8}" x2="{x}" y2="{HOUSING_TOP - 8}" stroke="#e65100" stroke-width="2" stroke-dasharray="4 4"/>')
            parts.append(f'<path d="M{x - 6} {HOUSING_TOP - 14} L{x} {HOUSING_TOP - 4} L{x + 6} {HOUSING_TOP - 14}" fill="none" stroke="#e65100" stroke-width="2"/>')
        parts.append(text(XS[1] + 50, 10, "LDR"))
        parts.append(text(XS[1] + 50, 28, "(GL5537)", size=12, color="#555"))
        parts.append(text(XS[0] - 10, ldr_tip - 30, "to 1", "end"))
        parts.append(text(XS[2] + 10, ldr_tip - 30, "to 3"))
    if state == "ldr-in":
        parts.append(text(XS[2] + 26, 110, "standing well"))
        parts.append(text(XS[2] + 26, 128, "clear of the plastic"))
    labels = {
        "ready": "Step 1: a 4.7k resistor with one lead bent back so both leads point down, above three dupont connectors: 1 +5V, 2 Gnd, 3 A5. The long bent lead lines up with connector 2, the short straight lead with connector 3.",
        "plugged": "Step 2: the long bent lead in connector 2 (Gnd), the short straight lead in connector 3 (A5). Connector 1 (+5V) is still empty.",
        "folded": "Step 3: the resistor folded down flat on the plastic of connector 3, covering the 3. The bent lead runs down the front of connector 2.",
        "taped": "Step 4: tape wrapped round connectors 2 and 3, holding the resistor against them.",
        "ldr-ready": "Step 5: the LDR above the connectors, its two legs spread to line up with connector 1 (+5V) and connector 3 (A5), either side of the taped connector 2.",
        "ldr-in": "Step 6: the LDR's legs pushed into connectors 1 and 3, with the LDR still standing well clear of the connectors.",
    }
    return f'<svg viewBox="0 {y0} 360 {440 - y0}" role="img" aria-label="{labels[state]}">\n' + "\n".join(parts) + "\n</svg>"


html = f'''<!-- ldr-steps: drawn by scripts/ldr-figs.py -->
  <section class="build-steps">
    <h2>Step by step: the right sensor (A5)</h2>
    <p>Three 20 cm female-female dupont wires. Any colors will do: what matters is where each
    one goes. In the pictures, wire <b>1 is +5V</b>, wire <b>2 is Gnd</b> and wire <b>3 is A5</b>.</p>
    <div class="steps">
      <figure>
        <h3 class="step-title">1. Bend the resistor</h3>
        {figure("ready")}
        <figcaption>Bend one lead of the 4.7k resistor back over its body, so both
        leads point the same way. The bent lead is the long one.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">2. Plug in the resistor</h3>
        {figure("plugged")}
        <figcaption>Push the <b>long bent lead into 2</b> (Gnd) and the <b>short
        straight lead into 3</b> (A5).</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">3. Fold the resistor down</h3>
        {figure("folded")}
        <figcaption>Fold the resistor down flat onto the plastic of connector 3, so it
        covers the 3.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">4. Tape the resistor</h3>
        {figure("taped")}
        <figcaption>Tape connectors 2 and 3 together, with the resistor caught under
        the tape.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">5. Line up the LDR</h3>
        {figure("ldr-ready")}
        <figcaption>Spread the LDR's legs a little and line them up with <b>1</b> (+5V)
        and <b>3</b> (A5), either side of the taped Gnd connector.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">6. Plug in the LDR</h3>
        {figure("ldr-in")}
        <figcaption>Push the legs in, but leave the LDR standing well clear of the
        connectors.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">7. Fold the LDR back</h3>
        {figure("side-folded")}
        <figcaption>Fold the LDR down onto the <b>back</b> of the connectors, the
        opposite side from the resistor. <b>Keep its two legs apart</b>, spaced like in step 6:
        they must not touch, or even get close.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">8. Tape it high</h3>
        {figure("side-taped")}
        <figcaption>Wrap tape around the connectors like in step 4, but higher up, so it
        sticks out past the end and covers where the LDR's legs bend. Keeping it high means the bend
        uses up only a little of the legs.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">9. Make the Z fold</h3>
        {figure("side-z")}
        <figcaption>Fold the LDR back up over the tape, so it sticks out of the top
        again. The Z fold stops it pulling out.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">10. More tape</h3>
        {figure("side-more-tape")}
        <figcaption>More tape around everything, over the Z fold and the tape from
        step 8.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">11. Find the analog header</h3>
        {figure("board")}
        <figcaption>Find the analog header at the bottom right of the motor shield.
        Its rows are <b>+5V</b> on top, <b>Gnd</b> in the middle and <b>A0-5</b> along the edge.
        A0 is on the left, <b>A5 on the right</b>.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">12. Plug into A5</h3>
        {figure("header")}
        <figcaption>Push the ends of the three wires onto the <b>A5 column</b>, tight
        against the motor connector: <b>+5V</b> on top, <b>Gnd</b> in the middle, <b>A5</b> at the
        edge.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">13. Tape it to the robot</h3>
        {figure("robot")}
        <figcaption>Tape the sensor to the front of the robot: A5 on the right, A4 on
        the left. Point the LDR down at a spot on the floor <b>1 inch in front of the
        wheel</b>.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">14. Build the left sensor</h3>
        {figure("header-all")}
        <figcaption>Repeat steps 1 to 13 for the <b>left</b> sensor, and plug it into
        <b>A4</b>.</figcaption>
      </figure>
      <figure>
        <h3 class="step-title">15. Chase the light</h3>
        {figure("chase")}
        <figcaption>Get a flashlight and try it out: <a href="/mecanum/light">How to use the
        Light Bot</a>.</figcaption>
        {use_page_qr()}
      </figure>
    </div>
  </section>
<!-- /ldr-steps -->'''

page = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mecanum", "light-build.html")
s = open(page).read()
s = re.sub(r"<!-- ldr-steps[\s\S]*?<!-- /ldr-steps -->", lambda m: html, s)
open(page, "w").write(s)
print("ok")
