"""Where xp2plasma keeps its own files, and where it looks for the user's.

Two rules:

* Every path is built from ``expanduser("~")`` and never from an ``XDG_*``
  environment variable. What matters is writing where the *running desktop
  session* will look: Plasma reads ``~/.local/share`` and ``~/.config``, and it
  does not consult environment variables the app changed for itself. In an
  AppImage that distinction stops being theoretical, because the runtime
  prepends its own directories to the ``XDG_*_DIRS`` search paths -- an
  XDG-derived path can resolve into the mount, where a theme would install
  "successfully" and change nothing.
* Nothing is written inside the application directory. An AppImage mount is a
  read-only squashfs, so the old in-tree ``build/`` and ``cache/`` were not
  merely untidy, they raised OSError.

The KDE-facing install directories (aurorae themes, color-schemes,
desktoptheme, plasmoids, fonts, the Kvantum plugin dir) also have to be real
home paths for that same reason -- the session reads them. They stay with
the modules that own them; this one covers xp2plasma's own working files.

``HERE`` is still the right root for *shipped, read-only* data -- the classic
schemes table, the Kvantum build sources -- which works in a source checkout,
an installed copy and an AppImage mount alike.

The legacy fallbacks matter: an existing install (this machine, today) has its
themes in ``samples/`` and ``iso/cache`` inside the checkout. Those keep
working unchanged, so moving the working files to XDG locations is a no-op
rather than a migration the user has to perform.
"""
from __future__ import annotations

import json
import os
import sys

REAL_HOME = os.path.expanduser("~")
HERE = os.path.dirname(os.path.abspath(__file__))   # read-only when packaged

# --- xp2plasma's own files (~/.local/share/xp2plasma) -------------------------

DATA = os.path.join(REAL_HOME, ".local", "share", "xp2plasma")
ASSETS = os.path.join(DATA, "assets")
THEMES = os.path.join(ASSETS, "themes")            # the user's .msstyles / .theme library
WALLPAPERS = os.path.join(ASSETS, "wallpapers")
MEDIA = os.path.join(ASSETS, "media")              # XP install media the user pointed us at
SCHEMES = os.path.join(DATA, "schemes")
USER_SCHEMES = os.path.join(SCHEMES, "user")
BACKUP = os.path.join(DATA, "backup")
FONTS_XP = os.path.join(REAL_HOME, ".local", "share", "fonts", "xp")   # read by fontconfig

# --- regenerable caches (~/.cache/xp2plasma) ---------------------------------

CACHE = os.path.join(REAL_HOME, ".cache", "xp2plasma")
BUILD = os.path.join(CACHE, "build")               # generated theme packages
PREVIEW_CACHE = os.path.join(CACHE, "preview")
INDEX_CACHE = os.path.join(CACHE, "style_index.json")
KVANTUM_BUILD = os.path.join(CACHE, "kvantum-build")   # staged for the host-side build
KVANTUM_OUT = os.path.join(CACHE, "kvantum-out")

# --- read-only data that ships with the app ---------------------------------

def _shipped(*parts: str) -> str:
    """A shipped, read-only data path.

    Has to work from a source checkout, from an installed copy, and from inside
    the AppImage. In a checkout the data sits beside the modules; an installed
    copy (pip, or install.py) puts it under ``<prefix>/share/xp2plasma``; the
    AppImage also lays it out under ``sys.prefix/share/xp2plasma``, which is
    ``$APPDIR/usr`` there. One resolver covers all three.
    """
    candidates = (os.path.join(HERE, *parts),
                  os.path.join(sys.prefix, "share", "xp2plasma", *parts))
    return next((c for c in candidates if os.path.exists(c)), candidates[0])


SHIPPED_SCHEMES = _shipped("schemes", "xp_classic_schemes.json")
KVANTUM_SRC = _shipped("kvantum-xp")     # build.sh, patch.py, the Kvantum tarball

# --- legacy in-tree locations, still honoured if present --------------------

_LEGACY_THEME_ROOTS = (
    os.path.join(HERE, "samples"),
    os.path.join(HERE, "iso", "cache"),
)
_LEGACY_WALLPAPERS = os.path.join(HERE, "xp_wallpapers")
_LEGACY_EXTRACTED = os.path.join(HERE, "iso", "extracted")
_LEGACY_ISO_CACHE = os.path.join(HERE, "iso", "cache")

CONFIG = os.path.join(DATA, "config.json")


def ensure(path: str) -> str:
    """Create a working directory if needed and return it."""
    os.makedirs(path, exist_ok=True)
    return path


def theme_roots() -> list[str]:
    """Directories to scan for .theme / .msstyles files.

    The new asset library first, then whatever in-tree trees still exist, so an
    install that predates the move keeps finding its themes.
    """
    roots = [THEMES, ASSETS]
    roots.extend(_LEGACY_THEME_ROOTS)
    return [root for root in dict.fromkeys(roots) if os.path.isdir(root)]


def browse_root() -> str:
    """Start directory for the editor's "Browse Themes" picker."""
    return next(iter(theme_roots()), THEMES)


def wallpaper_roots() -> tuple[str, ...]:
    """Where to look for a .theme bundle's wallpaper image."""
    roots = [WALLPAPERS]
    if os.path.isdir(_LEGACY_WALLPAPERS):
        roots.append(_LEGACY_WALLPAPERS)
    return tuple(roots)


def load_config() -> dict:
    try:
        with open(CONFIG, encoding="utf-8") as fh:
            data = json.load(fh)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


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
    """The XP install media the user pointed us at (recorded by setup.py).

    Recorded by path rather than copied: an extracted XP ISO is ~600 MB and
    there is no reason to duplicate it into our own asset directory.
    """
    configured = load_config().get("media")
    for candidate in (configured, MEDIA, _LEGACY_EXTRACTED):
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
    if os.path.isdir(_LEGACY_ISO_CACHE):
        return _LEGACY_ISO_CACHE
    root = media_root()
    return os.path.join(root, "cache") if root else None


def stock_classic_theme() -> str | None:
    """XP's own "Windows Classic" appearance bundle, if we have it.

    Comes from the user's install media once setup.py has run; the in-tree copy
    is honoured so an existing install keeps working. With neither, the classic
    pipeline falls back to its built-in scheme table.
    """
    for candidate in (os.path.join(THEMES, "Windows Classic.theme"),
                      os.path.join(_LEGACY_ISO_CACHE, "Windows Classic.theme")):
        if os.path.exists(candidate):
            return candidate
    return None
