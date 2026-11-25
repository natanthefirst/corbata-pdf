"""Draws the Corbata PDF icon (a page with a red necktie) and writes
app.ico (Windows) and AppIcon.png (1024 px, turned into .icns on macOS).
Requires Pillow. Run:  python resources/make_icon.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

HERE = Path(__file__).resolve().parent
S = 1024
k = S / 100  # the design is on a 100x100 grid


def p(*pts):
    return [(x * k, y * k) for x, y in pts]


img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
d.rounded_rectangle([4 * k, 4 * k, 96 * k, 96 * k], radius=20 * k, fill=(0x1F, 0x3A, 0x5F))
d.rounded_rectangle([24 * k, 14 * k, 76 * k, 86 * k], radius=5 * k, fill="white")
red = (0xD9, 0x3A, 0x2B)
d.polygon(p((43, 22), (57, 22), (54, 31), (46, 31)), fill=red)
d.polygon(p((46, 32), (54, 32), (61, 66), (50, 77), (39, 66)), fill=red)

img.save(HERE / "AppIcon.png")
img.save(HERE / "app.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("wrote", HERE / "AppIcon.png", "and", HERE / "app.ico")
