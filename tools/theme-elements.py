#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""theme-elements.py SVG OUT.png [PREFIX] [--zoom N] [--cols N]

The pieces of one of XPlasma's generated Plasma theme SVGs (its <image>
elements, the PNGs they embed), each zoomed and labelled with its id, over a
checkerboard so transparency shows. With PREFIX, only the ids starting so.
A developer's tool: needs Pillow.
"""
import argparse, base64, io, re
from PIL import Image, ImageDraw

p = argparse.ArgumentParser()
p.add_argument('svg')
p.add_argument('out')
p.add_argument('prefix', nargs='?', default='')
p.add_argument('--zoom', type=int, default=6)
p.add_argument('--cols', type=int, default=6)
a = p.parse_args()

text = open(a.svg).read()
cells = []
for m in re.finditer(r'<image id="([^"]+)"[^>]*?xlink:href="data:image/png;base64,([^"]+)"', text):
    name, data = m.group(1), m.group(2)
    if not name.startswith(a.prefix):
        continue
    im = Image.open(io.BytesIO(base64.b64decode(data))).convert('RGBA')
    im = im.resize((max(1, im.width * a.zoom), max(1, im.height * a.zoom)), Image.NEAREST)
    cells.append((name, im))
if not cells:
    raise SystemExit('no elements' + (f' starting {a.prefix}' if a.prefix else ''))

w = max(max(im.width for _, im in cells), 120) + 10
h = max(im.height for _, im in cells) + 18
rows = (len(cells) + a.cols - 1) // a.cols
sheet = Image.new('RGBA', (a.cols * w, rows * h), 'white')
draw = ImageDraw.Draw(sheet)
for i, (name, im) in enumerate(cells):
    x, y = (i % a.cols) * w, (i // a.cols) * h
    board = Image.new('RGBA', im.size, (200, 200, 200, 255))
    bd = ImageDraw.Draw(board)
    for by in range(0, im.height, 8):
        for bx in range(0, im.width, 8):
            if (bx // 8 + by // 8) % 2:
                bd.rectangle([bx, by, bx + 7, by + 7], fill=(255, 255, 255, 255))
    board.alpha_composite(im)
    sheet.paste(board, (x + 4, y + 16))
    draw.text((x + 4, y + 2), name, fill='black')
sheet.save(a.out)
print(a.out, len(cells), 'elements')
