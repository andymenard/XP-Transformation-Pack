#!/usr/bin/env python3
"""Apply the classic XP selection outline to an existing Plasma FolderView override."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

OLD = '''                radius: Kirigami.Units.cornerRadius
                border.color: sysPalette.highlight
                color: Qt.alpha(border.color, 0.3)'''
NEW = '''                // XPlasma: classic XP desktop selection (clear center, dashed edge).
                radius: 0
                border.width: 0
                color: "transparent"

                Canvas {
                    anchors.fill: parent
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();
                        if (width < 1 || height < 1) return;
                        ctx.lineWidth = 1;
                        ctx.strokeStyle = "white";
                        ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
                        ctx.setLineDash([2, 2]);
                        ctx.strokeStyle = "black";
                        ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
                    }
                }'''


def patch(text):
    if NEW in text:
        return text
    if text.count(OLD) != 1:
        raise RuntimeError("Unrecognized FolderView selection code; refusing to alter it")
    return text.replace(OLD, NEW)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--home", type=Path, default=Path.home())
    args = parser.parse_args()
    rel = ".local/share/plasma/plasmoids/org.kde.desktopcontainment/contents/ui/FolderView.qml"
    path = args.home / rel
    if not path.is_file():
        raise RuntimeError("The existing XPlasma desktop override is missing")
    before = path.read_text()
    after = patch(before)
    if before == after:
        print("XP desktop selection is already installed")
        return
    backup = args.home / ".local/share/xplasma/maintenance/desktop-selection/FolderView.qml.before"
    backup.parent.mkdir(parents=True, exist_ok=True)
    if not backup.exists():
        shutil.copy2(path, backup)
    temp = path.with_suffix(".qml.new")
    temp.write_text(after)
    shutil.copymode(path, temp)
    temp.replace(path)
    # Keep the original installer's uninstall fingerprint current, if tracked.
    for name in ("fedora-xp-installer", "kde-xp-suite", "kubuntu-xp-suite"):
        record = args.home / ".local/share" / name / "after.json"
        if record.is_file():
            data = json.loads(record.read_text())
            if rel in data and data[rel].get("kind") == "file":
                data[rel]["value"] = hashlib.sha256(path.read_bytes()).hexdigest()
                record.write_text(json.dumps(data, indent=2) + "\n")
    print("Installed transparent XP desktop selection with a dashed outline")


if __name__ == "__main__":
    main()
