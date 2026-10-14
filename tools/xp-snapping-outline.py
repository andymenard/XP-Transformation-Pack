#!/usr/bin/env python3
"""Install the XP snapping preview without restarting the Wayland session."""
from pathlib import Path
import shutil
import subprocess


def main():
    home = Path.home()
    source = Path(__file__).resolve().parent.parent / "themes/kwin/xplasma/outline.qml"
    writer = shutil.which("kwriteconfig6")
    if not writer:
        raise RuntimeError("Plasma 6's kwriteconfig6 is required")
    backup = home / ".local/share/xplasma/maintenance/snapping-outline"
    backup.mkdir(parents=True, exist_ok=True)
    config = home / ".config/kwinrc"
    if config.exists() and not (backup / "kwinrc.before").exists():
        shutil.copy2(config, backup / "kwinrc.before")
    dest = home / ".local/share/kwin/xplasma/outline.qml"
    dest.parent.mkdir(parents=True, exist_ok=True)
    if dest.exists() and not (backup / "outline.qml.before").exists():
        shutil.copy2(dest, backup / "outline.qml.before")
    shutil.copy2(source, dest)
    subprocess.run([writer, "--file", "kwinrc", "--group", "Outline", "--key", "QmlPath",
                    "kwin/xplasma/outline.qml"], check=True)
    dbus = shutil.which("qdbus6") or shutil.which("qdbus-qt6")
    if dbus:
        subprocess.run([dbus, "org.kde.KWin", "/KWin", "reconfigure"], check=False)
    print("XP snapping preview installed. Log out and back in if the old preview is cached.")


if __name__ == "__main__":
    main()
