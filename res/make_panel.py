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
FONT_BLACK = os.environ.get("PANEL_FONT_BLACK", "/usr/share/fonts/opentype/inter/InterDisplay-Black.otf")

W, H = 81.28, 128.5  # 16 HP
IN_X0, IN_DX = 6.6, 9.725      # 8 input jacks
OCT_X = 11.0                   # octave knob in the output box
OUT_X0, OUT_DX = 25.0, 14.6    # 4 output jacks

PAPER = "#ECE8DF"
INK = "#151515"
RED = "#D7362B"
GREY = "#8A857B"

fonts = {}


def text(s, x, y, size, font=FONT_MED, fill=INK, anchor="middle", spacing=0.0):
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


def tick(cx, cy, value, r0, r1, label="", rl=None, fill=INK, size=1.4):
    """A mark at a knob position (value 0..1 over a -150..+150 degree sweep)."""
    a = math.radians(-150 + 300 * value)
    sx, sy = math.sin(a), -math.cos(a)
    add(f'<line x1="{cx + r0 * sx:.3f}" y1="{cy + r0 * sy:.3f}" x2="{cx + r1 * sx:.3f}" '
        f'y2="{cy + r1 * sy:.3f}" stroke="{fill}" stroke-width="0.25"/>')
    if label:
        rl = rl or r1 + 1.4
        add(text(label, cx + rl * sx, cy + rl * sy + size * 0.36, size, FONT_MED, GREY))


def rule(y):
    add(f'<line x1="4.5" y1="{y}" x2="{W - 4.5}" y2="{y}" stroke="{INK}" stroke-width="0.18"/>')


def label(s, x, y, size=1.9, fill=INK, font=None):
    add(text(s, x, y, size, font or FONT_BOLD, fill))


add(f'<rect width="{W}" height="{H}" fill="{PAPER}"/>')

# title
add(text("weighted", 4.6, 11.4, 6.2, FONT_BLACK, INK, anchor="start", spacing=-0.02))
add(f'<circle cx="{W - 6.2}" cy="9.3" r="1.5" fill="{RED}"/>')
add(text("quantizer by weight", W - 9.0, 10.0, 1.6, FONT_MED, GREY, anchor="end"))
# the display widget sits at (4.5, 15.0), 72.28 x 28.0 mm

# row A: root, relative, scale, profile (knob r = 6.1)
c1, c2, c3 = 14.0, 40.64, 67.28
rowA, rowB, rowC = 52.0, 70.5, 87.0
rule(44.8)
for x, n in ((c1, "root"), (c2, "scale"), (c3, "profile")):
    label(n, x, rowA + 8.6)
label("relative", 27.32, rowA + 5.4, 1.7)

rule(62.6)

# row B: focus, step, range
label("focus", c1, rowB + 8.8, 2.3, RED)
tick(c1, rowB, 0.0, 6.9, 7.8)
tick(c1, rowB, 1 / 3, 6.9, 7.8, "1")
tick(c1, rowB, 1.0, 6.9, 7.8, "3")
label("step", c2, rowB + 8.6)
tick(c2, rowB, 0.0, 6.9, 7.8)
tick(c2, rowB, 1.0, 6.9, 7.8)
add(text("leaps", c2 - 9.4, rowB + 6.6, 1.35, FONT_MED, GREY))
add(text("steps", c2 + 9.4, rowB + 6.6, 1.35, FONT_MED, GREY))
label("range", c3, rowB + 8.6)
for i in range(5):
    if i != 2:  # the top tick would touch the rule above
        tick(c3, rowB, i / 4, 6.9, 7.8, str(i + 1) if i in (0, 4) else "")

# row C
label("cv amt", c1, rowC + 5.6, 1.6, GREY)
for x, n in ((29.0, "length"), (41.5, "strong"), (54.0, "gravity"), (64.0, "invert"), (74.0, "learn")):
    label(n, x, rowC + 6.6 if n in ("length", "strong") else rowC + 5.6, 1.75)

rule(95.0)

# inputs
for i, n in enumerate(("cv", "trig", "accent", "root", "scale", "rel", "profile", "focus")):
    add(text(n, IN_X0 + IN_DX * i, 97.9, 1.5, FONT_MED if i >= 3 else FONT_BOLD, INK if i < 3 else GREY))

# outputs on a black strip
add(f'<rect x="4.5" y="107.6" width="{W - 9.0}" height="15.2" rx="0.6" fill="{INK}"/>')
add(text("octave \u00b13", OCT_X, 111.2, 1.6, FONT_BOLD, PAPER))
for i, n in enumerate(("v/oct", "trig", "degree", "stability")):
    add(text(n, OUT_X0 + OUT_DX * i, 111.2, 1.6, FONT_BOLD, RED if i == 3 else PAPER))

svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" '
    f'viewBox="0 0 {W} {H}">\n' + "\n".join(parts) + "\n</svg>\n"
)
with open(os.path.join(HERE, "Weighted.svg"), "w") as fh:
    fh.write(svg)
print("wrote Weighted.svg")
