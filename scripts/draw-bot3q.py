# Draws our mecanum bot (the LAFVIN 4WD kit) in a stylized front-quarter view, the
# way the product photo img/mecanumbot.png shows it: front to the lower left, left
# side toward you, seen a little from above. Writes two SVGs for instruction pages:
#   img/mecanumbot-3q.svg        the robot as it comes
#   img/mecanumbot-3q-light.svg  the same with a light sensor on each front corner
# Run: python3 scripts/draw-bot3q.py
# Robot coordinates are millimeters: x to the robot's right, y forward, z up.
import math
import os

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SCALE = 0.85
CX, CY = 200, 196


def P(x, y, z):
    """Robot millimeters to screen pixels, seen from the front left corner, above: the
    front faces the lower left, the left side the lower right (like the photo)."""
    return (CX + SCALE * (-0.85 * x - 0.75 * y), CY + SCALE * (-0.42 * x + 0.55 * y - 0.9 * z))


def poly(points, fill, stroke=None, width=1.2, extra=""):
    d = " ".join(f"{x:.1f},{y:.1f}" for x, y in points)
    s = f' stroke="{stroke}" stroke-width="{width}" stroke-linejoin="round"' if stroke else ""
    return f'<polygon points="{d}" fill="{fill}"{s}{extra}/>'


def path(points, stroke, width, closed=False, fill="none"):
    d = "M" + " L".join(f"{x:.1f} {y:.1f}" for x, y in points) + (" Z" if closed else "")
    return f'<path d="{d}" fill="{fill}" stroke="{stroke}" stroke-width="{width}" stroke-linecap="round" stroke-linejoin="round"/>'


def rounded_rect(x0, x1, y0, y1, r, z, steps=6):
    """A rounded rectangle in the plane z, as a list of screen points."""
    pts = []
    for cx, cy, a0 in ((x1 - r, y1 - r, 0), (x0 + r, y1 - r, 90), (x0 + r, y0 + r, 180), (x1 - r, y0 + r, 270)):
        for i in range(steps + 1):
            a = math.radians(a0 + 90 * i / steps)
            pts.append(P(cx + r * math.cos(a), cy + r * math.sin(a), z))
    return pts


def prism(x0, x1, y0, y1, z0, z1, top, front, side):
    """A box: its left side (x0), front (y1) and top are the faces we see."""
    out = [poly([P(x0, y0, z0), P(x0, y1, z0), P(x0, y1, z1), P(x0, y0, z1)], side),
           poly([P(x0, y1, z0), P(x1, y1, z0), P(x1, y1, z1), P(x0, y1, z1)], front),
           poly([P(x0, y0, z1), P(x1, y0, z1), P(x1, y1, z1), P(x0, y1, z1)], top)]
    return "".join(out)


def wheel(xc, yc, near):
    """A mecanum wheel on the axle along x at (xc, yc), radius 32, 35 wide. near = the
    face toward us is its outside (left wheels); far wheels show their inside."""
    r, half = 32, 17.5
    zc = r
    face_x = xc - half if near else xc + half    # the face that is closer to us is the -x one
    back_x = xc + half if near else xc - half
    out = []
    n = 16
    # rollers: slanted black bars around the tread, between the two rims
    for i in range(n):
        a1 = 2 * math.pi * i / n
        a2 = a1 + 2 * math.pi / n * 0.78
        sk = 0.45
        q = [P(back_x, yc + r * math.cos(a1 + sk), zc + r * math.sin(a1 + sk)),
             P(back_x, yc + r * math.cos(a2 + sk), zc + r * math.sin(a2 + sk)),
             P(face_x, yc + r * math.cos(a2), zc + r * math.sin(a2)),
             P(face_x, yc + r * math.cos(a1), zc + r * math.sin(a1))]
        out.append(poly(q, "#17191d", "#0b0c0e", 0.8))
    # the yellow side plate on the face we see, a little smaller than the rollers
    plate = [P(face_x - 0.5, yc + 0.80 * r * math.cos(t), zc + 0.80 * r * math.sin(t)) for t in [2 * math.pi * k / 40 for k in range(40)]]
    out.append(poly(plate, "#f6c21c", "#a77f00", 1.2))
    # roller ends peeking past the plate's rim
    for i in range(n):
        t = 2 * math.pi * (i + 0.4) / n
        out.append(path([P(face_x - 0.6, yc + 0.80 * r * math.cos(t), zc + 0.80 * r * math.sin(t)),
                         P(face_x - 0.6, yc + 0.98 * r * math.cos(t + 0.18), zc + 0.98 * r * math.sin(t + 0.18))], "#17191d", 3.2))
    hub = [P(face_x - 1, yc + 0.34 * r * math.cos(t), zc + 0.34 * r * math.sin(t)) for t in [2 * math.pi * k / 24 for k in range(24)]]
    out.append(poly(hub, "#e0a800", "#8a6900", 1))
    cap = [P(face_x - 1.5, yc + 0.13 * r * math.cos(t), zc + 0.13 * r * math.sin(t)) for t in [2 * math.pi * k / 16 for k in range(16)]]
    out.append(poly(cap, "#2a2d33"))
    # the yellow motor mount between the wheel and the plate
    return "".join(out)


def motor(xc, yc):
    """The yellow gear motor under the plate, from the wheel in toward the middle."""
    x_in = -20 if xc < 0 else 20
    x0, x1 = (min(xc + 17, x_in), max(xc + 17, x_in)) if xc < 0 else (min(x_in, xc - 17), max(x_in, xc - 17))
    return prism(x0, x1, yc - 11, yc + 11, 22, 42, "#f6c21c", "#e3ad12", "#c9960b")


def ldr(x, y, z):
    """A light sensor on a front corner, leaning out and down, with its two legs."""
    head = [P(x + 7 * math.cos(t), y + 9, z + 7 * math.sin(t)) for t in [2 * math.pi * k / 20 for k in range(20)]]
    legs = path([P(x - 2, y, z + 10), P(x - 2, y + 7, z + 2)], "#9aa3ad", 1.6) + path([P(x + 2, y, z + 10), P(x + 2, y + 7, z + 2)], "#9aa3ad", 1.6)
    zig = path([P(x - 4, y + 9.5, z + 2), P(x + 4, y + 9.5, z + 1), P(x - 4, y + 9.5, z), P(x + 4, y + 9.5, z - 1), P(x - 4, y + 9.5, z - 2)], "#c0541a", 1.4)
    return legs + poly(head, "#f1e9d6", "#b9a47a", 1.4) + zig


def robot(light=False):
    track, base = 156, 128
    parts = []
    # far side first: the right wheels sit behind the chassis
    for y in (-base / 2, base / 2):
        parts.append(wheel(track / 2, y, near=False))
        parts.append(motor(track / 2, y))
    # lower plate (z 40), with a little thickness
    lower = (-58, 58, -111, 111, 16)
    parts.append(poly(rounded_rect(*lower[:4], lower[4], 36), "#0c0d10"))
    parts.append(poly(rounded_rect(*lower[:4], lower[4], 40), "#22262d", "#0c0d10", 1.2))
    for x, y in ((-46, -92), (46, -92), (-46, 0), (46, 0), (-46, 92), (46, 92)):
        parts.append(poly(rounded_rect(x - 2.5, x + 2.5, y - 10, y + 10, 2.4, 40.2, 3), "#3a414b"))
    # battery holder on the lower plate, toward the back
    parts.append(prism(-36, 36, -100, -58, 40, 58, "#0f1216", "#1a1e24", "#14171c"))
    parts.append(prism(-32, 32, -96, -80, 58, 66, "#5a46b8", "#4a3a9c", "#40338a"))
    parts.append(prism(-32, 32, -76, -62, 58, 66, "#5a46b8", "#4a3a9c", "#40338a"))
    # brass standoffs up to the upper plate
    for x, y in ((-44, -40), (44, -40), (-44, 40), (44, 40)):
        parts.append(path([P(x, y, 40), P(x, y, 82)], "#c9a227", 3.2))
    # upper plate (z 84)
    upper = (-50, 50, -60, 58, 14)
    parts.append(poly(rounded_rect(*upper[:4], upper[4], 81), "#0c0d10"))
    parts.append(poly(rounded_rect(*upper[:4], upper[4], 84), "#22262d", "#0c0d10", 1.2))
    # the Arduino (teal) with the motor shield (blue) stacked on it
    parts.append(prism(-27, 27, -48, 22, 84, 90, "#0d8a99", "#0b7480", "#09636d"))
    parts.append(prism(-27, 27, -46, 18, 96, 99, "#1b4fa6", "#163f86", "#123570"))
    for x0 in (-27, 27 - 3.4):    # header strips down the shield's edges
        parts.append(prism(x0, x0 + 3.4, -42, 14, 99, 104, "#0b0d10", "#0b0d10", "#0b0d10"))
    for x in (-13, 4):            # two chips
        parts.append(prism(x, x + 9, -30, -8, 99, 103, "#0b0d10", "#16181c", "#111317"))
    for x, y in ((-20, 12), (2, 12), (-20, -44), (2, -44)):   # blue terminal blocks
        parts.append(prism(x, x + 18, y, y + 8, 99, 108, "#3d8ce6", "#2f75c8", "#2a68b2"))
    parts.append(prism(16, 26, -18, -6, 99, 106, "#f2d33c", "#d9bb2a", "#c4a822"))   # the analog header
    # Bluetooth module standing at the back right
    parts.append(prism(18, 22, -58, -42, 99, 128, "#1f62c4", "#1a53a6", "#174a94"))
    # sonar on the front of the lower plate: a board facing forward with two eyes
    parts.append(prism(-26, 26, 96, 100, 40, 66, "#1f62c4", "#2a6fd0", "#1a53a6"))
    for ex in (-13, 13):
        rim = [P(ex + 9 * math.cos(t), 100, 53 + 9 * math.sin(t)) for t in [2 * math.pi * k / 24 for k in range(24)]]
        face = [P(ex + 9 * math.cos(t), 112, 53 + 9 * math.sin(t)) for t in [2 * math.pi * k / 24 for k in range(24)]]
        parts.append(poly(rim, "#9aa1a9"))
        side = [P(ex - 9, 100, 53), P(ex - 9, 112, 53)]
        parts.append(poly([P(ex + 9 * math.cos(t), 100 + 12 * s, 53 + 9 * math.sin(t)) for s, t in
                           [(0, 2 * math.pi * k / 24) for k in range(13)] + [(1, 2 * math.pi * k / 24) for k in range(12, -1, -1)]], "#b8bec6"))
        parts.append(poly(face, "#cdd2d8", "#8f969e", 1))
        mesh = [P(ex + 6 * math.cos(t), 112.5, 53 + 6 * math.sin(t)) for t in [2 * math.pi * k / 20 for k in range(20)]]
        parts.append(poly(mesh, "#2a2f36"))
    # a few wires from the shield down to the sonar
    for i, color in enumerate(("#e33b2e", "#f6c21c", "#f4f6f8", "#2fa866")):
        x = -9 + i * 6
        a, b, c = P(x, 14, 104), P(x * 1.6, 70, 96), P(x * 0.6, 98, 66)
        parts.append(f'<path d="M{a[0]:.1f} {a[1]:.1f} Q{b[0]:.1f} {b[1]:.1f} {c[0]:.1f} {c[1]:.1f}" fill="none" stroke="{color}" stroke-width="1.8" stroke-linecap="round"/>')
    if light:
        parts.append(ldr(50, 104, 42))     # right front corner first (behind)
    # near side last: the left wheels and their motors are closest to us
    for y in (-base / 2, base / 2):
        parts.append(motor(-track / 2, y))
        parts.append(wheel(-track / 2, y, near=True))
    if light:
        parts.append(ldr(-50, 104, 42))
    return "\n".join(parts)


def write(name, light):
    title = "Our mecanum robot" + (" with its two light sensors" if light else "")
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="55 58 290 228" role="img" aria-label="{title}, seen from the front left: '
           'four yellow mecanum wheels, a black two-deck chassis, the blue motor shield on top and the sonar\'s two eyes on the front'
           + (', and a light sensor on each front corner' if light else '') + '.">\n<title>' + title + '</title>\n' + robot(light) + "\n</svg>\n")
    open(os.path.join(ROOT, "img", name), "w").write(svg)
    print("wrote img/" + name)


write("mecanumbot-3q.svg", False)
write("mecanumbot-3q-light.svg", True)
