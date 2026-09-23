"""Assets that live in Windows XP's own binaries rather than in the visual
style:

* the Start button flag, which explorer.exe carries as bitmap resource 143
  (25x20, real alpha);
* the monitor that Display Properties previews a desktop in, which themeui.dll
  carries as bitmap resource 126 (185x163, magenta used as the transparency
  colour). That DLL *is* the XP component that implements the dialog, so this
  is the same bitmap the real Display Properties draws.

Both are extracted from the install media the user pointed us at
(I386/EXPLORER.EX_, I386/THEMEUI.DL_) with cabextract, and cached beside that
media -- the extracted files are derived from the media, and keeping them
together means removing one removes the other.

None of this ships with the app: it is Microsoft's artwork, taken from media
the user owns, exactly like the fonts and the Luna style.
"""
from __future__ import annotations

import os

import pefile
from PIL import Image

import hostexec as host
import paths
from msstyles_parse import _dib_to_image

START_FLAG_BITMAP_ID = "143"
MONITOR_BITMAP_ID = "126"

# The screen area inside the monitor bitmap, and the one measurement in here
# that is not read out of a resource: bitmaps carry no name for it. Measured
# against the bezel -- scanning inward from each edge until the bezel's neutral
# greys give way to picture -- and checked by compositing an image into it. It
# is a rectangle with no rounding, so the wallpaper can be pasted straight in.
# preview_gen owns the compositing; this is only the number it needs.
MONITOR_SCREEN = (15, 17, 168, 131)     # left, top, right, bottom
MONITOR_TRANSPARENT = (255, 0, 255)


def _from_media(packed_name: str, cached_name: str) -> str | None:
    """One file unpacked out of the user's XP media, cached beside it."""
    cache_dir = paths.iso_cache()
    i386 = paths.iso_i386()
    if cache_dir is None or i386 is None:
        return None
    cached = os.path.join(cache_dir, cached_name)
    if os.path.isfile(cached):
        return cached
    packed = os.path.join(i386, packed_name)
    if not host.exists(packed):
        return None
    paths.ensure(cache_dir)
    try:
        host.run(["cabextract", "-q", "-d", cache_dir, packed], check=True, capture=True)
    except host.HostCommandError:
        return None
    return cached if os.path.isfile(cached) else None


def _explorer_exe() -> str | None:
    """explorer.exe, unpacked from the user's XP media on first use."""
    return _from_media("EXPLORER.EX_", "explorer.exe")


def _themeui_dll() -> str | None:
    """themeui.dll, unpacked from the user's XP media on first use."""
    return _from_media("THEMEUI.DL_", "themeui.dll")


def pe_bitmap(path: str, resource_id: str) -> Image.Image | None:
    pe = pefile.PE(path)
    pe.parse_data_directories()
    for t in getattr(pe, "DIRECTORY_ENTRY_RESOURCE", None).entries if hasattr(pe, "DIRECTORY_ENTRY_RESOURCE") else []:
        tname = str(t.name) if t.name is not None else str(t.struct.Id)
        if tname != "2":  # RT_BITMAP
            continue
        for e in t.directory.entries:
            rid = str(e.name) if e.name is not None else str(e.struct.Id)
            if rid != resource_id:
                continue
            lang = e.directory.entries[0]
            return _dib_to_image(pe.get_data(lang.data.struct.OffsetToData, lang.data.struct.Size))
    return None


def start_flag() -> Image.Image | None:
    exe = _explorer_exe()
    return pe_bitmap(exe, START_FLAG_BITMAP_ID) if exe else None


def monitor() -> Image.Image | None:
    """Display Properties' monitor, as RGB with its magenta left intact.

    Returning it raw rather than pre-keyed keeps the transparency decision in
    preview_gen, where the compositing happens; the caller that only wants the
    bezel takes the magenta out there.
    """
    dll = _themeui_dll()
    return pe_bitmap(dll, MONITOR_BITMAP_ID) if dll else None
