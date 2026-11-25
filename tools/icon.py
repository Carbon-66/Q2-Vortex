#!/usr/bin/env python3
"""The card's icons, drawn: rings of bricks running down a dark tube, a bat on the near rim and
the ball, in a rounded square. card/icon.png (52 px, Library's Games row) and card/icon-40.png
(40 px). Usage: tools/icon.py"""
import math, pathlib
from PIL import Image, ImageDraw

def icon(size):
    S = size * 8  # drawn large, then scaled down smooth
    bg = Image.new('RGB', (S, S), (6, 10, 22))
    d = ImageDraw.Draw(bg)
    cx, cy = S * 0.5, S * 0.5
    # the tube: rings closing in on a dark middle
    for k in range(14, 0, -1):
        r = S * 0.62 / (1 + k * 0.32)
        shade = int(30 + 60 / (1 + k * 0.5))
        d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=(shade, shade + 8, shade + 30), width=max(1, int(S * 0.008)))
    # three rings of bricks, near to far, each a run of coloured arcs with gaps
    colours = [(255, 150, 50), (255, 214, 64), (100, 228, 80), (64, 170, 255), (184, 112, 255)]
    for ring, (rad, w) in enumerate([(0.36, 0.075), (0.27, 0.058), (0.2, 0.044)]):
        r = S * rad
        for j in range(16):
            if (j + ring * 3) % 7 == 0:
                continue  # a brick knocked out
            a0, a1 = j * 22.5 + ring * 9 + 1.5, (j + 1) * 22.5 + ring * 9 - 1.5
            col = colours[(j // 3 + ring) % len(colours)]
            d.arc([cx - r, cy - r, cx + r, cy + r], a0, a1, fill=col, width=int(S * w))
    # the far end, glowing
    r = S * 0.08
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(30, 60, 120), outline=(120, 190, 255), width=int(S * 0.012))
    # the bat on the near rim, and the ball coming back to it
    r = S * 0.45
    d.arc([cx - r, cy - r, cx + r, cy + r], 52, 92, fill=(225, 232, 240), width=int(S * 0.06))
    d.arc([cx - r, cy - r, cx + r, cy + r], 52, 92, fill=(150, 160, 175), width=int(S * 0.02))
    bx, by, br = cx + S * 0.1, cy + S * 0.3, S * 0.045
    d.ellipse([bx - br * 2, by - br * 2, bx + br * 2, by + br * 2], fill=(40, 60, 110))
    d.ellipse([bx - br, by - br, bx + br, by + br], fill=(250, 252, 255))
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    mask = Image.new('L', (S, S), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=int(S * 0.22), fill=255)
    im.paste(bg, (0, 0), mask)
    return im.resize((size, size), Image.LANCZOS)

root = pathlib.Path(__file__).resolve().parent.parent
icon(52).save(root / 'card' / 'icon.png')
icon(40).save(root / 'card' / 'icon-40.png')
