#!/usr/bin/env python3
# Draws res/Weighted.svg (20 HP, Paper design). VCV Rack can't draw SVG <text>,
# so every label is turned into outline paths with fontTools. Positions are in
# mm and must match src/Weighted.cpp.
#   pip install fonttools
#   python3 res/make_panel.py
import math
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FB = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FM = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")
FK = os.environ.get("PANEL_FONT_BLACK", "/usr/share/fonts/opentype/inter/InterDisplay-Black.otf")
PAPER, INK, RED, GREY = "#ECE8DF", "#151515", "#D7362B", "#8A857B"
W, H = 101.6, 128.5  # 20 HP
L = 3.2   # control label size (was 1.6-1.9)
_f = {}
def text(s, x, y, size, font=FB, fill=INK, anchor="middle", spacing=0.0):
    if font not in _f:
        f = TTFont(font); _f[font] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    gs, cmap, upm = _f[font]; sc = size / upm; tr = spacing * upm
    names = [cmap[ord(c)] for c in s]; width = sum(gs[n].width + tr for n in names) - tr
    x0 = x - width * sc / 2 if anchor == "middle" else (x - width * sc if anchor == "end" else x)
    pen = SVGPathPen(gs); cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (sc, 0, 0, -sc, x0 + cx * sc, y))); cx += gs[n].width + tr
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'
p = []; add = p.append
def tick(cx, cy, v, r0, r1, lab="", size=2.2):
    a = math.radians(-150 + 300 * v); sx, sy = math.sin(a), -math.cos(a)
    add(f'<line x1="{cx+r0*sx:.2f}" y1="{cy+r0*sy:.2f}" x2="{cx+r1*sx:.2f}" y2="{cy+r1*sy:.2f}" stroke="{INK}" stroke-width="0.3"/>')
    if lab:
        rl = r1 + 1.8; add(text(lab, cx + rl*sx, cy + rl*sy + size*0.36, size, FM, GREY))
def rule(y): add(f'<line x1="4.5" y1="{y}" x2="{W-4.5}" y2="{y}" stroke="{INK}" stroke-width="0.2"/>')
col = [12.7 + 25.4 * i for i in range(4)]
rowA, rowB, rowC = 45.0, 65.0, 82.5
add(f'<rect width="{W}" height="{H}" fill="{PAPER}"/>')
add(text("weighted", 4.6, 11.2, 6.2, FK, INK, anchor="start", spacing=-0.02))
add(f'<circle cx="{W-6.2}" cy="9.2" r="1.5" fill="{RED}"/>')
add(text("quantizer by weight", W - 9.2, 10.0, 2.2, FM, GREY, anchor="end"))
# display 4.5,14 to W-4.5, 36
add(text("root", col[0], rowA + 10.3, L)); add(text("relative", col[1], rowA + 7.6, L)); add(text("scale", col[2], rowA + 10.3, L)); add(text("profile", col[3], rowA + 10.3, L))
rule(57.6)
add(text("focus", col[0], rowB + 10.4, L * 1.15, FB, RED))
tick(col[0], rowB, 0, 6.9, 7.8); tick(col[0], rowB, 1/3, 6.9, 7.8, "1"); tick(col[0], rowB, 1, 6.9, 7.8)
add(text("step", col[1], rowB + 10.3, L)); tick(col[1], rowB, 0, 6.9, 7.8); tick(col[1], rowB, 1, 6.9, 7.8)
add(text("leaps", col[1] - 10.4, rowB + 7.4, 2.2, FM, GREY)); add(text("steps", col[1] + 10.4, rowB + 7.4, 2.2, FM, GREY))
add(text("range", col[2], rowB + 10.3, L))
for i in (0, 1, 3, 4): tick(col[2], rowB, i / 4, 6.9, 7.8)
add(text("cv amt", col[3], rowB + 7.2, L * 0.9, FB, GREY))
add(text("(focus)", col[3], rowB + 10.2, 2.2, FM, GREY))
c5 = [12.7 + 19.05 * i for i in range(5)]
for x, n in zip(c5, ("length", "strong", "gravity", "invert", "learn")):
    add(text(n, x, rowC + 8.6, L))
rule(93.8)
ix = [7.6 + 12.343 * i for i in range(8)]
for i, n in enumerate(("cv", "trig", "accent", "root", "scale", "rel", "profile", "focus")):
    add(text(n, ix[i], 97.6, 2.9, FB if i < 3 else FM, INK if i < 3 else GREY))
add(f'<rect x="4.5" y="106.6" width="{W-9.0}" height="17.6" rx="0.6" fill="{INK}"/>')
for x, n in zip(c5, ("octave", "v/oct", "trig", "degree", "stability")):
    add(text(n, x, 111.3, L, FB, RED if n == "stability" else PAPER))
add(text("-3", c5[0] - 6.6, 119.8, 2.2, FM, "#9a958b")); add(text("+3", c5[0] + 6.6, 119.8, 2.2, FM, "#9a958b"))
open(os.path.join(HERE, "Weighted.svg"), "w").write(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">' + "".join(p) + "</svg>")
print("wrote Weighted.svg")
