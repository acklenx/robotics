# Draws the light sensor build steps (resistor, LDR, Z fold, board) for the Light bot page and puts them
# between the <!-- ldr-steps --> markers in mecanum/light.html.
import re

PITCH = 44            # px between connector centres
X1 = 110              # centre of connector 1
XS = [X1, X1 + PITCH, X1 + 2 * PITCH]
HOUSING_TOP = 250
HOUSING_H = 110
HW = 38               # housing width
WIRES = [("#d42a2a", "1", "5V", "red"), ("#161616", "2", "Gnd", "black"), ("#1f6fd1", "3", "A5", "blue")]
LEAD = "#9aa3ad"
BODY = "#e9d3a6"
BANDS = ["#e8c200", "#7b3fb8", "#d42a2a", None, "#b8962e"]   # yellow violet red, gap, gold = 4.7k


def connectors():
    out = []
    for x, (colour, num, name, _) in zip(XS, WIRES):
        y0 = HOUSING_TOP + HOUSING_H
        out.append(f'<line x1="{x}" y1="{y0}" x2="{x}" y2="{y0 + 38}" stroke="{colour}" stroke-width="13" stroke-linecap="round"/>')
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


SX = 170          # side view: centre of the connectors
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
    tape2 = (ST + 4, 40)                 # the LDR tape: high, near the top end of the connector
    fold_y = tape2[0] + tape2[1] + 14    # the bottom of the Z, just under that tape
    ldr_out = right + 28                 # the folded-back legs, outside the tape
    # the LDR: legs up out of the connector, over the top, down the back
    if folded_back:
        out.append(lead(f"M{SX + 6} {ST + 6} V{ST - 12} Q{SX + 6} {ST - 24} {right + 4} {ST - 14} V{fold_y} Q{right + 4} {fold_y + 18} {right + 16} {fold_y + 18} Q{ldr_out} {fold_y + 18} {ldr_out} {fold_y} V{ST - 50}"))
        out.append(head(ldr_out - 13, ST - 50 - 88))
    else:
        out.append(lead(f"M{SX + 6} {ST + 6} V{ST - 12} Q{SX + 6} {ST - 24} {right + 4} {ST - 14} V{ST + 150}"))
        out.append(head(right + 4, ST + 150))
    # wires
    for i, (colour, *_rest) in enumerate(WIRES):
        out.append(f'<line x1="{SX - 8 + i * 8}" y1="{bottom}" x2="{SX - 14 + i * 14}" y2="{bottom + 70}" stroke="{colour}" stroke-width="10" stroke-linecap="round"/>')
    # the resistor's leads: the short one over the top edge to the body's top end, the
    # long one down the front and round the bottom to the body's lower end
    body_top, body_bottom = ST + 14, ST + 110
    out.append(lead(f"M{SX - 6} {ST + 6} V{ST - 8} Q{SX - 6} {ST - 18} {left - 10} {ST - 10} V{body_top}"))
    out.append(lead(f"M{SX - 2} {ST + 6} V{ST - 4} Q{SX - 2} {ST - 12} {left - 3} {ST - 4} V{body_bottom + 12} Q{left - 3} {body_bottom + 22} {left - 10} {body_bottom + 22} V{body_bottom}"))
    out.append(f'<rect x="{left}" y="{ST}" width="{HW}" height="{SH}" rx="3" fill="#202326" stroke="#4a5058" stroke-width="1.5"/>')
    out.append(f'<rect x="{left - 20}" y="{body_top}" width="20" height="96" rx="8" fill="{BODY}" stroke="#b89a62" stroke-width="1.5"/>')
    out.append(tape_band(left - 22, ST + 30, HW + 26, 64))                      # step 4: round 2 and 3
    if state != "side-folded":
        out.append(tape_band(left - 26, tape2[0], HW + 26 + 12, tape2[1]))     # step 8: over the LDR's legs
    if folded_back:
        out.append(lead(f"M{ldr_out} {fold_y} V{ST - 50}"))                     # the folded-back legs lie over the tape
    if state == "side-more-tape":
        out.append(tape_band(left - 30, ST - 2, HW + 26 + 12 + 30, 100))       # step 10: round everything, Z fold too
    out.append(text(left - 26, ST + 140, "front:", "end", 13, "#555"))
    out.append(text(left - 26, ST + 156, "resistor", "end", 13, "#555"))
    out.append(text(SX - 110, 62, "side view", "start", 13, "#555"))
    if state == "side-folded":
        out.append(text(right + 40, ST + 190, "LDR folded"))
        out.append(text(right + 40, ST + 208, "onto the back"))
    if state == "side-taped":
        out.append(text(right + 18, ST + 22, "tape over"))
        out.append(text(right + 18, ST + 40, "its legs"))
    if state == "side-z":
        out.append(text(ldr_out + 22, ST - 110, "LDR back up,"))
        out.append(text(ldr_out + 22, ST - 92, "out of the top"))
        out.append(text(ldr_out + 12, fold_y + 20, "Z fold"))
    if state == "side-more-tape":
        out.append(text(ldr_out + 22, ST + 40, "more tape,"))
        out.append(text(ldr_out + 22, ST + 58, "over the Z"))
    labels = {
        "side-folded": "Step 7, side view: the LDR's legs come out of the top, bend over and run down the back of the connectors, with the LDR lying against the back, the opposite side from the taped resistor.",
        "side-taped": "Step 8, side view: a second piece of tape, high up near the end of the connectors, holds the LDR's legs against the back.",
        "side-z": "Step 9, side view: the LDR folded back up over the tape so it sticks out of the top again. Its legs make a Z: up out of the connector, down the back under the tape, and back up outside it.",
        "side-more-tape": "Step 10, side view: more tape round everything, over the Z fold, with the LDR still sticking out of the top.",
    }
    return f'<svg viewBox="30 0 320 440" role="img" aria-label="{labels[state]}">\n' + "\n".join(out) + "\n</svg>"


def done_view():
    """Front view, finished: tape round all three connectors, the LDR standing up out
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
    label = "Step 11, done: the right light sensor (A5). From the front, tape round all three connectors (1 red 5V, 2 black Gnd, 3 blue A5) and the LDR standing up out of the top."
    return f'<svg viewBox="0 20 360 420" role="img" aria-label="{label}">\n' + "\n".join(parts) + "\n</svg>"


def text(x, y, words, anchor="start", size=14, colour="#222"):
    return f'<text x="{x}" y="{y}" text-anchor="{anchor}" font-size="{size}" fill="{colour}">{words}</text>'



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
    out.append(f'<text x="{a5x + 12}" y="{top + 4}" font-size="10" fill="#fff">+5</text>')
    out.append(f'<text x="{a5x + 12}" y="{top + pitch + 4}" font-size="10" fill="#fff">Gnd</text>')
    out.append(f'<text x="{a5x + 12}" y="{top + 2 * pitch + 4}" font-size="10" fill="#fff">A0-5</text>')
    out.append(f'<path d="M230 288 Q{a5x} 290 {a5x} {top + 2 * pitch + 12}" fill="none" stroke="#e65100" stroke-width="2.5"/>')
    out.append(f'<path d="M{a5x - 6} {top + 2 * pitch + 22} L{a5x} {top + 2 * pitch + 11} L{a5x + 6} {top + 2 * pitch + 22}" fill="none" stroke="#e65100" stroke-width="2.5"/>')
    out.append('<text x="224" y="292" text-anchor="end" font-size="14" fill="#222">the A5 column</text>')
    label = ("Step 12: the motor shield from above. The yellow analog header is at the bottom right, tucked under the right motor connector: "
             "three rows, +5 on top, Gnd in the middle and A0-5 along the board's edge, with A0 on the left and A5 on the right. "
             "The A5 column, the one furthest right, is circled.")
    return f'<svg viewBox="0 0 360 300" role="img" aria-label="{label}">\n' + "\n".join(out) + "\n</svg>"


SENSOR_NAMES = {3: "back", 4: "left", 5: "right"}


def header_closeup(columns):
    """Close-up of the analog header from above. Each dupont plug stands on its pin
    with the wire coming straight up at you: a coloured dot on the plug."""
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
        for (colour, *_rest), y in zip(WIRES, ys):
            out.append(f'<rect x="{xs[col] - 20}" y="{y - 20}" width="40" height="40" rx="3" fill="#202326" stroke="#4a5058" stroke-width="1.5"/>')
            out.append(f'<circle cx="{xs[col]}" cy="{y}" r="11" fill="{colour}" stroke="#fff" stroke-width="1.5"/>')
    for label, y in (("+5", ys[0]), ("Gnd", ys[1]), ("A0-5", ys[2])):
        out.append(f'<text x="{xs[5] + 28}" y="{y + 5}" font-size="13" font-weight="700" fill="#fff">{label}</text>')
    for i, x in enumerate(xs):
        used = i in columns
        out.append(f'<text x="{x}" y="246" text-anchor="middle" font-size="14" font-weight="{700 if used else 400}" fill="{"#c24400" if used else "#555"}">A{i}</text>')
        if used:
            out.append(f'<text x="{x}" y="263" text-anchor="middle" font-size="12" fill="#555">{SENSOR_NAMES[i]}</text>')
    if columns == [5]:
        label = ("Step 13: close-up of the yellow analog header from above, columns A0 to A5 from left to right, rows +5 on top, Gnd in the middle, "
                 "A0-5 at the board's edge, with the motor connector right above the A5 column. The right sensor's three plugs sit on the A5 column: "
                 "red on +5, black on Gnd, blue on A5.")
    else:
        label = ("Step 14: the same header with all three sensors plugged in: the right sensor on A5, the left sensor on A4 and the back sensor on A3, "
                 "each with red on +5, black on Gnd and blue on the A row.")
    return f'<svg viewBox="0 20 360 250" role="img" aria-label="{label}">\n' + "\n".join(out) + "\n</svg>"


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
        return header_closeup([3, 4, 5])
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
        parts.append(text(XS[2] + 26, 98, "yellow violet", size=12, colour="#555"))
        parts.append(text(XS[2] + 26, 114, "red gold", size=12, colour="#555"))
    if state in ("plugged", "folded", "taped"):
        parts.append(text(XS[0], HOUSING_TOP - 28, "empty", "middle", 13, "#555"))
        parts.append(text(XS[0], HOUSING_TOP - 12, "for now", "middle", 13, "#555"))
    if state == "folded":
        parts.append(text(XS[2] + 28, HOUSING_TOP + 50, "resistor"))
        parts.append(text(XS[2] + 28, HOUSING_TOP + 68, "folded"))
        parts.append(text(XS[2] + 28, HOUSING_TOP + 86, "down on 3"))
    if state == "taped":
        parts.append(text(tape_right + 10, HOUSING_TOP + 52, "tape round"))
        parts.append(text(tape_right + 10, HOUSING_TOP + 70, "2 and 3"))
    if state == "ldr-ready":
        for x in (XS[0], XS[2]):
            parts.append(f'<line x1="{x}" y1="{ldr_tip + 8}" x2="{x}" y2="{HOUSING_TOP - 8}" stroke="#e65100" stroke-width="2" stroke-dasharray="4 4"/>')
            parts.append(f'<path d="M{x - 6} {HOUSING_TOP - 14} L{x} {HOUSING_TOP - 4} L{x + 6} {HOUSING_TOP - 14}" fill="none" stroke="#e65100" stroke-width="2"/>')
        parts.append(text(XS[1] + 50, 10, "LDR"))
        parts.append(text(XS[1] + 50, 28, "(GL5537)", size=12, colour="#555"))
        parts.append(text(XS[0] - 10, ldr_tip - 30, "to 1", "end"))
        parts.append(text(XS[2] + 10, ldr_tip - 30, "to 3"))
    if state == "ldr-in":
        parts.append(text(XS[2] + 26, 110, "standing well"))
        parts.append(text(XS[2] + 26, 128, "clear of the plastic"))
    labels = {
        "ready": "Step 1: a 4.7k resistor with one lead bent back so both leads point down, above three dupont connectors: 1 red 5V, 2 black Gnd, 3 blue A5. The long bent lead lines up with connector 2, the short straight lead with connector 3.",
        "plugged": "Step 2: the long bent lead in connector 2 (Gnd, black), the short straight lead in connector 3 (A5, blue). Connector 1 (5V, red) is still empty.",
        "folded": "Step 3: the resistor folded down flat on the plastic of connector 3, covering the 3. The bent lead runs down the front of connector 2.",
        "taped": "Step 4: tape wrapped round connectors 2 and 3, holding the resistor against them.",
        "ldr-ready": "Step 5: the LDR above the connectors, its two legs spread to line up with connector 1 (5V, red) and connector 3 (A5, blue), either side of the taped connector 2.",
        "ldr-in": "Step 6: the LDR's legs pushed into connectors 1 and 3, with the LDR still standing well clear of the connectors.",
    }
    return f'<svg viewBox="0 {y0} 360 {440 - y0}" role="img" aria-label="{labels[state]}">\n' + "\n".join(parts) + "\n</svg>"


html = f'''<!-- ldr-steps: drawn by scripts/ldr-figs.py -->
  <section class="card">
    <h2>Step by step: the right sensor (A5)</h2>
    <p>Three 20 cm female-female dupont wires. Your colours will be mixed up; in the pictures
    <b>1 is red (5V)</b>, <b>2 is black (Gnd)</b> and <b>3 is blue (A5)</b>.</p>
    <div class="steps">
      <figure>
        {figure("ready")}
        <figcaption><b>1.</b> Bend one lead of the 4.7k resistor back over its body, so both
        leads point the same way. The bent lead is the long one.</figcaption>
      </figure>
      <figure>
        {figure("plugged")}
        <figcaption><b>2.</b> Push the <b>long bent lead into 2</b> (Gnd) and the <b>short
        straight lead into 3</b> (A5).</figcaption>
      </figure>
      <figure>
        {figure("folded")}
        <figcaption><b>3.</b> Fold the resistor down flat onto the plastic of connector 3, so it
        covers the 3.</figcaption>
      </figure>
      <figure>
        {figure("taped")}
        <figcaption><b>4.</b> Tape connectors 2 and 3 together, with the resistor caught under
        the tape.</figcaption>
      </figure>
      <figure>
        {figure("ldr-ready")}
        <figcaption><b>5.</b> Spread the LDR's legs a little and line them up with <b>1</b> (5V)
        and <b>3</b> (A5), either side of the taped Gnd connector.</figcaption>
      </figure>
      <figure>
        {figure("ldr-in")}
        <figcaption><b>6.</b> Push the legs in, but leave the LDR standing well clear of the
        connectors.</figcaption>
      </figure>
      <figure>
        {figure("side-folded")}
        <figcaption><b>7.</b> Fold the LDR down onto the <b>back</b> of the connectors, the
        opposite side from the resistor.</figcaption>
      </figure>
      <figure>
        {figure("side-taped")}
        <figcaption><b>8.</b> Tape over the LDR's legs, high up near the end of the connectors, so the bend uses up only a little of the legs.</figcaption>
      </figure>
      <figure>
        {figure("side-z")}
        <figcaption><b>9.</b> Fold the LDR back up over the tape, so it sticks out of the top
        again. The Z fold stops it pulling out.</figcaption>
      </figure>
      <figure>
        {figure("side-more-tape")}
        <figcaption><b>10.</b> More tape round everything, over the Z fold.</figcaption>
      </figure>
      <figure>
        {figure("done")}
        <figcaption><b>11.</b> Done: the right light sensor (A5), ready to plug into the robot.</figcaption>
      </figure>
      <figure>
        {figure("board")}
        <figcaption><b>12.</b> Find the yellow analog header at the bottom right of the motor
        shield. Its rows are <b>+5</b> on top, <b>Gnd</b> in the middle and <b>A0-5</b> along the
        edge. A0 is on the left, <b>A5 on the right</b>.</figcaption>
      </figure>
      <figure>
        {figure("header")}
        <figcaption><b>13.</b> Push the other ends of the three wires onto the <b>A5 column</b>,
        tight against the motor connector: red on <b>+5</b>, black on <b>Gnd</b>, blue on
        <b>A5</b>.</figcaption>
      </figure>
      <figure>
        {figure("header-all")}
        <figcaption><b>14.</b> Build the <b>left</b> sensor and the <b>back</b> sensor the same
        way (steps 1 to 11). The left one goes on <b>A4</b>, the back one on <b>A3</b>: red on top,
        black in the middle, blue at the edge.</figcaption>
      </figure>
    </div>
    <p>The back sensor is optional: the robot works with just the left and right ones.</p>
  </section>
<!-- /ldr-steps -->'''

page = "/workspace/robots-site/mecanum/light.html"
s = open(page).read()
if "<!-- ldr-steps" in s:
    s = re.sub(r"<!-- ldr-steps[\s\S]*?<!-- /ldr-steps -->", lambda m: html, s)
else:
    anchor = '  <section class="card">\n    <h2>If it does not work</h2>'
    assert anchor in s
    s = s.replace(anchor, html + "\n\n" + anchor)
if "@media print { .steps .fig-bg" not in s and ".steps {" in s:
    s = s.replace("  .steps figcaption { margin-top: 8px; font-size: 14px; }\n", "  .steps figcaption { margin-top: 8px; font-size: 14px; }\n  @media print { .steps .fig-bg { fill: #fff; } }\n", 1)
if ".steps {" not in s:
    s = s.replace("</style>", "  .steps { display: flex; flex-wrap: wrap; gap: 16px; }\n  .steps figure { margin: 0; flex: 1 1 260px; max-width: 360px; }\n  .steps svg { width: 100%; height: auto; display: block; }\n  .steps figcaption { margin-top: 8px; font-size: 14px; }\n  @media print { .steps .fig-bg { fill: #fff; } }\n</style>", 1)
open(page, "w").write(s)
print("ok")
