#!/usr/bin/env python3
# Draws res/Weighted.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# src/Weighted.cpp.
#   pip install fonttools
#   python3 res/make_panel.py
import math
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 81.28, 128.5  # 16 HP

BRONZE = "#4A3826"
BRONZE_DARK = "#3A2B1D"
INK = "#F3E6CC"
SOFT = "#C2AE8C"
GOLD = "#D9A441"
EMBER = "#E86A3A"
OUTBOX = "#21180F"

fonts = {}


def text(s, x, y, size, font=FONT_MED, fill=INK, anchor="middle", spacing=0.04):
    """Return an SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    if anchor == "middle":
        x0 = x - width * scale / 2
    elif anchor == "end":
        x0 = x - width * scale
    else:
        x0 = x
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        tp = TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y))
        gs[n].draw(tp)
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


parts = []
add = parts.append


def tick(cx, cy, value, label, r0=6.6, r1=7.6, fill=SOFT, size=1.6):
    """A mark at a knob position (value 0..1 over Rack's -150..+150 degree sweep)."""
    a = math.radians(-150 + 300 * value)
    sx, sy = math.sin(a), -math.cos(a)
    add(f'<line x1="{cx + r0 * sx:.3f}" y1="{cy + r0 * sy:.3f}" x2="{cx + r1 * sx:.3f}" '
        f'y2="{cy + r1 * sy:.3f}" stroke="{fill}" stroke-width="0.35"/>')
    if label:
        rl = r1 + 1.5
        add(text(label, cx + rl * sx, cy + rl * sy + size * 0.36, size, FONT_BOLD, fill))


def rule(y):
    add(f'<line x1="3.0" y1="{y}" x2="{W - 3.0}" y2="{y}" stroke="{BRONZE_DARK}" stroke-width="0.5"/>')


add(f'<rect width="{W}" height="{H}" fill="{BRONZE}"/>')
# faint hammered texture: a few soft discs
for i, (x, y, r) in enumerate([(12, 30, 9), (60, 58, 12), (30, 80, 10), (70, 100, 8), (20, 115, 7), (50, 20, 6)]):
    add(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{BRONZE_DARK}" opacity="0.18"/>')

# title with a small weighted-bars mark
add(text("WEIGHTED", 5.0, 9.4, 3.6, FONT_BOLD, INK, anchor="start", spacing=0.12))
bars = [0.35, 0.7, 0.5, 1.0, 0.45]
for i, b in enumerate(bars):
    h = 4.2 * b
    add(f'<rect x="{W - 26.0 + i * 2.1:.2f}" y="{9.6 - h:.2f}" width="1.4" height="{h:.2f}" fill="{GOLD if b == 1.0 else SOFT}"/>')

# display window frame
add(f'<rect x="4.1" y="11.6" width="73.08" height="30.8" rx="1.6" fill="#120E0A"/>')

# row A: root, scale, profile
c1, c2, c3 = 14.0, 40.64, 67.28
for x, n in zip((c1, c2, c3), ("ROOT", "SCALE", "PROFILE")):
    add(text(n, x, 45.6, 2.3, FONT_BOLD, INK, spacing=0.06))
add(text("names show in the display", W / 2, 60.2, 1.5, FONT_MED, SOFT))

rule(62.2)

# row B: focus, step, range
add(text("FOCUS", c1, 64.9, 2.3, FONT_BOLD, GOLD, spacing=0.06))
tick(c1, 70.5, 0.0, "flat", r0=8.0, r1=9.0)
tick(c1, 70.5, 1 / 3, "1", r0=8.0, r1=9.0)
tick(c1, 70.5, 1.0, "3", r0=8.0, r1=9.0)
add(text("STEP", c2, 64.9, 2.3, FONT_BOLD, INK, spacing=0.06))
tick(c2, 70.5, 0.0, "")
tick(c2, 70.5, 1.0, "")
add(text("leaps", c2 - 7.6, 77.6, 1.5, FONT_MED, SOFT))
add(text("steps", c2 + 7.6, 77.6, 1.5, FONT_MED, SOFT))
add(text("RANGE", c3, 64.9, 2.3, FONT_BOLD, INK, spacing=0.06))
for i in range(5):
    tick(c3, 70.5, i / 4, str(i + 1) if i in (0, 4) else "")

# row C
rowC = 87.0
add(text("CV AMT", c1, 82.2, 1.75, FONT_BOLD, SOFT))
for x, n in ((29.0, "LENGTH"), (41.5, "STRONG"), (54.0, "GRAVITY"), (64.0, "INVERT"), (74.0, "LEARN")):
    add(text(n, x, 82.2, 1.75, FONT_BOLD, INK))
add(text("accent cycle  (strong 0 = off)", 35.25, 93.6, 1.35, FONT_MED, SOFT))

rule(95.6)

# inputs
for i, n in enumerate(("CV", "TRIG", "ACCENT", "ROOT", "SCALE", "PROFILE", "FOCUS")):
    add(text(n, 7.64 + 11.0 * i, 98.4, 1.6, FONT_BOLD, INK if i < 3 else SOFT))

# outputs
add(f'<rect x="4.1" y="109.2" width="73.08" height="16.4" rx="1.2" fill="{OUTBOX}"/>')
for i, n in enumerate(("V/OCT", "TRIG", "DEGREE", "STABILITY")):
    add(text(n, 13.0 + 18.43 * i, 113.6, 1.75, FONT_BOLD, GOLD if i == 3 else INK))

svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" '
    f'viewBox="0 0 {W} {H}">\n' + "\n".join(parts) + "\n</svg>\n"
)
with open(os.path.join(HERE, "Weighted.svg"), "w") as fh:
    fh.write(svg)
print("wrote Weighted.svg")
