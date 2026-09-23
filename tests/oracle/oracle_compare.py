#!/usr/bin/env python3
"""Put XPLasma's renderer next to real uxtheme, pixel for pixel.

    oracle_compare.py STYLE.msstyles [--job JOB] [--out DIR] [--cached]

Plans a job covering the whole style (xplasma-render --plan) unless --job is
given, runs it in the XP VM (refharness/oracle.py; needs refharness/vm.sh
running) and through xplasma-render, then compares every render composited
over black *and* over white -- exact equality, no tolerance -- and uxtheme's
metrics. --cached reuses DIR's earlier oracle results instead of asking XP
again. Mismatches get a magnified side-by-side under DIR/diff/.
"""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[2]
RENDER = ROOT / "build/dev-qt6/xplasma-render"
sys.path.insert(0, str(ROOT / "refharness"))


def metrics(path: Path) -> dict[str, dict[str, str]]:
    out = {}
    for line in path.read_text(encoding="latin-1").splitlines():
        if line.startswith("s") and " drawn=" in line:
            head, _, rest = line.partition(" drawn=")
            out[head] = dict(kv.split("=", 1) for kv in ("drawn=" + rest).split())
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("style")
    ap.add_argument("--job")
    ap.add_argument("--out", default=None)
    ap.add_argument("--cached", action="store_true")
    args = ap.parse_args()
    style = Path(args.style).resolve()
    name = style.name
    out = Path(args.out or f"/tmp/xplasma-oracle/{style.stem}")
    oracle_dir, ours_dir, diff_dir = out / "oracle", out / "ours", out / "diff"
    job = Path(args.job) if args.job else out / "job.txt"
    # --cached compares against XP's earlier answers, so it must keep the job
    # they answered: re-planning could change sizes under them.
    if not args.job and not (args.cached and job.exists()):
        out.mkdir(parents=True, exist_ok=True)
        job.write_text(subprocess.run([str(RENDER), "--plan", str(style), name], capture_output=True,
                                      text=True, check=True).stdout)
    if not args.cached:
        import oracle
        shutil.rmtree(oracle_dir, ignore_errors=True)
        oracle.run(job.read_text(), str(oracle_dir), [str(style)], timeout=900)
    shutil.rmtree(ours_dir, ignore_errors=True)
    shutil.rmtree(diff_dir, ignore_errors=True)
    subprocess.run([str(RENDER), str(job), str(ours_dir), "--style", f"{name}={style}"], check=True)
    diff_dir.mkdir(parents=True)

    theirs, ours = metrics(oracle_dir / "metrics.txt"), metrics(ours_dir / "metrics.txt")
    exact, wrong, by_part = 0, [], defaultdict(lambda: [0, 0])
    for key in theirs:
        if theirs[key].get("drawn") != "1":
            continue
        stem = key.replace(" ", "_", 4).replace(" ", "x", 1)
        part_key = " ".join(key.split()[1:3])
        bad = 0
        for bg in ("black", "white"):
            a = Image.open(oracle_dir / "raw" / f"{stem}_{bg}.bmp").convert("RGB")
            b_path = ours_dir / f"{stem}_{bg}.png"
            if not b_path.exists():
                bad = -1
                break
            b = Image.open(b_path).convert("RGB")
            bad += sum(1 for p in ImageChops.difference(a, b).getdata() if p != (0, 0, 0))
        by_part[part_key][0 if bad == 0 else 1] += 1
        if bad == 0:
            exact += 1
            continue
        wrong.append((bad, key))
        if bad > 0 and len(wrong) <= 60:
            a = Image.open(oracle_dir / "raw" / f"{stem}_white.bmp").convert("RGB")
            b = Image.open(ours_dir / f"{stem}_white.png").convert("RGB")
            d = ImageChops.difference(a, b).point(lambda v: 255 if v else 0)
            scale = max(1, min(8, 400 // max(a.width, 1)))
            sheet = Image.new("RGB", (a.width * 3 * scale + 8, a.height * scale), (255, 0, 255))
            for i, im in enumerate((a, b, d)):
                sheet.paste(im.resize((a.width * scale, a.height * scale), Image.NEAREST), (i * (a.width * scale + 4), 0))
            sheet.save(diff_dir / f"{stem}.png")
    metric_diffs = defaultdict(int)
    for key, want in theirs.items():
        got = ours.get(key, {})
        for field in ("min", "true", "draw", "content", "sizing", "contentmargins"):
            if want.get(field) != got.get(field):
                metric_diffs[field] += 1
    print(f"{style.name}: {exact}/{exact + len(wrong)} renders pixel-exact over black and white")
    for part_key, (ok, bad) in sorted(by_part.items()):
        if bad:
            print(f"   {part_key:24s} {ok:3d} exact, {bad:3d} differ")
    for field, n in sorted(metric_diffs.items()):
        print(f"   metric {field}: {n} differ")
    return 0 if not wrong else 1


if __name__ == "__main__":
    sys.exit(main())
