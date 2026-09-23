"""Apply the msstyles' declared fonts (CaptionFont, MenuFont, ...) to the
live Plasma session, using fonts extracted from XP install media."""
from __future__ import annotations

import hostexec as host

from msstyles_parse import MsStyle


def _parse_font_spec(value: str) -> tuple[str, int, bool]:
    """"Trebuchet MS, 10, bold" -> ("Trebuchet MS", 10, True)."""
    parts = [p.strip() for p in value.split(",")]
    family = parts[0]
    point = int(parts[1]) if len(parts) > 1 and parts[1].isdigit() else 10
    bold = any(p.lower() == "bold" for p in parts[2:])
    return family, point, bold


def _qt_font_string(family: str, point: int, bold: bool) -> str:
    weight = 700 if bold else 400
    # Matches the 16-field Qt6 QFont::toString() layout used by this system's
    # kdeglobals/kwinrc (family,pointSize,pixelSize,styleHint,weight,italic,
    # underline,strikeOut,fixedPitch,rawMode,+6 Qt6 extension fields).
    return f"{family},{point},-1,0,{weight},0,0,0,0,0,0,0,0,0,0,1"


def _write_and_broadcast(writes) -> None:
    """Write the font entries and make the running session pick them up.

    KWin's title-bar font and every Qt app's UI font come from the KDE
    platform theme, which only re-reads kdeglobals when it hears
    org.kde.KDEPlatformTheme.refreshFonts -- the signal the Fonts KCM sends.
    A KWin `reconfigure` on its own does nothing for fonts (verified live:
    the title bar kept the old face until this signal went out), which is
    why fonts used to look like they needed a re-login. --notify covers the
    KConfigWatcher-based listeners as well.
    """
    for file, group, key, value in writes:
        host.run(["kwriteconfig6", "--notify", "--file", file, "--group", group, "--key", key, value],
                 check=True)
    host.run(["dbus-send", "--session", "--type=signal", "/KDEPlatformTheme",
              "org.kde.KDEPlatformTheme.refreshFonts"], check=False)
    host.run([host.resolve("qdbus6", "qdbus"), "org.kde.KWin", "/KWin", "reconfigure"], check=False)


FONT_KEYS = (("kdeglobals", "WM", "activeFont"), ("kdeglobals", "WM", "inactiveFont"),
             ("kdeglobals", "General", "font"), ("kdeglobals", "General", "menuFont"))


def reset_fonts() -> None:
    """Drop every font entry xp2plasma writes, so Plasma falls back to its
    defaults. Deleting rather than writing a value: the default is whatever the
    platform theme decides, not a string we should guess."""
    for file, group, key in FONT_KEYS:
        host.run(["kwriteconfig6", "--notify", "--file", file, "--group", group, "--key", key, "--delete"],
                 check=False)
    host.run(["dbus-send", "--session", "--type=signal", "/KDEPlatformTheme",
              "org.kde.KDEPlatformTheme.refreshFonts"], check=False)
    host.run([host.resolve("qdbus6", "qdbus"), "org.kde.KWin", "/KWin", "reconfigure"], check=False)


def apply_font_specs(caption: tuple[str, int, bool], ui: tuple[str, int, bool]) -> None:
    """Apply explicit (family, point size, bold) fonts, e.g. for Windows Classic."""
    cap, gen = _qt_font_string(*caption), _qt_font_string(*ui)
    _write_and_broadcast((("kdeglobals", "WM", "activeFont", cap), ("kdeglobals", "WM", "inactiveFont", cap),
                          ("kdeglobals", "General", "font", gen), ("kdeglobals", "General", "menuFont", gen)))


def apply_fonts(theme: MsStyle) -> None:
    m = theme.effective("sysmetrics")
    writes = []
    if "captionfont" in m:
        family, point, bold = _parse_font_spec(m["captionfont"])
        font_str = _qt_font_string(family, point, bold)
        writes.append(("kdeglobals", "WM", "activeFont", font_str))
        writes.append(("kdeglobals", "WM", "inactiveFont", font_str))
    if "menufont" in m:
        family, point, bold = _parse_font_spec(m["menufont"])
        writes.append(("kdeglobals", "General", "menuFont", _qt_font_string(family, point, bold)))
    if "icontitlefont" in m:
        family, point, bold = _parse_font_spec(m["icontitlefont"])
        writes.append(("kdeglobals", "General", "font", _qt_font_string(family, point, bold)))

    _write_and_broadcast(writes)
