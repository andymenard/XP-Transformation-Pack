#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""XP's keyboard cues: mnemonic underlines only while Alt is held, focus
rectangles only once focus has moved by keyboard.

    keyboard_cues.py          (needs PySide6 and build/dev built)
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHILD = r'''
import sys
from PySide6.QtWidgets import QApplication, QWidget, QVBoxLayout, QPushButton, QStyleFactory
from PySide6.QtCore import Qt, QTimer, QEventLoop, QEvent
from PySide6.QtGui import QKeyEvent
app = QApplication([])
app.setStyle(QStyleFactory.create("XPlasma"))
w = QWidget(); v = QVBoxLayout(w)
a = QPushButton("&Open"); b = QPushButton("&Save"); v.addWidget(a); v.addWidget(b)
w.resize(160, 90); w.show(); w.activateWindow()
def settle(ms=150):
    loop = QEventLoop(); QTimer.singleShot(ms, loop.quit); loop.exec()
settle()
out = sys.argv[1]
a.setFocus(Qt.MouseFocusReason); settle(); w.grab().save(out + "-mouse.png")
QApplication.sendEvent(a, QKeyEvent(QEvent.KeyPress, Qt.Key_Alt, Qt.AltModifier)); settle(); w.grab().save(out + "-alt.png")
QApplication.sendEvent(a, QKeyEvent(QEvent.KeyRelease, Qt.Key_Alt, Qt.NoModifier)); settle(); w.grab().save(out + "-released.png")
from PySide6.QtTest import QTest
QTest.keyClick(a, Qt.Key_Tab); settle(); w.grab().save(out + "-tab.png")
'''


def main() -> int:
    import tempfile
    from PIL import Image, ImageChops
    with tempfile.TemporaryDirectory(prefix="xplasma-cues-") as tmp:
        out = os.path.join(tmp, "cues")
        env = {**os.environ, "QT_QPA_PLATFORM": "offscreen", "QT_PLUGIN_PATH": str(ROOT / "build/dev/lib/xplasma/qt6")}
        subprocess.run([sys.executable, "-c", CHILD, out], env=env, check=True, stderr=subprocess.DEVNULL)
        img = {k: Image.open(f"{out}-{k}.png").convert("RGB") for k in ("mouse", "alt", "released", "tab")}

        def same(x, y):
            return ImageChops.difference(img[x], img[y]).getbbox() is None
        checks = [
            ("Alt shows the underlines", not same("mouse", "alt")),
            ("releasing Alt hides them again", same("mouse", "released")),
            ("a mouse-focused button has no focus rect, a tabbed one has", not same("released", "tab")),
        ]
    ok = True
    for name, passed in checks:
        ok &= passed
        print(f"{'ok  ' if passed else 'FAIL'} {name}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
