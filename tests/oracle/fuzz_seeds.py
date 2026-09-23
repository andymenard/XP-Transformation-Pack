#!/usr/bin/env python3
"""Write fuzz seeds: synthetic ones always, real ones when the corpus is here.

    fuzz_seeds.py SEEDDIR       -> SEEDDIR/{theme,dib,ini}/
"""
import os
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "app"))
sys.path.insert(0, str(HERE.parents[1] / "app" / "tests"))

import synth  # noqa: E402


def main(out: Path) -> None:
    from PIL import Image
    (out / "theme" / "synth.msstyles").write_bytes(synth.example())
    (out / "dib" / "synth32.dib").write_bytes(synth.dib(Image.new("RGBA", (5, 3), (1, 2, 3, 128))))
    (out / "dib" / "synth24.dib").write_bytes(synth.dib(Image.new("RGB", (7, 2), (200, 100, 0)), bpp=24))
    (out / "ini" / "synth.ini").write_bytes("[Button.PushButton(Hot)]\nTextColor=1 2 3 ; c\n".encode("utf-16le"))
    try:
        import fixtures
        from msstyles_parse import MsStyle
    except Exception:
        return
    # One of each DIB format the corpus uses, and a couple of real layouts.
    seen = set()
    for path in list(fixtures.OFFICIAL.values())[:2]:
        if not path.exists():
            continue
        style = MsStyle.load(str(path))
        for name, raw in style._bitmap_raw.items():
            fmt = struct.unpack_from("<HI", raw, 14)
            if fmt not in seen and len(raw) < 65536:
                seen.add(fmt)
                (out / "dib" / f"{name}.dib").write_bytes(raw)
        (out / "ini" / f"{path.stem}.ini").write_bytes(style._all_ini_texts[style._ini_resource_name].encode("utf-16le"))
    print(f"seeds: {len(os.listdir(out / 'dib'))} dib, {len(os.listdir(out / 'ini'))} ini, 1 theme")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
