"""Parser for classic (pre-Vista) Windows XP .msstyles files.

These are PE files containing no code, only resources:
  - TEXTFILE resources hold plain UTF-16LE INI text (e.g. "NORMALROYALE_INI")
    describing classes/parts/states as sections like [Window.CloseButton] or
    [Window.Caption(active)], with keys like ImageFile, SizingMargins, etc.
  - Resource type "2" (RT_BITMAP) holds raw bitmaps referenced by ImageFile,
    named by uppercasing the path and replacing non-alnum chars with "_"
    (e.g. "Royale\\FrameCaption.bmp" -> "ROYALE_FRAMECAPTION_BMP"). These are
    bare DIBs (BITMAPINFOHEADER + pixels, no BITMAPFILEHEADER).

No binary VMAP/CMAP property-record parsing is needed for this era of file;
the layout data is literal text.
"""
from __future__ import annotations

import io
import re
import struct
from dataclasses import dataclass, field

import pefile
from PIL import Image

RT_BITMAP = "2"
RT_TEXTFILE = "TEXTFILE"

_SECTION_RE = re.compile(r"^\[(.+)\]$")
_STATE_RE = re.compile(r"^(.*)\((.+)\)$")


def _decode_bgra_dib(data: bytes, header_size: int, width: int, height: int) -> Image.Image | None:
    """32bpp BI_RGB DIBs carry a real per-pixel alpha channel (used for
    antialiased glyphs like the white close-button X), which Pillow's BMP
    loader silently discards. Decode the pixel rows ourselves instead."""
    h = abs(height)
    n = width * h * 4
    pixels = data[header_size : header_size + n]
    if len(pixels) < n:
        return None
    orientation = 1 if height < 0 else -1
    img = Image.frombytes("RGBA", (width, h), pixels, "raw", "BGRA", 0, orientation)
    _, hi = img.getchannel("A").getextrema()
    if hi == 0:
        img.putalpha(255)  # alpha byte unused in this file; treat as opaque
    else:
        img.info["has_alpha"] = True
    return img


def _dib_to_image(data: bytes) -> Image.Image:
    """Decode a bare DIB (as stored in msstyles bitmap resources). 32bpp
    images are decoded directly to keep their alpha; everything else is
    wrapped in a synthetic BITMAPFILEHEADER so Pillow can decode it."""
    header_size, width, height, _planes, bpp, compression = struct.unpack_from("<IiiHHI", data, 0)
    if bpp == 32 and compression == 0:
        img = _decode_bgra_dib(data, header_size, width, height)
        if img is not None:
            return img
    clr_used = struct.unpack_from("<I", data, 32)[0]
    palette_colors = (clr_used or (1 << bpp)) if bpp <= 8 else 0
    palette_size = palette_colors * 4
    pixel_offset = 14 + header_size + palette_size
    file_size = pixel_offset + (len(data) - header_size - palette_size)
    file_header = b"BM" + struct.pack("<IHHI", file_size, 0, 0, pixel_offset)
    return Image.open(io.BytesIO(file_header + data)).convert("RGBA")


def _resource_key_for_path(path: str) -> str:
    """Reproduce the PE resource name msstyles derives from an ImageFile path."""
    return re.sub(r"[^A-Za-z0-9]", "_", path).upper()


def _iter_pe_resources(pe: pefile.PE, want_data=True):
    """Walk the resource tree. `want_data` is True, False, or a set of resource
    type names whose bytes are wanted; anything else yields b"" -- so a caller
    after a couple of tiny tables never pays for the bitmaps."""
    if not hasattr(pe, "DIRECTORY_ENTRY_RESOURCE"):
        return
    for type_entry in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        type_name = str(type_entry.name) if type_entry.name is not None else str(type_entry.struct.Id)
        if not hasattr(type_entry, "directory"):
            continue
        for name_entry in type_entry.directory.entries:
            res_name = str(name_entry.name) if name_entry.name is not None else str(name_entry.struct.Id)
            if not hasattr(name_entry, "directory"):
                continue
            if want_data is False or (want_data is not True and type_name not in want_data):
                yield type_name, res_name, b""
                continue
            lang_entry = name_entry.directory.entries[0]
            rva = lang_entry.data.struct.OffsetToData
            size = lang_entry.data.struct.Size
            data = pe.get_data(rva, size)
            yield type_name, res_name, data


def _parse_ini_text(text: str) -> dict[tuple[str, str, str], dict[str, str]]:
    """Parse a msstyles TEXTFILE INI blob into
    {(class_lower, part_lower, state_lower): {key_lower: raw_value}}.
    A section with no part is keyed ('class', '', ''); no state is ('class','part','').
    Tolerant of duplicate keys/sections and inline ';' comments, since
    real-world theme files are not always strictly well-formed.
    """
    sections: dict[tuple[str, str, str], dict[str, str]] = {}
    current: dict[str, str] | None = None
    for raw_line in text.splitlines():
        # Comments first, as uxtheme does: a section header can carry one too
        # ("[ExplorerBar.HeaderBackground]; Not used in IE 6"), and read as a
        # key line its section's keys would land in the previous section.
        # Values themselves never contain ';'. (Found by XPLasma's XP oracle.)
        line = raw_line.split(";", 1)[0].strip()
        if not line:
            continue
        m = _SECTION_RE.match(line)
        if m:
            header = m.group(1).strip()
            cls, _, rest = header.partition(".")
            part, state = rest, ""
            sm = _STATE_RE.match(rest)
            if sm:
                part, state = sm.group(1), sm.group(2)
            key = (cls.lower(), part.lower(), state.lower())
            current = sections.setdefault(key, {})
            continue
        if current is None or "=" not in line:
            continue
        key_part, _, value = line.partition("=")
        current[key_part.strip().lower()] = value.strip()
    return sections


@dataclass
class MsStyle:
    path: str
    text_sections: dict[tuple[str, str, str], dict[str, str]] = field(default_factory=dict)
    _bitmap_cache: dict[str, Image.Image] = field(default_factory=dict)
    _bitmap_raw: dict[str, bytes] = field(default_factory=dict)

    @classmethod
    def _open(cls, path: str) -> "pefile.PE":
        """Open the PE and parse *only* the resource directory.

        pefile's full load parses every data directory -- imports, relocations,
        debug -- which on a 20 MB msstyles costs ~1.5s to reach a resource table
        that takes 0.01s to walk. fast_load skips all of it.
        """
        pe = pefile.PE(path, fast_load=True)
        pe.parse_data_directories(
            directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
        return pe

    @classmethod
    def ini_resource_names(cls, path: str) -> list[str]:
        """Every TEXTFILE resource name, without decoding any of them.

        This is what "Color scheme" and "Font size" are built from, and it has
        to run over every .msstyles on disk, so it reads resource *names* only.
        """
        pe = cls._open(path)
        try:
            return sorted({res_name.upper() for type_name, res_name, _data
                           in _iter_pe_resources(pe, want_data=False)
                           if type_name == RT_TEXTFILE})
        finally:
            pe.close()

    @classmethod
    def variant_table(cls, path: str) -> dict:
        """The style's own variant bookkeeping, without decoding any layout INI.

        packthem writes three UTF-16 lists: COLORNAMES (the scheme names a
        .theme's ColorStyle= uses, e.g. "NormalColor"), SIZENAMES ("NormalSize")
        and FILERESNAMES -- the INI resource for every (colour, size) pair,
        colour-major -- plus THEMES_INI, whose [ColorScheme.X]/[Size.X]
        sections carry the display names XP shows ("Default (blue)").
        """
        want = {RT_TEXTFILE, "COLORNAMES", "SIZENAMES", "FILERESNAMES"}
        out = {"ini_names": [], "colour_names": [], "size_names": [], "file_names": [], "themes_ini": None}
        pe = cls._open(path)
        try:
            for type_name, res_name, data in _iter_pe_resources(pe, want_data=want):
                if type_name == RT_TEXTFILE:
                    out["ini_names"].append(res_name.upper())
                    if res_name.upper() == "THEMES_INI":
                        try:
                            out["themes_ini"] = data.decode("utf-16le")
                        except UnicodeDecodeError:
                            out["themes_ini"] = data.decode("latin1")
                elif type_name in ("COLORNAMES", "SIZENAMES", "FILERESNAMES"):
                    key = {"COLORNAMES": "colour_names", "SIZENAMES": "size_names",
                           "FILERESNAMES": "file_names"}[type_name]
                    out[key] = [s for s in data.decode("utf-16le", "replace").split("\0") if s]
        finally:
            pe.close()
        out["ini_names"].sort()
        return out

    @classmethod
    def load(cls, path: str, ini_resource_name: str | None = None) -> "MsStyle":
        pe = cls._open(path)
        bitmap_raw: dict[str, bytes] = {}
        ini_texts: dict[str, str] = {}
        file_names: list[str] = []
        for type_name, res_name, data in _iter_pe_resources(pe):
            if type_name == RT_BITMAP:
                bitmap_raw[res_name.upper()] = data
            elif type_name == "FILERESNAMES":
                file_names = [s.upper() for s in data.decode("utf-16le", "replace").split("\0") if s]
            elif type_name == RT_TEXTFILE:
                try:
                    ini_texts[res_name.upper()] = data.decode("utf-16le")
                except UnicodeDecodeError:
                    ini_texts[res_name.upper()] = data.decode("latin1")
        pe.close()

        if not ini_texts:
            raise ValueError(
                f"{path!r} has no TEXTFILE resources -- this parser only supports "
                "classic pre-Vista XP msstyles (plaintext INI layout data). Files "
                "using CMAP/VMAP/VARIANT binary property records are Vista/Longhorn/7+ "
                "era visual styles in the newer VSTYLE format, which is a different, "
                "unsupported format."
            )

        if ini_resource_name is None:
            # XP's default variant is the first colour at the first size, which
            # is FILERESNAMES[0] -- *not* the alphabetically first NORMAL*_INI
            # (that made "Royale Noir.msstyles" default to DarkRoyale, where XP
            # opens it as Royale). Fall back to any NORMAL* variant, then the
            # largest TEXTFILE (the layout INI dwarfs THEMES_INI).
            candidates = [n for n in file_names if n in ini_texts]
            if not candidates:
                candidates = [n for n in ini_texts if n.startswith("NORMAL")]
            if not candidates:
                candidates = sorted(ini_texts, key=lambda n: len(ini_texts[n]), reverse=True)
            ini_resource_name = candidates[0]

        text_sections = _parse_ini_text(ini_texts[ini_resource_name])
        inst = cls(path=path, text_sections=text_sections)
        inst._bitmap_raw = bitmap_raw
        inst._ini_resource_name = ini_resource_name  # type: ignore[attr-defined]
        inst._all_ini_texts = ini_texts  # type: ignore[attr-defined]
        return inst

    def effective(self, cls: str, part: str = "", state: str = "") -> dict[str, str]:
        """Merge class-level, part-level, and state-level properties (later
        entries override earlier), matching how uxtheme cascades lookups."""
        out: dict[str, str] = {}
        out.update(self.text_sections.get((cls.lower(), "", ""), {}))
        out.update(self.text_sections.get((cls.lower(), part.lower(), ""), {}))
        if state:
            out.update(self.text_sections.get((cls.lower(), part.lower(), state.lower()), {}))
        return out

    def parts(self, cls: str) -> set[str]:
        return {p for (c, p, s) in self.text_sections if c == cls.lower() and p}

    def get_bitmap(self, image_file: str) -> Image.Image:
        key = _resource_key_for_path(image_file)
        if key not in self._bitmap_cache:
            raw = self._bitmap_raw.get(key)
            if raw is None:
                raise KeyError(f"No bitmap resource for ImageFile={image_file!r} (looked for {key})")
            self._bitmap_cache[key] = _dib_to_image(raw)
        return self._bitmap_cache[key]

    def get_background_bitmap(self, cls: str, part: str) -> Image.Image | None:
        """Fetch a part's background ImageFile with colour-key transparency
        applied per its Transparent/TransparentColor properties. Use this
        (rather than get_bitmap directly) for any BgType=ImageFile part --
        window frames, buttons, panels, explorer-bar chrome, etc. -- since
        rounded/cut-out corners are near-universally painted this way."""
        eff = self.effective(cls, part)
        if "imagefile" not in eff:
            return None
        img = self.get_bitmap(eff["imagefile"])
        if img.info.get("has_alpha"):
            return img  # real per-pixel alpha wins over colour-keying
        if eff.get("transparent", "").lower() == "true":
            key = parse_color(eff["transparentcolor"]) if "transparentcolor" in eff else DEFAULT_TRANSPARENT_COLOR
            img = apply_colorkey(img, key)
        return img


DEFAULT_TRANSPARENT_COLOR = (255, 0, 255)  # classic magenta colour-key


def apply_colorkey(img: Image.Image, key: tuple[int, int, int]) -> Image.Image:
    """Cut a hard (non-anti-aliased) transparent hole wherever img matches
    key exactly, matching classic GDI transparent-blt semantics used by
    pre-Vista msstyles background bitmaps (rounded corners etc. are painted
    with a magic colour, magenta by default, that the engine keys out)."""
    img = img.convert("RGBA")
    out = Image.new("RGBA", img.size)
    out.putdata([(r, g, b, 0) if (r, g, b) == key else (r, g, b, a) for (r, g, b, a) in img.getdata()])
    return out


def parse_color(value: str) -> tuple[int, int, int]:
    parts = [int(p) for p in value.replace(",", " ").split()]
    r, g, b = parts[:3]
    return (r, g, b)


def parse_margins(value: str) -> tuple[int, int, int, int]:
    """Returns (left, right, top, bottom), matching the Win32 MARGINS struct
    order (cxLeftWidth, cxRightWidth, cyTopHeight, cyBottomHeight) that
    TMT_SIZINGMARGINS values use."""
    parts = [int(p.strip()) for p in value.split(",")]
    left, right, top, bottom = parts[:4]
    return (left, right, top, bottom)
