"""Draws the Corbata PDF icon -- a white calligraphy "C" on a red tile -- and
writes app.ico (Windows) and AppIcon.png (1024 px: the window icon, and turned
into .icns on macOS). Requires Pillow. Run:  python resources/make_icon.py

The C is one pointed-pen stroke: a hairline loop on the left that crosses
into a narrow loop with a small curl, then a heavy downstroke into the bowl
and a hairline exit. Width follows pen pressure (thick on downstrokes along
the slant). Drawn 4x larger and scaled down for smooth edges.
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
S = 1024
SUPERSAMPLE = 4
RED = (179, 11, 0)
THICK, HAIR = 8.5, 2.6           # stroke widths, in path units (letter is ~100 tall)
SLANT = (-0.42, 0.91)            # direction of the heaviest downstroke
UPSTROKE = 0.22                  # how much weight upstrokes get
HEIGHT = 0.70                    # letter height as a fraction of the icon

# The pen path (y down), followed by a smooth Catmull-Rom spline.
PATH = [(71, 123), (62, 126), (51, 125), (42, 119), (37, 108), (38, 95), (46, 85), (60, 79), (80, 80),
        (98, 88), (108, 94), (117, 98), (126, 98), (133, 94), (131, 89), (124, 90), (118, 96), (114, 107),
        (114, 118), (119, 124), (129, 120), (137, 108), (142, 94), (141, 83), (134, 77), (125, 78),
        (118, 82), (108, 90), (97, 104), (87, 124), (81, 146), (82, 163), (89, 173), (100, 176),
        (111, 170), (122, 155), (131, 138)]
# Pressure per path point: full on the main downstroke, light elsewhere.
PRESSURE = [1.0 if 24 <= i <= 35 else 0.25 for i in range(len(PATH))]
STEPS = 60  # samples per path segment


def catmull_rom(points, values):
    pts, vals = [], []
    q = [points[0]] + points + [points[-1]]
    for i in range(1, len(q) - 2):
        p0, p1, p2, p3 = q[i - 1], q[i], q[i + 1], q[i + 2]
        for j in range(STEPS):
            t = j / STEPS
            pts.append(tuple(0.5 * (2 * p1[c] + (-p0[c] + p2[c]) * t + (2 * p0[c] - 5 * p1[c] + 4 * p2[c] - p3[c]) * t * t
                                    + (-p0[c] + 3 * p1[c] - 3 * p2[c] + p3[c]) * t ** 3) for c in (0, 1)))
            vals.append(values[i - 1] * (1 - t) + values[i] * t)
    pts.append(points[-1])
    vals.append(values[-1])
    return pts, vals


def tangent(pts, i):
    a, b = pts[max(i - 1, 0)], pts[min(i + 1, len(pts) - 1)]
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy) or 1
    return dx / length, dy / length


def pen_stroke():
    pts, pressure = catmull_rom(PATH, PRESSURE)
    n = len(pts)
    widths = []
    for i in range(n):
        tx, ty = tangent(pts, i)
        d = tx * SLANT[0] + ty * SLANT[1]
        widths.append(HAIR + (THICK - HAIR) * (pressure[i] * max(d, 0) ** 3 + UPSTROKE * max(-d, 0) ** 2))
    for _ in range(3):  # smooth the swelling
        acc = [0.0]
        for w in widths:
            acc.append(acc[-1] + w)
        r = 25
        widths = [(acc[min(i + r, n)] - acc[max(i - r, 0)]) / (min(i + r, n) - max(i - r, 0)) for i in range(n)]
    for i in range(n):  # slightly tapered ends
        t = min(i, n - 1 - i) / (n * 0.02)
        if t < 1:
            widths[i] *= 0.55 + 0.45 * t
    return pts, widths


def draw_icon():
    big = S * SUPERSAMPLE
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0.04 * big, 0.04 * big, 0.96 * big, 0.96 * big], radius=0.2 * big, fill=RED)
    pts, widths = pen_stroke()
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    k = HEIGHT * big / (max(ys) - min(ys))
    ox, oy = (min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2

    def px(x, y):
        return (x - ox) * k + big / 2, (y - oy) * k + big / 2

    normals = [(-ty, tx) for tx, ty in (tangent(pts, i) for i in range(len(pts)))]
    for i in range(len(pts) - 1):
        quad = []
        for j, side in ((i, 1), (i + 1, 1), (i + 1, -1), (i, -1)):
            h = side * widths[j] / 2
            quad.append(px(pts[j][0] + normals[j][0] * h, pts[j][1] + normals[j][1] * h))
        d.polygon(quad, fill="white")
    for j in (0, len(pts) - 1):  # round caps
        r = widths[j] / 2 * k
        x, y = px(*pts[j])
        d.ellipse([x - r, y - r, x + r, y + r], fill="white")
    return img.resize((S, S), Image.LANCZOS)


img = draw_icon()
img.save(HERE / "AppIcon.png")
img.save(HERE / "app.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("wrote", HERE / "AppIcon.png", "and", HERE / "app.ico")
