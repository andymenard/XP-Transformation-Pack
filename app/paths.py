"""Where XPLasma keeps its own files, and where it looks for the user's.

Two rules:

* Every path is built from ``expanduser("~")`` and never from an ``XDG_*``
  environment variable. What matters is writing where the *running desktop
  session* will look: Plasma reads ``~/.local/share`` and ``~/.config``, and it
  does not consult environment variables the app changed for itself. (A
  bundled runtime, such as the AppImage xp2plasma shipped as, prepends its own
  directories to ``XDG_*_DIRS``; a theme written to an XDG-derived path could
  then install "successfully" into the bundle and change nothing.)
* Nothing is written inside the application directory, which may be
  read-only once installed.

The KDE-facing install directories (aurorae themes, color-schemes,
desktoptheme, plasmoids, fonts, Qt style plugins) also have to be real home
paths for that same reason -- the session reads them. They stay with the
modules that own them; this one covers XPLasma's own working files.

``HERE`` is the root for *shipped, read-only* data (the classic schemes
table), which works in a source checkout and an installed copy alike.
"""
from __future__ import annotations

import json
import os
import sys

REAL_HOME = os.path.expanduser("~")
HERE = os.path.dirname(os.path.abspath(__file__))   # read-only when packaged

# --- xp2plasma, XPLasma's predecessor -----------------------------------------
#
# XPLasma adopts what xp2plasma already put on this machine rather than asking
# for it again: the imported asset library (fonts, styles, wallpapers, icons --
# ~100 MB, taken from ~600 MB of downloaded XP media), the user's saved classic
# schemes, and the download cache. Each is used from xp2plasma's location only
# while XPLasma has no copy of its own, so a fresh machine gets clean XPLasma
# paths and an upgraded one loses nothing.

LEGACY_DATA = os.path.join(REAL_HOME, ".local", "share", "xp2plasma")
LEGACY_CACHE = os.path.join(REAL_HOME, ".cache", "xp2plasma")


def _adopt(own: str, legacy: str) -> str:
    """`own` if it exists or there is nothing to adopt, else `legacy`."""
    return legacy if not os.path.exists(own) and os.path.exists(legacy) else own


# --- XPLasma's own files (~/.local/share/xplasma) -----------------------------

DATA = os.path.join(REAL_HOME, ".local", "share", "xplasma")
ASSETS = _adopt(os.path.join(DATA, "assets"), os.path.join(LEGACY_DATA, "assets"))
THEMES = os.path.join(ASSETS, "themes")            # the user's .msstyles / .theme library
WALLPAPERS = os.path.join(ASSETS, "wallpapers")
MEDIA = os.path.join(ASSETS, "media")              # XP install media the user pointed us at
SCHEMES = _adopt(os.path.join(DATA, "schemes"), os.path.join(LEGACY_DATA, "schemes"))
USER_SCHEMES = os.path.join(SCHEMES, "user")
BACKUP = os.path.join(DATA, "backup")
FONTS_XP = os.path.join(REAL_HOME, ".local", "share", "fonts", "xp")   # read by fontconfig

# --- regenerable caches (~/.cache/xplasma) ------------------------------------

CACHE = os.path.join(REAL_HOME, ".cache", "xplasma")
# Not regenerable in practice: the XP media is a ~589 MB one-time fetch.
DOWNLOADS = _adopt(os.path.join(CACHE, "downloads"), os.path.join(LEGACY_CACHE, "downloads"))
BUILD = os.path.join(CACHE, "build")               # generated theme packages
PREVIEW_CACHE = os.path.join(CACHE, "preview")
INDEX_CACHE = os.path.join(CACHE, "style_index.json")

# --- read-only data that ships with the app ---------------------------------

def _shipped(*parts: str) -> str:
    """A shipped, read-only data path: beside the modules in a source
    checkout, or under ``<prefix>/share/xplasma`` in an installed copy."""
    candidates = (os.path.join(HERE, *parts),
                  os.path.join(sys.prefix, "share", "xplasma", *parts))
    return next((c for c in candidates if os.path.exists(c)), candidates[0])


SHIPPED_SCHEMES = _shipped("schemes", "xp_classic_schemes.json")

CONFIG = os.path.join(DATA, "config.json")
LEGACY_CONFIG = os.path.join(LEGACY_DATA, "config.json")


def ensure(path: str) -> str:
    """Create a working directory if needed and return it."""
    os.makedirs(path, exist_ok=True)
    return path


def theme_roots() -> list[str]:
    """Directories to scan for .theme / .msstyles files."""
    return [root for root in dict.fromkeys((THEMES, ASSETS)) if os.path.isdir(root)]


def browse_root() -> str:
    """Start directory for the editor's "Browse Themes" picker."""
    return next(iter(theme_roots()), THEMES)


def wallpaper_roots() -> tuple[str, ...]:
    """Where to look for a .theme bundle's wallpaper image."""
    return (WALLPAPERS,)


def _read_json(path: str) -> dict:
    try:
        with open(path, encoding="utf-8") as fh:
            data = json.load(fh)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def load_config() -> dict:
    """Setup choices. Until XPLasma has saved any, xp2plasma's are used, so
    the media it was pointed at is still found."""
    return _read_json(CONFIG) if os.path.exists(CONFIG) else _read_json(LEGACY_CONFIG)


def save_config(**fields) -> None:
    """Record setup choices (notably where the user's XP media lives).
    Never fatal, like applied_state.save."""
    try:
        ensure(DATA)
        # A None value removes the key -- how a rollback puts a setting back
        # to "never set".
        data = {k: v for k, v in {**load_config(), **fields}.items() if v is not None}
        tmp = f"{CONFIG}.tmp"
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(data, fh, indent=2)
        os.replace(tmp, CONFIG)
    except OSError:
        pass


def media_root() -> str | None:
    """The XP install media the user pointed us at (recorded by setup).

    Recorded by path rather than copied: an extracted XP ISO is ~600 MB and
    there is no reason to duplicate it into our own asset directory.
    """
    configured = load_config().get("media")
    for candidate in (configured, MEDIA):
        if candidate and os.path.isdir(candidate):
            return candidate
    return None


def iso_i386() -> str | None:
    """The I386 directory inside that media, if it really looks like one."""
    root = media_root()
    if root is None:
        return None
    for candidate in (os.path.join(root, "I386"), root):
        if any(os.path.isfile(os.path.join(candidate, name))
               for name in ("EXPLORER.EX_", "EXPLORER.EXE")):
            return candidate
    return None


def iso_cache() -> str | None:
    """Scratch where cabextract unpacks explorer.exe.

    Deliberately beside the media rather than under ~/.cache: it holds files
    unpacked *from* that media, including a full explorer.exe, and it should be
    thrown away with the media it came from rather than outliving it.
    """
    root = media_root()
    return os.path.join(root, "cache") if root else None


def stock_classic_theme() -> str | None:
    """XP's own "Windows Classic" appearance bundle, if setup imported it.
    Without it, the classic pipeline falls back to its built-in scheme table."""
    candidate = os.path.join(THEMES, "Windows Classic.theme")
    return candidate if os.path.exists(candidate) else None
