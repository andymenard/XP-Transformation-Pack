#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Live reload: switching ~/.config/xplasmarc repaints a running app.

    live_reload.py            (needs PySide6 and build/dev built)

Starts a small window on Luna Silver, then atomically replaces xplasmarc --
as the app does -- with Royale, then with Windows Classic, grabbing after
each; every live grab must equal a fresh process started on that theme.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
THEMES = Path(os.path.expanduser("~/.local/share/xplasma/assets/themes"))
STEPS = [(THEMES / "Luna/luna.msstyles", "NORMALMETALLIC_INI"), (THEMES / "Royale/Royale.msstyles", "NORMALROYALE_INI"),
         (THEMES / "Windows Classic.theme", "")]

CHILD = r'''
import os, sys
from PySide6.QtWidgets import QApplication, QWidget, QVBoxLayout, QPushButton, QProgressBar, QCheckBox, QStyleFactory
from PySide6.QtCore import QTimer, QEventLoop
app = QApplication([])
app.setStyle(QStyleFactory.create("XPlasma"))
w = QWidget(); v = QVBoxLayout(w)
for x in (QPushButton("OK"), QCheckBox("Show window contents")): v.addWidget(x)
p = QProgressBar(); p.setValue(60); p.setTextVisible(False); v.addWidget(p)
w.resize(200, 110); w.show()
def settle(ms):
    loop = QEventLoop(); QTimer.singleShot(ms, loop.quit); loop.exec()
settle(200)
out = sys.argv[1]
steps = sys.argv[2:]
w.grab().save(out + "-0.png")
for i, line in enumerate(steps[1:], 1):
    path, variant = line.split("|")
    cfg = os.path.join(os.environ["XDG_CONFIG_HOME"], "xplasmarc")
    with open(cfg + ".tmp", "w") as fh:
        fh.write(f"[Theme]\nPath={path}\nVariant={variant}\n")
    os.replace(cfg + ".tmp", cfg)
    settle(800)
    w.grab().save(out + f"-{i}.png")
'''


def run(config_dir: str, steps: list[str], out: str) -> None:
    cfg = os.path.join(config_dir, "xplasmarc")
    path, variant = steps[0].split("|")
    with open(cfg, "w") as fh:
        fh.write(f"[Theme]\nPath={path}\nVariant={variant}\n")
    env = {**os.environ, "XDG_CONFIG_HOME": config_dir, "QT_QPA_PLATFORM": "offscreen",
           "QT_PLUGIN_PATH": str(ROOT / "build/dev/lib/xplasma/qt6")}
    env.pop("XPLASMA_THEME", None)
    subprocess.run([sys.executable, "-c", CHILD, out, *steps], env=env, check=True, stderr=subprocess.DEVNULL)


def main() -> int:
    from PIL import Image, ImageChops
    steps = [f"{p}|{v}" for p, v in STEPS]
    ok = True
    with tempfile.TemporaryDirectory(prefix="xplasma-live-") as tmp:
        os.makedirs(os.path.join(tmp, "live"))
        run(os.path.join(tmp, "live"), steps, os.path.join(tmp, "live"))
        for i, step in enumerate(steps):
            fresh_dir = os.path.join(tmp, f"fresh{i}")
            os.makedirs(fresh_dir)
            run(fresh_dir, [step], os.path.join(tmp, f"fresh{i}"))
            a = Image.open(os.path.join(tmp, f"live-{i}.png")).convert("RGB")
            b = Image.open(os.path.join(tmp, f"fresh{i}-0.png")).convert("RGB")
            same = a.size == b.size and ImageChops.difference(a, b).getbbox() is None
            ok &= same
            print(f"{'ok  ' if same else 'FAIL'} step {i}: {Path(step.split('|')[0]).name} live == fresh")
        a = Image.open(os.path.join(tmp, "live-0.png")).convert("RGB")
        b = Image.open(os.path.join(tmp, "live-1.png")).convert("RGB")
        changed = ImageChops.difference(a, b).getbbox() is not None
        ok &= changed
        print(f"{'ok  ' if changed else 'FAIL'} the switch changed the pixels")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
