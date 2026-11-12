#!/usr/bin/env python3
# Draws res/Changes.svg (28 HP). VCV Rack can't draw SVG <text>, so every
# label is turned into outline paths with fontTools. Positions are in mm and
# must match src/Changes.cpp.
#   pip install fonttools
#   python3 res/make_changes_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FB = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FM = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")
BG, INK, QUIET, ACC = "#ece5d8", "#26262a", "#6b6660", "#cf5a2e"
PLATE, ONPLATE, CYAN, TEAL = "#2b2c31", "#f3efe8", "#3fd0e0", "#1f8fa0"
W, H = 142.24, 128.5  # 28 HP
CTL, JCK = 3.2, 2.9   # label sizes (house standard)
_f = {}


def text(s, x, y, size, font=FB, fill=INK, anchor="middle", spacing=0.02):
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
add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
add(text("THE CHANGES", 8, 13.2, 6.2, FB, INK, anchor="start", spacing=0.08))
add(text("chords the way the style plays them", W - 8, 13.2, 3.4, FM, QUIET, anchor="end"))
add(f'<line x1="8" y1="15.6" x2="{W - 8}" y2="15.6" stroke="{ACC}" stroke-width="0.5"/>')
add(f'<rect x="8" y="18.5" width="{W - 16}" height="21" rx="1.2" fill="#1d1e22"/>')  # display (drawn by the module)
# row 1: setting knobs (RoundBlackKnob, r ~4.2)
for i, n in enumerate(("STYLE", "KEY", "MODE", "RATE", "LOOP")):
    add(text(n, 15 + i * (W - 30) / 4, 50 - 6.6, CTL))
# row 2: big knobs (RoundLargeBlackKnob, r ~6) and their CV jacks
pair = (W - 16) / 4
for i, n in enumerate(("FOCUS", "COLOUR", "SPREAD", "DRIFT")):
    x = 8 + pair * i + pair / 2 - 5
    add(text(n, x, 68 - 8.4, CTL))
    add(text("CV", x + 13, 69.5 - 5.4, JCK))
# row 3: buttons and the tension meter
bx = [15 + i * (W - 30) / 4 for i in range(5)]
for x, n in zip(bx[:3], ("HOLD", "NEW", "LOCK")):
    add(text(n, x, 87 - 5.6, CTL))
add(text("TENSION", (bx[3] + bx[4]) / 2, 87 - 4.8, JCK, FB, QUIET))
# inputs
for i, n in enumerate(("CLOCK", "RESET", "CHANGE", "HOLD", "TRANSP", "LINK")):
    add(text(n, 14 + i * (W - 28) / 5, 103 - 5.4, JCK, FB, TEAL if n == "LINK" else INK))
# outputs on the plate
add(f'<rect x="6.5" y="108.6" width="{W - 13}" height="{122.3 - 108.6:.2f}" rx="1.6" fill="{PLATE}"/>')
for i, n in enumerate(("CHORD", "BASS", "ROOT", "TOP", "GATE", "CHANGE", "TENSION", "HARM")):
    add(text(n, 14 + i * (W - 28) / 7, 117.5 - 5.4, JCK, FB, CYAN if n == "HARM" else ONPLATE))
add(text("style chords  ·  voice-led  ·  HARM to Weighted  ·  Haer", W / 2, H - 2.4, 2.9, FM, QUIET))
open(os.path.join(HERE, "Changes.svg"), "w").write(
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">' + "".join(p) + "</svg>")
print("wrote Changes.svg")
