#!/usr/bin/env python3
"""Where each control's label sits, XP against Qt.

    text_boxes.py XP_DIR QT_DIR

Glyphs will never match between GDI and FreeType, but their placement can:
for every control, the bounding box of its text pixels (dark ones, inside
the region where the two renders differ) on each side, and the offset
between them. Zero offsets and similar sizes are the goal.
"""
import sys
from pathlib import Path

from PIL import Image


def dark_box(img, region):
    x0, y0, x1, y1 = region
    pts = [(x, y) for y in range(y0, y1 + 1) for x in range(x0, x1 + 1) if sum(img.getpixel((x, y))) < 330]
    if not pts:
        return None
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def main() -> int:
    xp_dir, qt_dir = Path(sys.argv[1]), Path(sys.argv[2])
    for xp_file in sorted(xp_dir.glob("control_*.bmp")):
        qt_file = qt_dir / (xp_file.stem + ".png")
        if not qt_file.exists():
            continue
        a, b = Image.open(xp_file).convert("RGB"), Image.open(qt_file).convert("RGB")
        diff = [(x, y) for y in range(a.height) for x in range(a.width) if a.getpixel((x, y)) != b.getpixel((x, y))]
        if not diff:
            print(f"{xp_file.stem:44s} identical")
            continue
        region = (min(p[0] for p in diff), min(p[1] for p in diff), max(p[0] for p in diff), max(p[1] for p in diff))
        ta, tb = dark_box(a, region), dark_box(b, region)
        if ta and tb:
            print(f"{xp_file.stem:44s} xp {ta}  qt {tb}  offset dx={tb[0] - ta[0]:+d} dy={tb[1] - ta[1]:+d}  "
                  f"size xp {ta[2] - ta[0] + 1}x{ta[3] - ta[1] + 1} qt {tb[2] - tb[0] + 1}x{tb[3] - tb[1] + 1}")
        else:
            print(f"{xp_file.stem:44s} differs in {region}, no text found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
