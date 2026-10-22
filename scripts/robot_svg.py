# Shared robot drawings for the instruction pages.
# bot_top(): our mecanum bot from above, the Blocks simulator's drawing as SVG.

def bot_top(cx, cy, angle, scale=0.5, ldrs=True, uid="bot"):
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
        clip = f"{uid}wheel{n}"
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
