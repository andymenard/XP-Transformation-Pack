#!/usr/bin/env python3
"""Validate `xplasma-render --export` (the manifest a GTK generator will read)
for every Microsoft style.

    export_check.py [--styles DIR]

For each style: every image the manifest names exists at the size it
declares; every part carries resolved sizing and content margins (the
border-image slices) that fit its art; every part's states run 1..N as the
schema has them; the subclasses XP's own dialogs use (PlacesBar::Toolbar)
are there; [SysMetrics] came through. The pixels themselves are the renderer's at the part's true size,
which the oracle already holds exact against XP.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
RENDER = ROOT / "build/dev-qt6/xplasma-render"
STYLES = [
    ("Luna/luna.msstyles", "NORMALBLUE_INI"),
    ("Luna/luna.msstyles", "NORMALMETALLIC_INI"),
    ("Royale/Royale.msstyles", "NORMALROYALE_INI"),
    ("Royale Noir/Royale Noir.msstyles", "NORMALDARKROYALE_INI"),
    ("Zune/Zune.msstyles", "NORMALDEFAULT_INI"),
    ("Embedded/Embedded.msstyles", "NORMALDEFAULT_INI"),
]


def check(manifest_dir: Path) -> list[str]:
    problems = []
    m = json.loads((manifest_dir / "manifest.json").read_text())
    if m.get("format") != 1:
        problems.append(f"format {m.get('format')}")
    for key in ("captionbarheight", "scrollbarwidth", "btnface", "window"):
        if key not in m["sysmetrics"]:
            problems.append(f"sysmetrics lacks {key}")
    states: dict[tuple, list[int]] = {}
    for part in m["parts"]:
        name = f"{part['sub'] + '::' if part['sub'] else ''}{part['class']}.{part['part']}.{part['state'] or 0}"
        states.setdefault((part["sub"], part["class"], part["part"]), []).append(part["stateId"])
        if "96" not in part["images"]:
            problems.append(f"{name}: no 96 DPI image")
        for dpi, image in part["images"].items():
            path = manifest_dir / image["file"]
            if not path.exists():
                problems.append(f"{name}@{dpi}: {image['file']} missing")
                continue
            with Image.open(path) as im:
                if im.size != (image["width"], image["height"]) or im.mode != "RGBA":
                    problems.append(f"{name}@{dpi}: {im.size} {im.mode}, manifest says "
                                    f"{image['width']}x{image['height']}")
        for margins in ("sizingMargins", "contentMargins"):
            if sorted(part.get(margins, {})) != ["bottom", "left", "right", "top"]:
                problems.append(f"{name}: no resolved {margins}")
        # (A TrueSize part is drawn unscaled; its margins never apply.)
        if part["sizingMargins"]["left"] + part["sizingMargins"]["right"] > part["images"]["96"]["width"] \
                and part["properties"].get("bgtype", "").lower() == "imagefile" \
                and part["properties"].get("sizingtype", "").lower() != "truesize":
            problems.append(f"{name}: sizing margins wider than the art")
    for (sub, cls, part), ids in states.items():
        ids = sorted(ids)
        if ids != list(range(ids[0], ids[0] + len(ids))):
            problems.append(f"{sub}::{cls}.{part}: states {ids} not contiguous")
    if not any(p["sub"] == "placesbar" and p["class"] == "toolbar" for p in m["parts"]):
        problems.append("no PlacesBar::Toolbar")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--styles", default=os.path.expanduser("~/.local/share/xplasma/assets/themes"))
    args = ap.parse_args()
    failed = 0
    for rel, variant in STYLES:
        with tempfile.TemporaryDirectory() as out:
            run = subprocess.run([str(RENDER), "--export", str(Path(args.styles) / rel), variant, out],
                                 capture_output=True, text=True)
            if run.returncode:
                print(f"FAIL {rel} {variant}: {run.stderr.strip()}")
                failed += 1
                continue
            problems = check(Path(out))
            print(("ok  " if not problems else "FAIL") + f" {rel} {variant}: {run.stdout.strip().split(' ->')[0]}")
            for p in problems[:10]:
                print("     " + p)
            failed += bool(problems)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
