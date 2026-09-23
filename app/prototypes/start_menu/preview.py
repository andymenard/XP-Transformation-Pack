"""Non-destructive, native-size Start menu layout preview; no launching/actions."""
from pathlib import Path
import argparse
import json
import os
import pwd
import sys
import tempfile

# This bitmap-only preview needs no GPU. Keep it independent of the host's
# Mesa/LLVM and the bundled Qt wheel's rendering stack.
os.environ.setdefault("QT_QUICK_BACKEND", "software")

from assets import MsStyle, export
import paths  # noqa: E402  (on sys.path via assets)
from PySide6.QtCore import QTimer, QUrl, Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickView

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
ICONS = Path.home() / ".local/share/xp2plasma/assets/icons"


def icon(resource, size=24, binary="shell32.dll"):
    folder = ICONS / binary / str(resource)
    for candidate in dict.fromkeys((size, 32, 48, 16)):
        path = folder / f"{candidate}.png"
        if path.exists():
            return path.as_uri()
    return ""


def item(label, resource, action, description="", size=24, binary="shell32.dll"):
    return {"label": label, "icon": icon(resource, size, binary), "action": action, "description": description}


def fixtures():
    """Illustrative rows, not the user's live pinned/recent application models."""
    return {
        "pinnedItems": [item("Internet", 14, "browser", "Web browser", 32),
                        item("E-mail", 25, "email", "Mail reader", 32)],
        "recentItems": [item("Documents", 235, "documents", size=32),
                        item("Pictures", 236, "pictures", size=32),
                        item("Music", 237, "music", size=32),
                        item("Notepad", 2, "notepad", size=32, binary="notepad.exe"),
                        item("System Settings", 22, "settings", size=32),
                        item("Help Center", 24, "help", size=32)],
        "placeGroups": [
            [item("Documents", 235, "documents"), item("Recent Files", 21, "recent-documents"),
             item("Pictures", 236, "pictures"), item("Music", 237, "music"), item("File System", 16, "computer")],
            [item("System Settings", 22, "settings"), item("Default Applications", 274, "defaults"),
             item("Printers", 17, "printers")],
            [item("Help Center", 24, "help"), item("Search", 23, "search"), item("Run Command...", 160, "run")],
        ],
    }


def create_view(data, username=None):
    view = QQuickView()
    view.setTitle("XP Start menu — layout preview (sample rows)")
    view.setColor(Qt.transparent)
    name = username or pwd.getpwuid(os.getuid()).pw_gecos.split(",")[0] or os.environ.get("USER", "User")
    face = next((p for p in (Path.home() / ".face.icon", Path.home() / ".face") if p.is_file()), None)
    properties = {"themeData": data, "userName": name,
                  "userPicture": face.as_uri() if face else icon(269, 48), **fixtures()}
    view.setInitialProperties(properties)
    view.setSource(QUrl.fromLocalFile(str(REPO / "start-menu/StartMenu.qml")))
    if view.status() != QQuickView.Ready:
        raise RuntimeError("\n".join(e.toString() for e in view.errors()))
    view.setMinimumSize(view.size())
    view.setMaximumSize(view.size())
    # Explicitly no real shutdown/logoff/app launching in this layout preview.
    def requested(action):
        if action == "close":
            view.close()
        else:
            print(f"Preview only: {action}", flush=True)
    view.rootObject().requested.connect(requested)
    return view


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--style")
    parser.add_argument("--ini")
    parser.add_argument("--screenshot", help="Save the 380x478 client area and exit")
    args = parser.parse_args()
    state_path = Path.home() / ".local/share/xp2plasma/applied.json"
    state = json.loads(state_path.read_text()) if state_path.exists() else {}
    style = args.style or state.get("msstyles") or os.path.join(paths.THEMES, "Luna", "luna.msstyles")
    ini = args.ini if args.style else args.ini or state.get("ini")
    app = QGuiApplication(sys.argv[:1])
    with tempfile.TemporaryDirectory(prefix="xp-start-preview-") as folder:
        data = export(MsStyle.load(style, ini), folder)
        view = create_view(data)
        view.show()
        if args.screenshot:
            def save():
                if not view.grabWindow().save(args.screenshot):
                    raise RuntimeError("Unable to save preview screenshot")
                app.quit()
            QTimer.singleShot(350, save)
        return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
