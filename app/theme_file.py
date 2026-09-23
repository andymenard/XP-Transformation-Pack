"""Windows XP .theme files: small INI documents (usually UTF-16LE with BOM)
that bundle a visual style with a wallpaper, cursors and shell icons, e.g.

    [VisualStyles]
    Path=%ResourceDir%\\Themes\\Foo\\Foo.msstyles
    [Control Panel\\Desktop]
    Wallpaper=%ResourceDir%\\Themes\\Foo\\wallpaper\\Foo.jpg

Windows-side paths are resolved by basename against the directory tree
around the .theme file, since bundles are never laid out the way the
absolute Windows paths claim.
"""
from __future__ import annotations

import glob
import os

import paths

MAX_SEARCH_DEPTH = 3
STOCK_WALLPAPERS = paths.WALLPAPERS


def _decode(raw: bytes) -> str:
    if raw.startswith(b"\xff\xfe") or raw.startswith(b"\xfe\xff"):
        return raw.decode("utf-16")
    if len(raw) > 1 and raw[1:2] == b"\x00":
        return raw.decode("utf-16le", errors="replace")
    try:
        return raw.decode("utf-8")
    except UnicodeDecodeError:
        return raw.decode("latin1")


def load_theme_file(path: str) -> dict[str, dict[str, str]]:
    """{section_lower: {key_lower: value}}; tolerant of comments/duplicates."""
    with open(path, "rb") as f:
        text = _decode(f.read())
    sections: dict[str, dict[str, str]] = {}
    current: dict[str, str] | None = None
    for raw in text.splitlines():
        line = raw.strip().lstrip("﻿")
        if not line or line.startswith(";"):
            continue
        if line.startswith("[") and line.endswith("]"):
            current = sections.setdefault(line[1:-1].strip().lower(), {})
            continue
        if current is None or "=" not in line:
            continue
        k, _, v = line.partition("=")
        current[k.strip().lower()] = v.strip()
    return sections


def win_basename(win_path: str) -> str:
    return win_path.replace("/", "\\").split("\\")[-1].strip()


def resolve_asset(theme_file: str, win_path: str, preferred_dirs: tuple[str, ...] = ()) -> str | None:
    """Find a local file matching the basename of a Windows-side path:
    the .theme's own directory tree first (bounded depth), then any
    preferred_dirs, then the parent tree (sibling bundles)."""
    # Locally saved presets can reference exact Linux paths. Do not reduce
    # those to a basename and accidentally select a different same-named file.
    if os.path.isabs(win_path):
        return win_path if os.path.isfile(win_path) else None
    target = win_basename(win_path).casefold()
    if not target:
        return None
    target_stem = os.path.splitext(target)[0]
    base = os.path.dirname(os.path.abspath(theme_file))
    # Collections often keep Foo.theme next to a Foo/ folder holding its
    # assets; that folder is the bundle proper and must win over siblings.
    theme_stem = os.path.splitext(os.path.basename(theme_file))[0].casefold()
    bundle_dirs = [os.path.join(base, d) for d in os.listdir(base)
                   if d.casefold() == theme_stem and os.path.isdir(os.path.join(base, d))]
    for root_dir in (*bundle_dirs, *preferred_dirs, base, os.path.dirname(base)):
        if not os.path.isdir(root_dir):
            continue
        # Stock copies may differ only by format (bliss.bmp vs bliss.jpg).
        stem_ok = root_dir in preferred_dirs
        root_depth = root_dir.rstrip(os.sep).count(os.sep)
        for dirpath, dirnames, filenames in os.walk(root_dir):
            if dirpath.count(os.sep) - root_depth >= MAX_SEARCH_DEPTH:
                dirnames[:] = []
            for fn in filenames:
                low = fn.casefold()
                if low == target or (stem_ok and os.path.splitext(low)[0] == target_stem):
                    return os.path.join(dirpath, fn)
    return None


def theme_msstyles_path(theme_file: str) -> str | None:
    sections = load_theme_file(theme_file)
    win_path = sections.get("visualstyles", {}).get("path")
    return resolve_asset(theme_file, win_path) if win_path else None


def theme_wallpaper_path(theme_file: str) -> str | None:
    sections = load_theme_file(theme_file)
    win_path = sections.get("control panel\\desktop", {}).get("wallpaper")
    if not win_path:
        return None
    # Stock XP wallpapers (bliss.jpg etc.) beat a same-named file from some
    # unrelated sibling bundle when the theme doesn't ship its own copy.
    return resolve_asset(theme_file, win_path, paths.wallpaper_roots())


# Plasma's org.kde.image FillMode uses Qt's Image.fillMode enum.
FILL_STRETCH, FILL_FIT, FILL_CROP, FILL_TILE, FILL_CENTER = 0, 1, 2, 3, 6

# Windows WallpaperStyle values (XP knows 0 and 2; later versions added the rest).
_WALLPAPER_STYLE_TO_FILL = {
    "0": FILL_CENTER,   # Center
    "2": FILL_STRETCH,  # Stretch
    "6": FILL_FIT,      # Fit (Win7+)
    "10": FILL_CROP,    # Fill (Win7+)
    "22": FILL_CROP,    # Span (Win8+) -- closest single-screen equivalent
}


def theme_wallpaper_fill_mode(theme_file: str) -> int | None:
    """Translate the .theme's wallpaper positioning to a Plasma FillMode, or
    None if it doesn't specify one we can map (caller should then leave the
    user's current positioning untouched)."""
    desktop = load_theme_file(theme_file).get("control panel\\desktop", {})
    if desktop.get("tilewallpaper", "0").strip() == "1":
        return FILL_TILE
    style = desktop.get("wallpaperstyle")
    if style is None:
        return None
    return _WALLPAPER_STYLE_TO_FILL.get(style.strip())


def find_theme_file_for(msstyles_path: str) -> str | None:
    """Locate a .theme in the same or parent directory whose [VisualStyles]
    Path points at this .msstyles (by basename)."""
    want = os.path.basename(msstyles_path).casefold()
    here = os.path.dirname(os.path.abspath(msstyles_path))
    candidates = []
    for d in (here, os.path.dirname(here)):
        candidates += glob.glob(os.path.join(d, "*.theme")) + glob.glob(os.path.join(d, "*.Theme"))
    for cand in sorted(set(candidates)):
        try:
            win_path = load_theme_file(cand).get("visualstyles", {}).get("path", "")
        except OSError:
            continue
        if win_basename(win_path).casefold() == want:
            return cand
    return None
