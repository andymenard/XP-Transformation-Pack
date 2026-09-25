#!/usr/bin/env python3
"""Hold XPlasma's Qt widgets against real XP controls of the same size.

    controls_compare.py XP_DIR QT_DIR [SHEET.png]

XP_DIR holds uxdump's control_*.bmp (refharness/oracle.py `control` lines),
QT_DIR xplasma-gallery --controls' control_*.png. Prints differing pixels per
control -- text included, since FreeType and GDI will never agree exactly, so
the number to watch is the art (see the sheet's diff column) -- and writes a
magnified XP | Qt | diff sheet.
"""
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw


def differing(diff: Image.Image) -> int:
    """Pixels where any channel of an RGB difference image is non-zero."""
    r, g, b = diff.split()
    return diff.width * diff.height - ImageChops.lighter(ImageChops.lighter(r, g), b).histogram()[0]


def main() -> int:
    xp_dir, qt_dir = Path(sys.argv[1]), Path(sys.argv[2])
    sheet_path = sys.argv[3] if len(sys.argv) > 3 else None
    rows = []
    for xp_file in sorted(xp_dir.glob("control_*.bmp")):
        qt_file = qt_dir / (xp_file.stem + ".png")
        a = Image.open(xp_file).convert("RGB")
        b = Image.open(qt_file).convert("RGB") if qt_file.exists() else Image.new("RGB", a.size, (255, 0, 255))
        diff = ImageChops.difference(a, b)
        bad = differing(diff)
        rows.append((xp_file.stem, a, b, diff, bad))
        print(f"{xp_file.stem:44s} {bad:6d} / {a.width * a.height} px differ")
    if sheet_path:
        k = 2
        width = max(r[1].width for r in rows) * k * 3 + 40
        height = sum(r[1].height * k + 14 for r in rows)
        sheet = Image.new("RGB", (width, height), (255, 255, 255))
        draw, y = ImageDraw.Draw(sheet), 0
        for name, a, b, diff, bad in rows:
            draw.text((2, y), f"{name}  ({bad} px)", fill=(0, 0, 0))
            y += 12
            mask = diff.point(lambda v: 255 if v else 0)
            for i, im in enumerate((a, b, mask)):
                sheet.paste(im.resize((a.width * k, a.height * k), Image.NEAREST), (i * (a.width * k + 12), y))
            y += a.height * k + 2
        sheet.save(sheet_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
