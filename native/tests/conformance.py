#!/usr/bin/env python3
"""The C++ core must read every .msstyles exactly as xp2plasma's parser does.

    native/tests/conformance.py [--dump PATH/TO/xplasma-dump] [--jobs N] [STYLE ...]

Compares, per style: the variant tables, the default variant, every layout
INI's sections (both sides canonicalised to sub::class.part(state)) and the
SHA-256 of every bitmap's decoded RGBA. Without STYLE arguments it runs the
whole corpus: the official styles (tests/fixtures.CORPUS) plus the fan
collection (XPLASMA_FAN_CORPUS, default xp2plasma's parked copy).

Where the two disagree because the *Python* side was wrong, the fix belongs in
app/msstyles_parse.py too (PLAN.md D4) -- this script is how that's noticed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "app"))
sys.path.insert(0, str(ROOT / "app" / "tests"))

FAN_CORPUS = Path(os.environ.get("XPLASMA_FAN_CORPUS",
                                 Path.home() / "clod/xp2plasma/parked/ultimate/extracted"))
DUMP = ROOT / "native/build-dev-qt6/xplasma-dump"


def canonical(header: str) -> str:
    """The C++ Ini::parseHeader, in Python: sub::class.part(state), lower-case."""
    rest, state = header.strip(), ""
    if rest.endswith(")"):
        open_ = rest.rfind("(")
        if open_ > 0:
            state, rest = rest[open_ + 1:-1].strip().lower(), rest[:open_].strip()
    sub = ""
    if "::" in rest:
        sub, rest = rest.split("::", 1)
        sub = sub.strip().lower()
    cls, dot, part = rest.partition(".")
    cls, part = cls.strip().lower(), part.strip().lower() if dot else ""
    out = (sub + "::" if sub else "") + cls
    if part:
        out += "." + part
    if state:
        out += "(" + state + ")"
    return out


def python_view(path: str) -> dict:
    from msstyles_parse import MsStyle, _dib_to_image
    try:
        table = MsStyle.variant_table(path)
        default = MsStyle.load(path)
    except Exception as exc:          # noqa: BLE001 -- recorded, compared
        return {"error": f"{type(exc).__name__}: {exc}"}
    variants = {}
    for name in table["ini_names"]:
        if name == "THEMES_INI":
            continue
        style = default if name == default._ini_resource_name else MsStyle.load(path, name)
        sections: dict[str, dict] = {}
        for (cls, part, state), props in style.text_sections.items():
            header = cls + (f".{part}" if part else "") + (f"({state})" if state else "")
            sections.setdefault(canonical(header), {}).update(props)
        variants[name] = sections
    bitmaps = {}
    for name, raw in sorted(default._bitmap_raw.items()):
        try:
            img = _dib_to_image(raw)
        except Exception:             # noqa: BLE001
            bitmaps[name] = {"error": "undecodable"}
            continue
        rgba = img.convert("RGBA")
        bitmaps[name] = {"size": list(rgba.size), "alpha": bool(img.info.get("has_alpha")),
                         "sha256": hashlib.sha256(rgba.tobytes()).hexdigest()}
    formats = {name: _dib_format(raw) for name, raw in default._bitmap_raw.items()}
    return {"default": default._ini_resource_name, "iniNames": table["ini_names"],
            "colourNames": table["colour_names"], "sizeNames": table["size_names"],
            "fileNames": [n.upper() for n in table["file_names"]], "variants": variants,
            "bitmaps": bitmaps, "formats": formats}


def _dib_format(raw: bytes) -> tuple[int, int, int]:
    """(bpp, compression, colours used) from a DIB header."""
    import struct
    _size, _w, _h, _planes, bpp, compression = struct.unpack_from("<IiiHHI", raw, 0)
    return bpp, compression, struct.unpack_from("<I", raw, 32)[0]


# Bitmap formats where Pillow (Python) and the C++ core decode differently.
# Each was put to XP's own GDI through the oracle (refharness, `bitmap`
# directive) on 2026-09-23, and the C++ core matched GDI exactly every time:
# the Python side is what's wrong here. Everything else must match exactly.
def known_divergence(fmt: tuple[int, int, int]) -> str | None:
    bpp, compression, used = fmt
    if compression in (1, 2):
        return f"RLE{8 if compression == 1 else 4}: Pillow can't decode it; C++ matches XP's GDI (35/35 corpus bitmaps)"
    if bpp == 16 and compression == 0:
        return "16bpp 5-5-5: Pillow's channel expansion is off; C++ matches XP's GDI (replicated high bits)"
    if bpp <= 8 and used and used < (1 << bpp):
        return "short palette: Pillow reads a black/white palette as grey levels; C++ matches XP's GDI"
    return None


def cpp_view(path: str, dump: str) -> dict:
    out = subprocess.run([dump, path, "--all-variants"], capture_output=True, text=True,
                         env={**os.environ, "QT_QPA_PLATFORM": "offscreen"})
    if out.returncode not in (0, 1):
        return {"error": f"xplasma-dump crashed ({out.returncode}): {out.stderr[-300:]}"}
    return json.loads(out.stdout)


def compare(path: str, dump: str) -> tuple[list[str], list[str]]:
    """(differences, known divergences) for one style."""
    py, cpp = python_view(path), cpp_view(path, dump)
    if "error" in py or "error" in cpp:
        if "error" in py and "error" in cpp:
            return [], []             # both refuse it (e.g. a Vista-format style)
        return [f"only one side failed: python={py.get('error')} c++={cpp.get('error')}"], []
    problems, known = [], []
    for key in ("default", "iniNames", "colourNames", "sizeNames", "fileNames"):
        if py[key] != cpp[key]:
            problems.append(f"{key}: python={py[key]} c++={cpp[key]}")
    for name in sorted(set(py["variants"]) | set(cpp["variants"])):
        a, b = py["variants"].get(name, {}), cpp["variants"].get(name, {})
        for section in sorted(set(a) | set(b)):
            if a.get(section) != b.get(section):
                pa, pb = a.get(section, {}), b.get(section, {})
                keys = sorted(k for k in set(pa) | set(pb) if pa.get(k) != pb.get(k))
                problems.append(f"{name} [{section}] {', '.join(f'{k}: {pa.get(k)!r} vs {pb.get(k)!r}' for k in keys[:3])}"
                                if pa and pb else f"{name} [{section}] only in {'python' if pa else 'c++'}")
    for name in sorted(set(py["bitmaps"]) | set(cpp["bitmaps"])):
        if py["bitmaps"].get(name) != cpp["bitmaps"].get(name):
            why = known_divergence(py["formats"][name]) if name in py["formats"] else None
            (known if why else problems).append(
                f"bitmap {name}: {why}" if why else
                f"bitmap {name}: python={py['bitmaps'].get(name)} c++={cpp['bitmaps'].get(name)}")
    return problems, known


def corpus() -> list[str]:
    import fixtures
    styles = [str(p) for p in fixtures.OFFICIAL.values() if p.exists()]
    if FAN_CORPUS.is_dir():
        styles += sorted(str(p) for p in FAN_CORPUS.rglob("*") if p.suffix.lower() == ".msstyles")
    return styles


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("styles", nargs="*")
    ap.add_argument("--dump", default=str(DUMP))
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()
    styles = args.styles or corpus()
    failed, known_total = 0, {}
    with ProcessPoolExecutor(args.jobs) as pool:
        for path, (problems, known) in zip(styles, pool.map(compare, styles, [args.dump] * len(styles))):
            for k in known:
                known_total[k.split(": ", 1)[1]] = known_total.get(k.split(": ", 1)[1], 0) + 1
            if problems:
                failed += 1
                print(f"DIFF {os.path.basename(path)}  ({len(problems)} difference(s))")
                for p in problems[:8]:
                    print(f"     {p}")
    for why, count in sorted(known_total.items()):
        print(f"known  {count:3d} bitmap(s)  {why}")
    print(f"{len(styles) - failed}/{len(styles)} styles read identically (apart from known divergences)")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
