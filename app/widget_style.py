"""Install and switch the Qt widget style for the live Plasma session.

Visual styles get a generated theme for **kvantum-xp**, xp2plasma's own build
of Kvantum (see kvantum-xp/patch.py): the same engine, but with its own style
key (`kvantum-xp`), library (`libkvantum-xp.so`), config folder
(`~/.config/KvantumXP`) and hidden symbols, so it never touches or clashes with
a real Kvantum install -- plus the Windows/Breeze-style progress label beside
the bar. Windows Classic gets Qt's own built-in "Windows" style.

Nothing is installed system-wide. The plugin is built by kvantum-xp/build.sh
against **the host's own Qt**, copied into ~/.local/lib/qt6/plugins, and exposed
through QT_PLUGIN_PATH: a plasma-workspace env script for future logins, plus
the systemd user environment for apps started from this session once
plasmashell restarts. `remove_kvantum()` undoes all of it.

Two things about that build are worth knowing. It has to be built against the
Qt the host's applications load -- a plugin compiled against a different Qt is
refused by Qt's loader and apps silently fall back to the default style, which
is why the version is recorded and checked rather than assumed. And it runs on
the *host* (through hostexec), so every input has to be staged under the real
home first: build.sh, patch.py and the Kvantum tarball ship inside the
application directory, which the host cannot see.
"""
from __future__ import annotations

import filecmp
import hashlib
import os
import re
import shutil

import hostexec as host
import paths

HOME = os.path.expanduser("~")
PLUGIN_DIR = os.path.join(HOME, ".local", "lib", "qt6", "plugins")
PLUGIN = os.path.join(PLUGIN_DIR, "styles", "libkvantum-xp.so")
LEGACY_PLUGIN = os.path.join(PLUGIN_DIR, "styles", "libkvantum.so")   # the stock build used at first
ENV_SCRIPT = os.path.join(HOME, ".config", "plasma-workspace", "env", "xp2plasma-kvantum.sh")
KVANTUM_CONFIG = os.path.join(HOME, ".config", "KvantumXP")
STYLE_KEY = "kvantum-xp"
STATE_DIR = paths.DATA
PREVIOUS_STYLE = os.path.join(STATE_DIR, "widgetstyle-previous.txt")

# The build runs on the host, so its inputs and output live under the real home
# rather than next to the source, which is read-only once installed.
BUILD_WORK = paths.KVANTUM_BUILD
BUILT_PLUGIN = os.path.join(paths.KVANTUM_OUT, "libkvantum-xp.so")
BUILT_AGAINST = os.path.join(paths.KVANTUM_OUT, "built-against.txt")
BUILD_INPUTS = ("build.sh", "patch.py", "Kvantum-1.1.5.tar.xz")

_VERSION_RE = re.compile(r"\d+\.\d+(?:\.\d+)?")


def _libqt_version() -> str | None:
    """Read the version off the host's libQt6Core filename."""
    dirs: list[str] = []
    for line in host.run(["ldconfig", "-p"], capture=True).stdout.splitlines():
        if "libQt6Core.so.6" in line and "=>" in line:
            dirs.append(os.path.dirname(line.rsplit("=>", 1)[-1].strip()))
    dirs += ["/usr/lib", "/usr/lib64", "/usr/local/lib",
             "/usr/lib/x86_64-linux-gnu", "/usr/lib/aarch64-linux-gnu"]
    for directory in dict.fromkeys(dirs):
        for entry in host.listdir(directory):
            # libQt6Core.so.6.9.1 -- the whole "6.9.1" is the version; the 6 in
            # the soname is the Qt major, so it has to be captured, not consumed.
            match = re.fullmatch(r"libQt6Core\.so\.(\d+\.\d+(?:\.\d+)?)", entry)
            if match:
                return match.group(1)
    return None


def host_qt_version() -> str | None:
    """The host's Qt version as "6.9.1", from whichever probe this distro has.

    Deliberately not the distro package manager: what matters is the Qt the
    host's applications will load the plugin into, and the version alone
    determines that. Every probe goes through hostexec, which resolves against
    the host's PATH with the AppImage mount subtracted -- so this reports the
    Qt the plugin will actually be loaded into, never the bundle's own.
    """
    for argv in (["qmake6", "-query", "QT_VERSION"], ["qtpaths6", "--qt-version"]):
        if host.have(argv[0]):
            out = host.run(argv, capture=True).stdout.strip()
            if _VERSION_RE.fullmatch(out):
                return out
    return _libqt_version()


def build_inputs_fingerprint() -> str:
    """A short digest of the sources the plugin is built from.

    The recorded Qt version alone can't tell a rebuild is needed after *our*
    side changes: edit patch.py or build.sh and the cached .so is still
    "current" for the same Qt, so the edit silently never ships. Hashing the
    inputs makes that case rebuild the way a Qt upgrade does.
    """
    digest = hashlib.sha256()
    for name in sorted(BUILD_INPUTS):
        try:
            with open(os.path.join(paths.KVANTUM_SRC, name), "rb") as fh:
                digest.update(fh.read())
        except OSError:
            return ""                # can't fingerprint; fall back to Qt alone
    return digest.hexdigest()[:16]


def plugin_is_current() -> bool:
    """True when the built plugin matches the host Qt *and* the sources it was
    built from.

    After a Qt upgrade Qt refuses to load a mismatched plugin and apps quietly
    fall back to the default style -- this is how that gets noticed and
    rebuilt. If the version cannot be determined at all we trust the recorded
    build instead, so a machine we can't probe doesn't rebuild on every apply.
    """
    try:
        with open(BUILT_AGAINST, encoding="utf-8") as fh:
            built = fh.read().strip()
    except OSError:
        return False
    if not built:
        return False
    # build.sh writes the bare Qt version; anything after "|" is our own
    # fingerprint, so a plugin built before this existed still reads cleanly.
    built_qt, _, built_inputs = built.partition("|")
    fingerprint = build_inputs_fingerprint()
    if fingerprint and built_inputs != fingerprint:
        return False
    current = host_qt_version()
    return True if current is None else built_qt == current


def kvantum_installed() -> bool:
    return os.path.exists(PLUGIN) and plugin_is_current()


def _stage_build_inputs() -> str:
    """Copy build.sh, patch.py and the Kvantum tarball where the host can see them.

    They ship inside the application directory, which is read-only when
    packaged and invisible to the host in any case.
    """
    work = paths.ensure(BUILD_WORK)
    for name in BUILD_INPUTS:
        src = os.path.join(paths.KVANTUM_SRC, name)
        if not os.path.exists(src):
            raise RuntimeError(f"missing build input: {src}")
        shutil.copy2(src, os.path.join(work, name))
    paths.ensure(paths.KVANTUM_OUT)
    return work


def install_kvantum(progress=print) -> str:
    """Build kvantum-xp against the host Qt if needed, then install it.
    Idempotent; the first build takes several minutes."""
    if not (os.path.exists(BUILT_PLUGIN) and plugin_is_current()):
        qt = host_qt_version()
        if qt is None:
            raise RuntimeError(
                "couldn't work out which Qt the host is running, so the widget style "
                "can't be built against it. Install Qt 6 development files (Arch: "
                "qt6-base; Debian/Ubuntu: qt6-base-dev; Fedora: qt6-qtbase-devel).")
        progress(f"Building widget style for Qt {qt} ...")
        work = _stage_build_inputs()
        result = host.run(["bash", os.path.join(work, "build.sh")], capture=True,
                          env={"XP2P_WORK": work, "XP2P_OUT": paths.KVANTUM_OUT})
        if result.returncode != 0 or not os.path.exists(BUILT_PLUGIN):
            raise RuntimeError("kvantum-xp build failed:\n" + (result.stdout + result.stderr)[-2000:])
        _record_build_inputs()
    os.makedirs(os.path.dirname(PLUGIN), exist_ok=True)
    _install_plugin(BUILT_PLUGIN, PLUGIN)
    if os.path.exists(LEGACY_PLUGIN):
        os.remove(LEGACY_PLUGIN)
    return PLUGIN


def _record_build_inputs() -> None:
    """Append our own fingerprint to the Qt version build.sh just wrote, so a
    later patch.py/build.sh edit is seen as stale (see plugin_is_current)."""
    fingerprint = build_inputs_fingerprint()
    if not fingerprint:
        return
    try:
        with open(BUILT_AGAINST, encoding="utf-8") as fh:
            qt = fh.read().strip().partition("|")[0]
        with open(BUILT_AGAINST, "w", encoding="utf-8") as fh:
            fh.write(f"{qt}|{fingerprint}")
    except OSError:
        pass                        # a missing marker just means "rebuild next time"


def _install_plugin(source: str, dest: str) -> None:
    """Put `source` at `dest` without ever writing into the live file.

    Every running Qt app has the installed plugin mmap'd and executable --
    plasmashell, kwin_wayland, kded6, every dialog on screen. Copying onto it
    truncates and rewrites that same inode, which invalidates all of those
    mappings, and the whole session then takes a SIGBUS the moment it next
    executes from one of the affected pages: kwin and plasmashell included.

    Renaming a freshly written file over it gives the new build its own inode
    instead. Processes that already mapped the old one keep it (unlinked but
    still mapped) and carry on; only apps started afterwards pick up the new
    build, which is the same "restart to get the new style" rule the rest of
    this module already works by.
    """
    if os.path.exists(dest) and filecmp.cmp(source, dest, shallow=False):
        return                      # identical already; don't churn the inode
    staged = f"{dest}.new"
    shutil.copy2(source, staged)
    os.replace(staged, dest)        # atomic: readers see old or new, never a torn file


def install_session_env() -> None:
    """Make the plugin visible to Qt apps: this login (via the systemd user
    environment, which plasmashell and the apps it launches inherit on their
    next start) and every later one (plasma-workspace env script)."""
    os.makedirs(os.path.dirname(ENV_SCRIPT), exist_ok=True)
    with open(ENV_SCRIPT, "w", encoding="utf-8") as fh:
        fh.write("# Written by xp2plasma: lets Qt find the user-local Kvantum style plugin.\n"
                 f'export QT_PLUGIN_PATH="{PLUGIN_DIR}${{QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}}"\n')
    # Read the *host's* value, not this process's: the AppImage exports its own
    # QT_PLUGIN_PATH pointing into the mount, and pushing that into the host's
    # systemd environment would clobber a real setting with a directory that
    # stops existing the moment the installer exits.
    current = host.show_environment("QT_PLUGIN_PATH") or ""
    value = PLUGIN_DIR if not current else (
        current if PLUGIN_DIR in current.split(":") else f"{PLUGIN_DIR}:{current}")
    host.run(["systemctl", "--user", "set-environment", f"QT_PLUGIN_PATH={value}"], check=False)
    host.run(["dbus-update-activation-environment", "--systemd", f"QT_PLUGIN_PATH={value}"], check=False)


def _current_style() -> str:
    out = host.run(["kreadconfig6", "--file", "kdeglobals", "--group", "KDE", "--key", "widgetStyle"],
                   capture=True)
    return out.stdout.strip()


def _set_style(style: str) -> None:
    """Switch [KDE] widgetStyle and tell running KDE apps (KGlobalSettings
    StyleChanged). Apps that can't load the style keep their current one."""
    if not os.path.exists(PREVIOUS_STYLE):
        os.makedirs(STATE_DIR, exist_ok=True)
        with open(PREVIOUS_STYLE, "w", encoding="utf-8") as fh:
            fh.write(_current_style() or "Breeze")
    host.run(["kwriteconfig6", "--notify", "--file", "kdeglobals", "--group", "KDE",
              "--key", "widgetStyle", style], check=True)
    host.run(["dbus-send", "--session", "--type=signal", "/KGlobalSettings",
              "org.kde.KGlobalSettings.notifyChange", "int32:2", "int32:0"], check=False)


def apply_kvantum_theme(theme_name: str, progress=print) -> None:
    install_kvantum(progress)
    install_session_env()
    os.makedirs(KVANTUM_CONFIG, exist_ok=True)
    with open(os.path.join(KVANTUM_CONFIG, "kvantum.kvconfig"), "w", encoding="utf-8") as fh:
        fh.write(f"[General]\ntheme={theme_name}\n")
    _set_style(STYLE_KEY)


def apply_classic_style() -> None:
    """Windows Classic: Qt's built-in Windows style is the real thing."""
    _set_style("Windows")


def remove_kvantum() -> str:
    """Undo everything: restore the previous style, drop the env hook and
    the plugin. Generated themes in ~/.config/KvantumXP are left for reuse."""
    previous = "Breeze"
    if os.path.exists(PREVIOUS_STYLE):
        with open(PREVIOUS_STYLE, encoding="utf-8") as fh:
            previous = fh.read().strip() or previous
    host.run(["kwriteconfig6", "--notify", "--file", "kdeglobals", "--group", "KDE",
              "--key", "widgetStyle", previous], check=True)
    for path in (ENV_SCRIPT, PLUGIN, LEGACY_PLUGIN, PREVIOUS_STYLE):
        if os.path.exists(path):
            os.remove(path)
    host.run(["systemctl", "--user", "unset-environment", "QT_PLUGIN_PATH"], check=False)
    return previous


def kvantum_theme_name(name: str) -> str:
    """A folder-safe Kvantum theme id for a display name."""
    slug = re.sub(r"[^A-Za-z0-9]+", "-", name).strip("-") or "theme"
    return f"xp2plasma-{slug}"
