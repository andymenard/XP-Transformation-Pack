#!/usr/bin/env python3
"""Every control job under every visual style: real XP against XPLasma.

    styles_matrix.py OUTDIR [--styles DIR] [--only LABEL ...] [--cached] [--qt 5|6]

For each style below, XP applies it for real (uxdump's `apply`: the visual
style and its system colours, as Display Properties would), captures
native/tests/controls-*.job, then puts Luna Silver back; xplasma-gallery
renders the same controls from the same .msstyles. Prints a table of exact
controls and differing pixels per style; OUTDIR/<label>/ keeps the XP
captures (oracle/), the Qt renders (qt<N>/) and a magnified sheet.

Needs refharness/vm.sh running, and the MS styles under --styles (default:
xp2plasma's asset directory). --cached reuses earlier XP captures.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "refharness"))
sys.path.insert(0, str(ROOT / "native/tests"))

import oracle  # noqa: E402
import vm as vmmod  # noqa: E402

# label, file under the styles dir, colour name, layout INI
STYLES = [
    ("luna-blue", "Luna/luna.msstyles", "NormalColor", "NORMALBLUE_INI"),
    ("luna-olive", "Luna/luna.msstyles", "HomeStead", "NORMALHOMESTEAD_INI"),
    ("luna-silver", "Luna/luna.msstyles", "Metallic", "NORMALMETALLIC_INI"),
    ("royale", "Royale/Royale.msstyles", "NormalColor", "NORMALROYALE_INI"),
    ("royale-noir", "Royale Noir/Royale Noir.msstyles", "Metallic", "NORMALDARKROYALE_INI"),
    ("zune", "Zune/Zune.msstyles", "NormalColor", "NORMALDEFAULT_INI"),
    ("embedded", "Embedded/Embedded.msstyles", "NormalColor", "NORMALDEFAULT_INI"),
    # Visual styles off, the scheme's colours from its .theme file.
    ("classic", "Windows Classic.theme", None, None),
    # ...and a synthetic scheme giving every colour role its own value.
    ("classic-synthetic", str(ROOT / "native/tests/schemes/Synthetic.theme"), None, None),
]

# [Control Panel\Colors] keys by COLOR_* index.
SYSCOLORS = ["Scrollbar", "Background", "ActiveTitle", "InactiveTitle", "Menu", "Window", "WindowFrame",
             "MenuText", "WindowText", "TitleText", "ActiveBorder", "InactiveBorder", "AppWorkspace", "Hilight",
             "HilightText", "ButtonFace", "ButtonShadow", "GrayText", "ButtonText", "InactiveTitleText",
             "ButtonHilight", "ButtonDkShadow", "ButtonLight", "InfoText", "InfoWindow", "ButtonAlternateFace",
             "HotTrackingColor", "GradientActiveTitle", "GradientInactiveTitle", "MenuHilight", "MenuBar"]


def scheme_lines(theme: Path) -> list[str]:
    """`syscolor` lines for a .theme file's [Control Panel\\Colors]."""
    colours, section = {}, None
    for raw in theme.read_bytes().decode("latin-1").splitlines():
        line = raw.strip()
        if line.startswith("["):
            section = line.lower()
        elif section == "[control panel\\colors]" and "=" in line:
            key, value = line.split("=", 1)
            colours[key.strip().lower()] = value.split()
    # (What the scheme leaves out, Windows' defaults -- not whatever the
    # style applied before happened to leave in the session.)
    colours.setdefault("hottrackingcolor", ["0", "0", "128"])
    lines = []
    # [Metrics] NonclientMetrics (NONCLIENTMETRICSA, decimal bytes): the
    # scheme's frame, caption, scroll and menu sizes and fonts.
    section, blob = None, None
    for raw in theme.read_bytes().decode("latin-1").splitlines():
        line = raw.strip()
        if line.startswith("["):
            section = line.lower()
        elif section == "[metrics]" and line.lower().startswith("nonclientmetrics="):
            blob = bytes(int(b) for b in line.split("=", 1)[1].split())
    if blob:
        lines.append("ncmetrics " + blob.hex())
    for index, name in enumerate(SYSCOLORS):
        rgb = colours.get(name.lower())
        if rgb and len(rgb) == 3:
            lines.append(f"syscolor {index} {rgb[0]} {rgb[1]} {rgb[2]}")
    return lines
RESTORE = r"apply C:\WINDOWS\Resources\Themes\Luna\luna.msstyles Metallic NormalSize"


def control_lines() -> list[str]:
    lines: list[str] = []
    for job in sorted((ROOT / "native/tests").glob("controls-*.job")):
        for line in job.read_text().splitlines():
            if line.startswith("control ") and line not in lines:
                lines.append(line)
    return lines


def compare(xp_dir: Path, qt_dir: Path) -> list[tuple[str, int, int]]:
    from PIL import Image, ImageChops
    from controls_compare import differing
    rows = []
    for xp_file in sorted(xp_dir.glob("control_*.bmp")):
        a = Image.open(xp_file).convert("RGB")
        qt_file = qt_dir / (xp_file.stem + ".png")
        if not qt_file.exists():
            rows.append((xp_file.stem, a.width * a.height, a.width * a.height))
            continue
        b = Image.open(qt_file).convert("RGB")
        bad = differing(ImageChops.difference(a, b))
        rows.append((xp_file.stem, bad, a.width * a.height))
    return rows


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("outdir")
    ap.add_argument("--styles", default=os.path.expanduser("~/.local/share/xp2plasma/assets/themes"))
    ap.add_argument("--only", nargs="*")
    ap.add_argument("--cached", action="store_true")
    ap.add_argument("--qt", default="6")
    args = ap.parse_args()
    out = Path(args.outdir)
    lines = control_lines()
    gallery = ROOT / f"native/build-dev-qt{args.qt}/xplasma-gallery"
    job_file = out / "controls.job"
    out.mkdir(parents=True, exist_ok=True)
    job_file.write_text("style -\n" + "\n".join(lines) + "\n")

    vm = None
    reset = False
    summary = []
    for label, rel, colour, variant in STYLES:
        if args.only and label not in args.only:
            continue
        path = Path(args.styles) / rel
        name = path.name
        xp_dir = out / label / "oracle"
        if not (args.cached and (xp_dir / "raw").is_dir()):
            vm = vm or vmmod.connect()
            if colour is not None and not reset:
                # A visual style keeps whatever window metrics the session
                # has, and a Classic scheme's (uxdump `ncmetrics`) can't be
                # undone without USER re-deriving the small icon size from
                # the caption's: start the styles from a freshly booted XP.
                # (Nothing is written to its registry, so a reset is one.)
                vm.command("system_reset")
                vmmod.wait_for_desktop(vm)
                reset = True
            if colour is None:
                job = "\n".join(["classic", *scheme_lines(path), *lines, RESTORE]) + "\n"
                carried = []
            else:
                job = "\n".join([f"apply {{style:{name}}} {colour} NormalSize", *lines, RESTORE]) + "\n"
                carried = [str(path)]
            oracle.run(job, str(xp_dir), carried, timeout=300, progress=lambda *_: None, vm=vm)
        qt_dir = out / label / f"qt{args.qt}"
        subprocess.run([str(gallery), "--controls", str(job_file), str(qt_dir)], check=True,
                       stderr=subprocess.DEVNULL,
                       env={**os.environ, "XPLASMA_XP_TEXT": "1", "QT_QPA_PLATFORM": "offscreen",
                            "QT_PLUGIN_PATH": str(ROOT / f"app/plugins/qt{args.qt}"),
                            "XPLASMA_THEME": f"{path}:{variant}" if variant else str(path)})
        rows = compare(xp_dir / "raw", qt_dir)
        exact = sum(1 for _, bad, _ in rows if bad == 0)
        summary.append((label, exact, len(rows), sum(bad for _, bad, _ in rows)))
        print(f"{label:12s} {exact:3d}/{len(rows)} exact, {summary[-1][3]:7d} px differ")
        for stem, bad, total in rows:
            if bad:
                print(f"    {stem:44s} {bad:6d} / {total}")
        subprocess.run([sys.executable, str(ROOT / "native/tests/controls_compare.py"), str(xp_dir / "raw"),
                        str(qt_dir), str(out / label / "sheet.png")], stdout=subprocess.DEVNULL)
    if vm:
        vm.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
