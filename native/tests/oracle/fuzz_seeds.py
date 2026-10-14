#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Write fuzz seeds: synthetic ones always, real ones when the corpus is here.

    fuzz_seeds.py SEEDDIR       -> SEEDDIR/{theme,dib,ini}/
"""
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import synth  # noqa: E402

# The official styles, where setup imported them (XPLASMA_CORPUS overrides).
CORPUS = Path(os.environ.get("XPLASMA_CORPUS") or Path.home() / ".local/share/xplasma/assets/themes")
OFFICIAL = [CORPUS / "Luna" / "luna.msstyles", CORPUS / "Royale" / "Royale.msstyles"]


def resources(pe: bytes) -> dict[tuple[object, object], bytes]:
    """Every resource in a PE file, keyed (type, name): ints for IDs, strings for names."""
    lfanew = struct.unpack_from("<I", pe, 0x3C)[0]
    sections, optional = struct.unpack_from("<H", pe, lfanew + 6)[0], struct.unpack_from("<H", pe, lfanew + 20)[0]
    magic = struct.unpack_from("<H", pe, lfanew + 24)[0]
    rva = struct.unpack_from("<I", pe, lfanew + 24 + (96 if magic == 0x10B else 112) + 2 * 8)[0]
    table = lfanew + 24 + optional
    for i in range(sections):
        vaddr, size, raw = struct.unpack_from("<III", pe, table + 40 * i + 12)
        if vaddr <= rva < vaddr + max(size, 1):
            base, delta = raw + (rva - vaddr), vaddr - raw
            break
    else:
        return {}

    def entries(offset: int):
        named, ids = struct.unpack_from("<HH", pe, base + offset + 12)
        for k in range(named + ids):
            name, target = struct.unpack_from("<II", pe, base + offset + 16 + 8 * k)
            if name & 0x80000000:
                length = struct.unpack_from("<H", pe, base + (name & 0x7FFFFFFF))[0]
                name = pe[base + (name & 0x7FFFFFFF) + 2:][:2 * length].decode("utf-16le")
            yield name, target

    out = {}
    for kind, t in entries(0):
        for name, n in entries(t & 0x7FFFFFFF):
            for _, leaf in entries(n & 0x7FFFFFFF):
                at, size = struct.unpack_from("<II", pe, base + leaf)
                out[kind, name] = pe[at - delta:at - delta + size]
                break
    return out


def main(out: Path) -> None:
    from PIL import Image
    (out / "theme" / "synth.msstyles").write_bytes(synth.example())
    (out / "dib" / "synth32.dib").write_bytes(synth.dib(Image.new("RGBA", (5, 3), (1, 2, 3, 128))))
    (out / "dib" / "synth24.dib").write_bytes(synth.dib(Image.new("RGB", (7, 2), (200, 100, 0)), bpp=24))
    (out / "ini" / "synth.ini").write_bytes("[Button.PushButton(Hot)]\nTextColor=1 2 3 ; c\n".encode("utf-16le"))
    # One of each DIB format the corpus uses, and a couple of real layouts.
    seen = set()
    for path in OFFICIAL:
        if not path.exists():
            continue
        found = resources(path.read_bytes())
        for (kind, name), raw in found.items():
            if kind == 2 and len(raw) >= 16:
                fmt = struct.unpack_from("<HI", raw, 14)
                if fmt not in seen and len(raw) < 65536:
                    seen.add(fmt)
                    (out / "dib" / f"{name}.dib").write_bytes(raw)
        inis = sorted(name for (kind, name) in found if kind == "TEXTFILE" and str(name).endswith("_INI")
                      and name != "THEMES_INI")
        if inis:
            (out / "ini" / f"{path.stem}.ini").write_bytes(found["TEXTFILE", inis[0]])
    print(f"seeds: {len(os.listdir(out / 'dib'))} dib, {len(os.listdir(out / 'ini'))} ini, 1 theme")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
