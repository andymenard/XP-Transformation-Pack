"""Install and switch the Qt widget style for the live Plasma session.

Visual styles are drawn by **XPLasma's own Qt style plugin**, which reads the
``.msstyles`` itself -- there is no generated intermediate theme. This module's
job is only to put the plugin where Qt looks, tell it which style to draw, and
switch the session to it:

* The plugins ship prebuilt, one per Qt major (``plugins/qt6`` and
  ``plugins/qt5`` beside the app). Each is built against the *oldest* Qt of its
  major that XPLasma supports and uses only public Qt API, so it loads into
  every newer Qt of that major: nothing is compiled on the user's machine, and
  a system update never makes it stale.
* The active style is a **frozen copy**: ``~/.local/share/xplasma/active/
  <sha256>.msstyles``. Every running Qt app reads it, so it must never change
  underneath them; a new Apply writes a new file and repoints the config.
* ``~/.config/xplasmarc`` names that copy and its variant. The plugin watches
  it, which is how a new Apply reaches apps that are already running.

Windows Classic currently uses Qt's built-in "Windows" style; it moves into the
plugin in Phase 3b (see PLAN.md).
"""
from __future__ import annotations

import configparser
import filecmp
import hashlib
import os
import shutil

import hostexec as host
import paths

HOME = os.path.expanduser("~")
STYLE_KEY = "XPLasma"                 # what System Settings lists; Qt matches keys case-insensitively
LIBRARY = "libxplasma.so"
QT_MAJORS = ("6", "5")
CONFIG = os.path.join(HOME, ".config", "xplasmarc")
ACTIVE_DIR = os.path.join(paths.DATA, "active")
ENV_SCRIPT = os.path.join(HOME, ".config", "plasma-workspace", "env", "xplasma-style.sh")
PREVIOUS_STYLE = os.path.join(paths.DATA, "widgetstyle-previous.txt")


def plugin_root(major: str) -> str:
    """The user-local Qt plugin root for one Qt major (``.../qt6/plugins``)."""
    return os.path.join(HOME, ".local", "lib", f"qt{major}", "plugins")


def installed_plugin(major: str) -> str:
    return os.path.join(plugin_root(major), "styles", LIBRARY)


def shipped_plugin(major: str) -> str:
    """The prebuilt plugin that ships with the app (native/ builds put it here)."""
    return paths._shipped("plugins", f"qt{major}", "styles", LIBRARY)


def plugins_available() -> list[str]:
    """Qt majors we have a prebuilt plugin for."""
    return [major for major in QT_MAJORS if os.path.isfile(shipped_plugin(major))]


def plugins_installed() -> bool:
    return any(os.path.isfile(installed_plugin(major)) for major in QT_MAJORS)


def install_plugins(progress=print) -> list[str]:
    """Copy every shipped plugin into place. Returns the Qt majors installed.

    Raises RuntimeError when there is nothing to install -- a source checkout
    whose native/ build hasn't been run yet.
    """
    majors = plugins_available()
    if not majors:
        raise RuntimeError("no prebuilt XPLasma style plugin found; build native/ first "
                           "(see native/README.md)")
    for major in majors:
        dest = installed_plugin(major)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        _install_file(shipped_plugin(major), dest)
        progress(f"  Qt {major} style plugin installed")
    install_session_env()
    return majors


def _install_file(source: str, dest: str) -> None:
    """Put `source` at `dest` without ever writing into the live file.

    Every running Qt app has the installed plugin mmap'd and executable --
    plasmashell, kwin_wayland, every dialog on screen. Copying onto it rewrites
    that inode under them, and the whole session takes a SIGBUS the next time
    it executes from an affected page (learned the hard way with kvantum-xp).
    Renaming a freshly written file over it gives the new build its own inode;
    processes that mapped the old one keep it and carry on.
    """
    if os.path.exists(dest) and filecmp.cmp(source, dest, shallow=False):
        return                      # identical already; don't churn the inode
    staged = f"{dest}.new"
    shutil.copy2(source, staged)
    os.replace(staged, dest)


def install_session_env() -> None:
    """Make the plugins visible to Qt apps: this login (via the systemd user
    environment, which plasmashell and the apps it launches inherit on their
    next start) and every later one (a plasma-workspace env script).

    Both majors' roots go on the one QT_PLUGIN_PATH. Each Qt reads a plugin's
    metadata before loading it and skips the other major's build.
    """
    roots = [plugin_root(major) for major in QT_MAJORS]
    os.makedirs(os.path.dirname(ENV_SCRIPT), exist_ok=True)
    with open(ENV_SCRIPT, "w", encoding="utf-8") as fh:
        fh.write("# Written by XPLasma: lets Qt find the user-local XPLasma style plugins.\n"
                 f'export QT_PLUGIN_PATH="{":".join(roots)}${{QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}}"\n')
    current = [p for p in (host.show_environment("QT_PLUGIN_PATH") or "").split(":") if p]
    value = ":".join(dict.fromkeys(roots + current))
    host.run(["systemctl", "--user", "set-environment", f"QT_PLUGIN_PATH={value}"], check=False)
    host.run(["dbus-update-activation-environment", "--systemd", f"QT_PLUGIN_PATH={value}"], check=False)


def freeze(msstyles: str) -> str:
    """The frozen, content-addressed copy of `msstyles` that apps will read."""
    digest = hashlib.sha256()
    with open(msstyles, "rb") as fh:
        for block in iter(lambda: fh.read(1 << 20), b""):
            digest.update(block)
    dest = os.path.join(paths.ensure(ACTIVE_DIR), f"{digest.hexdigest()}.msstyles")
    if not os.path.exists(dest):
        staged = f"{dest}.new"
        shutil.copyfile(msstyles, staged)
        os.replace(staged, dest)
    return dest


def write_config(frozen: str, variant: str | None, source: str) -> None:
    """Point the plugin at a frozen style. Atomic, so a watching app never
    reads half a file."""
    cp = configparser.ConfigParser(interpolation=None)
    cp.optionxform = str
    cp["Theme"] = {"Path": frozen, "Variant": variant or "", "Source": os.path.abspath(source)}
    tmp = f"{CONFIG}.tmp"
    os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
    with open(tmp, "w", encoding="utf-8") as fh:
        cp.write(fh, space_around_delimiters=False)
    os.replace(tmp, CONFIG)


def prune_frozen(keep: str) -> None:
    """Drop frozen copies other than `keep`. An app that still has an old one
    open keeps reading it (it was read into memory); nothing reads it after."""
    for entry in os.listdir(ACTIVE_DIR) if os.path.isdir(ACTIVE_DIR) else []:
        path = os.path.join(ACTIVE_DIR, entry)
        if entry.endswith(".msstyles") and path != keep:
            os.remove(path)


def _current_style() -> str:
    out = host.run(["kreadconfig6", "--file", "kdeglobals", "--group", "KDE", "--key", "widgetStyle"],
                   capture=True)
    return out.stdout.strip()


def _set_style(style: str) -> None:
    """Switch [KDE] widgetStyle and tell running KDE apps (KGlobalSettings
    StyleChanged). Apps that can't load the style keep their current one."""
    if not os.path.exists(PREVIOUS_STYLE):
        os.makedirs(os.path.dirname(PREVIOUS_STYLE), exist_ok=True)
        with open(PREVIOUS_STYLE, "w", encoding="utf-8") as fh:
            fh.write(_current_style() or "Breeze")
    host.run(["kwriteconfig6", "--notify", "--file", "kdeglobals", "--group", "KDE",
              "--key", "widgetStyle", style], check=True)
    host.run(["dbus-send", "--session", "--type=signal", "/KGlobalSettings",
              "org.kde.KGlobalSettings.notifyChange", "int32:2", "int32:0"], check=False)


def apply_visual_style(msstyles: str, variant: str | None, progress=print) -> None:
    """Draw every Qt app with `msstyles` (in `variant`, or its default)."""
    install_plugins(progress)
    frozen = freeze(msstyles)
    write_config(frozen, variant, msstyles)
    prune_frozen(keep=frozen)
    _set_style(STYLE_KEY)


def apply_classic_style() -> None:
    """Windows Classic: Qt's built-in Windows style until Phase 3b."""
    _set_style("Windows")


def remove() -> str:
    """Undo everything: restore the previous style, drop the env hook, the
    plugins, the config and the frozen styles."""
    previous = "Breeze"
    if os.path.exists(PREVIOUS_STYLE):
        with open(PREVIOUS_STYLE, encoding="utf-8") as fh:
            previous = fh.read().strip() or previous
    host.run(["kwriteconfig6", "--notify", "--file", "kdeglobals", "--group", "KDE",
              "--key", "widgetStyle", previous], check=True)
    for path in (ENV_SCRIPT, CONFIG, PREVIOUS_STYLE, *(installed_plugin(m) for m in QT_MAJORS)):
        if os.path.exists(path):
            os.remove(path)
    shutil.rmtree(ACTIVE_DIR, ignore_errors=True)
    # Take only our roots back out: anything else on the path belongs to
    # someone else (kvantum-xp, while it's still installed, for one).
    ours = {plugin_root(major) for major in QT_MAJORS}
    rest = [p for p in (host.show_environment("QT_PLUGIN_PATH") or "").split(":") if p and p not in ours]
    if rest:
        host.run(["systemctl", "--user", "set-environment", f"QT_PLUGIN_PATH={':'.join(rest)}"], check=False)
    else:
        host.run(["systemctl", "--user", "unset-environment", "QT_PLUGIN_PATH"], check=False)
    return previous
