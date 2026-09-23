#!/usr/bin/env python3
"""Build synthetic .msstyles files: real PE resource DLLs, no Microsoft art.

    from synth import msstyles, dib
    data = msstyles(
        inis={"NORMAL_INI": "[Button.PushButton]\\nImageFile=Test\\\\Button.bmp\\n"},
        bitmaps={"TEST_BUTTON_BMP": dib(Image.new("RGBA", (20, 23), "red"))},
        colour_names=["NormalColor"], size_names=["NormalSize"], file_names=["NORMAL_INI"])

The repository can never contain Microsoft's styles, so anything that must
run anywhere (unit tests, fuzz seeds, CI) builds its styles with this. The
layout is what packthem produces: TEXTFILE resources holding UTF-16LE INI text,
RT_BITMAP (type 2) resources holding bare DIBs, and the COLORNAMES / SIZENAMES
/ FILERESNAMES string lists.

    synth.py OUT.msstyles        writes a small example style (a fuzz seed)
"""
from __future__ import annotations

import struct
import sys

SECTION_RVA = 0x1000
FILE_ALIGN = 0x200


def dib(image, bpp: int = 32) -> bytes:
    """A bare DIB (BITMAPINFOHEADER + pixels, bottom-up) from a Pillow image.
    32bpp keeps the alpha channel; 24bpp drops it."""
    img = image.convert("RGBA")
    w, h = img.size
    stride = (w * (bpp // 8) + 3) & ~3
    rows = []
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            r, g, b, a = img.getpixel((x, y))
            row += bytes((b, g, r, a)) if bpp == 32 else bytes((b, g, r))
        rows.append(bytes(row).ljust(stride, b"\0"))
    header = struct.pack("<IiiHHIIiiII", 40, w, h, 1, bpp, 0, stride * h, 2835, 2835, 0, 0)
    return header + b"".join(rows)


def _string_list(items: list[str]) -> bytes:
    return "".join(s + "\0" for s in items).encode("utf-16le") + b"\0\0"


def msstyles(inis: dict[str, str], bitmaps: dict[str, bytes] | None = None,
             colour_names: list[str] | None = None, size_names: list[str] | None = None,
             file_names: list[str] | None = None, extra: dict[str, dict[str, bytes]] | None = None) -> bytes:
    """A PE32 resource DLL laid out like an XP-era .msstyles."""
    types: dict[object, dict[object, bytes]] = {}
    types["TEXTFILE"] = {name: text.encode("utf-16le") for name, text in inis.items()}
    if bitmaps:
        types[2] = dict(bitmaps)
    for key, values in (("COLORNAMES", colour_names), ("SIZENAMES", size_names), ("FILERESNAMES", file_names)):
        if values is not None:
            types[key] = {1: _string_list(values)}
    for key, entries in (extra or {}).items():
        types.setdefault(key, {}).update(entries)
    rsrc = _resource_section(types)
    return _pe(rsrc)


def _resource_section(types: dict[object, dict[object, bytes]]) -> bytes:
    """IMAGE_RESOURCE_DIRECTORY tree: type -> name -> language (0x409) -> data."""
    def ordered(keys):
        named = sorted((k for k in keys if isinstance(k, str)), key=str.upper)
        ids = sorted(k for k in keys if not isinstance(k, str))
        return named + ids

    # Pass 1: sizes. Directories first, then data entries, strings, blobs.
    n_types = len(types)
    n_names = sum(len(v) for v in types.values())
    dir_size = 16 + 8 * n_types + sum(16 + 8 * len(v) for v in types.values()) + n_names * (16 + 8)
    data_entries = dir_size
    strings_at = data_entries + 16 * n_names
    strings = bytearray()
    string_offsets: dict[str, int] = {}
    for t in ordered(types):
        for key in [t] + ordered(types[t]):
            if isinstance(key, str) and key not in string_offsets:
                string_offsets[key] = strings_at + len(strings)
                strings += struct.pack("<H", len(key)) + key.encode("utf-16le")
    blobs_at = (strings_at + len(strings) + 3) & ~3

    out = bytearray(blobs_at)
    blobs = bytearray()

    def entry_name(key) -> int:
        return (0x80000000 | string_offsets[key]) if isinstance(key, str) else key

    def directory(at: int, keys: list) -> None:
        named = sum(isinstance(k, str) for k in keys)
        struct.pack_into("<IIHHHH", out, at, 0, 0, 0, 0, named, len(keys) - named)

    # Pass 2: lay out root, type directories, name directories, languages.
    directory(0, ordered(types))
    cursor = 16 + 8 * n_types
    data_cursor = data_entries
    for i, t in enumerate(ordered(types)):
        names = ordered(types[t])
        struct.pack_into("<II", out, 16 + 8 * i, entry_name(t), 0x80000000 | cursor)
        type_dir = cursor
        directory(type_dir, names)
        cursor += 16 + 8 * len(names)
        for j, name in enumerate(names):
            struct.pack_into("<II", out, type_dir + 16 + 8 * j, entry_name(name), 0x80000000 | cursor)
            directory(cursor, [0x409])
            struct.pack_into("<II", out, cursor + 16, 0x409, data_cursor)
            cursor += 16 + 8
            payload = types[t][name]
            rva = SECTION_RVA + blobs_at + len(blobs)
            struct.pack_into("<IIII", out, data_cursor, rva, len(payload), 0, 0)
            data_cursor += 16
            blobs += payload + b"\0" * (-len(payload) % 4)
    out[strings_at:strings_at + len(strings)] = strings
    return bytes(out) + bytes(blobs)


def _pe(rsrc: bytes) -> bytes:
    raw_size = (len(rsrc) + FILE_ALIGN - 1) & ~(FILE_ALIGN - 1)
    dos = bytearray(64)
    dos[0:2] = b"MZ"
    struct.pack_into("<I", dos, 0x3C, 64)
    coff = struct.pack("<4sHHIIIHH", b"PE\0\0", 0x14C, 1, 0, 0, 0, 224, 0x2102)
    optional = bytearray(224)
    struct.pack_into("<H", optional, 0, 0x10B)                          # PE32
    struct.pack_into("<I", optional, 28, 0x10000000)                    # ImageBase
    struct.pack_into("<II", optional, 32, 0x1000, FILE_ALIGN)           # alignments
    struct.pack_into("<HH", optional, 40, 5, 1)                         # OS version 5.1
    struct.pack_into("<HH", optional, 48, 5, 1)                         # subsystem version
    struct.pack_into("<II", optional, 56, SECTION_RVA + ((len(rsrc) + 0xFFF) & ~0xFFF), FILE_ALIGN)
    struct.pack_into("<H", optional, 68, 2)                             # GUI subsystem
    struct.pack_into("<I", optional, 92, 16)                            # NumberOfRvaAndSizes
    struct.pack_into("<II", optional, 96 + 2 * 8, SECTION_RVA, len(rsrc))
    section = struct.pack("<8sIIIIIIHHI", b".rsrc", len(rsrc), SECTION_RVA, raw_size, FILE_ALIGN,
                          0, 0, 0, 0, 0x40000040)
    headers = bytes(dos) + coff + bytes(optional) + section
    return headers.ljust(FILE_ALIGN, b"\0") + rsrc.ljust(raw_size, b"\0")


def example() -> bytes:
    """A small but complete style: two variants, a bitmap, the name tables."""
    from PIL import Image
    button = Image.new("RGBA", (20, 23 * 5), (255, 0, 255, 255))
    for i, colour in enumerate(((236, 233, 216), (255, 200, 60), (160, 180, 220), (200, 200, 200), (40, 90, 200))):
        for y in range(23 * i + 1, 23 * i + 22):
            for x in range(1, 19):
                button.putpixel((x, y), colour + (255,))
    layout = ("[Globals]\nTextColor=0 0 0\n[SysMetrics]\nBtnFace=236 233 216\nWindow=255 255 255\n"
              "[Button.PushButton]\nImageFile=Synth\\Button.bmp\nImageCount=5\nImageLayout=Vertical\n"
              "SizingMargins=8, 8, 9, 9\nContentMargins=3, 3, 3, 3\nTransparent=True\n"
              "[Button.PushButton(Disabled)]\nTextColor=161 161 146\n"
              "[Toolbar(Pressed)]\nFillColor=1 2 3\n[Media::Toolbar.Button]\nTextColor=9 9 9\n")
    return msstyles(
        inis={"NORMALSYNTH_INI": layout, "LARGEFONTSSYNTH_INI": layout.replace("0 0 0", "1 1 1"),
              "THEMES_INI": "[documentation]\nDisplayName=Synthetic\n[ColorScheme.NormalColor]\nDisplayName=Synth\n"},
        bitmaps={"SYNTH_BUTTON_BMP": dib(button)},
        colour_names=["NormalColor"], size_names=["NormalSize", "LargeFonts"],
        file_names=["NORMALSYNTH_INI", "LARGEFONTSSYNTH_INI"])


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    with open(sys.argv[1], "wb") as fh:
        fh.write(example())
