"""Put the desktop back to Plasma's default look: Breeze, the default panel
layout and the default wallpaper.

Used by `xplasma reset`, so leaving is as clean as arriving.
Plasma's own tool does the heavy lifting -- `plasma-apply-lookandfeel` applies
the global theme (decoration, colours, plasma theme, widget style, icons,
cursor) and `--resetLayout` rebuilds the default panel -- and the rest is
undoing what XPLasma added on top: the style plugin and its environment
hook, the XP font overrides, and the generated theme packages.

Everything goes through hostexec, so this behaves the same from a source
checkout or an installed copy.
The wallpaper is set explicitly from the global theme's own `defaults` file
rather than trusting the layout reset to do it: it usually does, but "usually"
is not what you want from an uninstaller.
"""
from __future__ import annotations

import os
import shutil
import time

import applied_state
import fonts_gen
import hostexec as host
import paths
import targets
from apply import AURORAE_THEMES_DIR, COLOR_SCHEMES_DIR, DESKTOP_THEMES_DIR
from start_applet import APPLET_ID, USER_PLASMOIDS

LOOK_AND_FEEL = "org.kde.breeze.desktop"
_LNF_DIRS = ("/usr/share/plasma/look-and-feel", "/usr/local/share/plasma/look-and-feel")
_WALLPAPER_DIRS = ("/usr/share/wallpapers", "/usr/local/share/wallpapers")
MARKER = "xp2plasma"          # what every generated package carries in its metadata


def _lnf_defaults(package: str) -> dict[str, str]:
    """The `[section][group] key=value` pairs of a global theme's defaults file."""
    text = None
    for base in _LNF_DIRS:
        text = host.read_text(f"{base}/{package}/contents/defaults")
        if text:
            break
    values: dict[str, str] = {}
    section = ""
    for line in (text or "").splitlines():
        line = line.strip()
        if line.startswith("["):
            section = line
        elif "=" in line and section:
            key, _, value = line.partition("=")
            values[f"{section}/{key.strip()}"] = value.strip()
    return values


def default_wallpaper(package: str = LOOK_AND_FEEL) -> str | None:
    """The global theme's wallpaper as a path Plasma's image plugin accepts: a
    wallpaper *package* directory (it picks the resolution and dark variant)."""
    name = _lnf_defaults(package).get("[Wallpaper]/Image") or "Next"
    if name.startswith("/"):
        return name if host.exists(name) else None
    for base in _WALLPAPER_DIRS:
        candidate = f"{base}/{name}"
        if host.exists(candidate):
            return candidate
    return None


def _set_wallpaper_all_desktops(path: str) -> None:
    script = (
        "var ds = desktops();"
        "for (var i = 0; i < ds.length; i++) {"
        "  var d = ds[i];"
        "  d.wallpaperPlugin = 'org.kde.image';"
        '  d.currentConfigGroup = ["Wallpaper", "org.kde.image", "General"];'
        f'  d.writeConfig("Image", {path!r});'
        "}"
    )
    host.run([host.resolve("qdbus6", "qdbus"), "org.kde.plasmashell", "/PlasmaShell",
              "org.kde.PlasmaShell.evaluateScript", script], check=False)


def _ours(metadata_path: str) -> bool:
    try:
        with open(metadata_path, encoding="utf-8", errors="replace") as fh:
            return MARKER in fh.read()
    except OSError:
        return False


def remove_generated_packages(progress=print) -> int:
    """Delete the theme packages xp2plasma generated -- and only those: each
    one carries the marker in its metadata, a user's own packages do not."""
    removed = 0
    import xp_icons
    removed += int(xp_icons.remove_theme())
    for base, meta_names in ((AURORAE_THEMES_DIR, ("metadata.desktop", "metadata.json")),
                             (DESKTOP_THEMES_DIR, ("metadata.json", "metadata.desktop"))):
        if not os.path.isdir(base):
            continue
        for entry in sorted(os.listdir(base)):
            pkg = os.path.join(base, entry)
            if os.path.isdir(pkg) and any(_ours(os.path.join(pkg, m)) for m in meta_names):
                shutil.rmtree(pkg, ignore_errors=True)
                removed += 1
    if os.path.isdir(COLOR_SCHEMES_DIR):
        for entry in sorted(os.listdir(COLOR_SCHEMES_DIR)):
            path = os.path.join(COLOR_SCHEMES_DIR, entry)
            if entry.endswith(".colors") and _ours(path):
                os.remove(path)
                removed += 1
    applet = os.path.join(USER_PLASMOIDS, APPLET_ID)
    for path in (applet, applet + ".new"):
        if os.path.isdir(path):
            shutil.rmtree(path, ignore_errors=True)
            removed += 1
    if removed:
        progress(f"Removed {removed} generated theme package(s).")
    return removed


def reset_desktop(progress=print, remove_packages: bool = True,
                  look_and_feel: str = LOOK_AND_FEEL) -> None:
    """Restore Breeze, the default panel layout and wallpaper, and undo
    xp2plasma's additions. Raises HostCommandError if Plasma's tool fails."""
    progress("Removing the XPLasma widget style and its environment hook ...")
    targets.revert(progress)
    import title_shadow
    title_shadow.remove()
    import panel_widgets
    panel_widgets.remove()
    host.check_cancelled()

    progress("Clearing the XP font overrides ...")
    fonts_gen.reset_fonts()
    host.check_cancelled()

    progress(f"Applying {look_and_feel} with the default panel layout ...")
    host.run(["plasma-apply-lookandfeel", "-a", look_and_feel, "--resetLayout"], check=True, capture=True)
    # The layout reset replaces the containments live; give plasmashell a
    # moment before writing wallpaper config into the new desktop containment.
    host.run(["systemctl", "--user", "restart", "plasma-plasmashell.service"], check=False)
    time.sleep(5)
    host.check_cancelled()

    wallpaper = default_wallpaper(look_and_feel)
    if wallpaper:
        progress(f"Setting the default wallpaper ({os.path.basename(wallpaper.rstrip('/'))}) ...")
        _set_wallpaper_all_desktops(wallpaper)
    else:
        progress("  the global theme's wallpaper isn't installed; leaving the wallpaper alone")

    if remove_packages:
        remove_generated_packages(progress)
    if os.path.exists(applied_state.STATE_PATH):
        os.remove(applied_state.STATE_PATH)
    # The Start-button images are only referenced by the (now removed) panel.
    for entry in os.listdir(paths.DATA) if os.path.isdir(paths.DATA) else []:
        if entry.startswith("start-") and entry.endswith(".png"):
            os.remove(os.path.join(paths.DATA, entry))
    host.run([host.resolve("qdbus6", "qdbus"), "org.kde.KWin", "/KWin", "reconfigure"], check=False)
    progress("Desktop restored to the default Plasma look.")
