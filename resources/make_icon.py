"""Draws the Corbata PDF icon (a white cursive "C" on a red tile) and writes
app.ico (Windows) and AppIcon.png (1024 px: the app's window icon, and turned
into .icns on macOS). Requires Pillow and Windows' "Script MT Bold" font.
Run:  python resources/make_icon.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

HERE = Path(__file__).resolve().parent
S = 1024
RED = (179, 11, 0)
FONT = "C:/Windows/Fonts/SCRIPTBL.TTF"  # Script MT Bold

img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
d.rounded_rectangle([0.04 * S, 0.04 * S, 0.96 * S, 0.96 * S], radius=0.2 * S, fill=RED)
font = ImageFont.truetype(FONT, int(0.78 * S))
l, t, r, b = d.textbbox((0, 0), "C", font=font)
d.text(((S - (l + r)) / 2, (S - (t + b)) / 2), "C", font=font, fill="white")

img.save(HERE / "AppIcon.png")
img.save(HERE / "app.ico", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print("wrote", HERE / "AppIcon.png", "and", HERE / "app.ico")
