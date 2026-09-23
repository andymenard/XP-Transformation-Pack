# XPLasma

Windows XP visual styles, natively, on KDE Plasma. The sequel to xp2plasma.

Point it at a Windows XP `.msstyles` (Luna, Royale, Royale Noir, Zune,
Embedded, or any of the fan styles) and it themes the whole Plasma desktop from
it: window decorations, taskbar and Start menu, tray, fonts, colours,
wallpaper, icons, and **every Qt application's widgets**, drawn by a native Qt
style that reads the `.msstyles` itself.

**Status: early.** The Python app is carried over from xp2plasma and works as
it did. The native style (`native/`) is being built, following
[PLAN.md](PLAN.md). Findings and follow-ups live in [NOTES.md](NOTES.md).

## Layout

| Path | What |
| --- | --- |
| `app/` | The Python app: the Display Properties editor, the generators (decorations, panel, Start menu, icons, …) and the CLI. Carried over from xp2plasma. |
| `native/` | The C++ core, renderer and Qt 5/6 style plugin *(in progress)*. |
| `refharness/` | Programs that run inside the Windows XP reference VM and dump what real uxtheme draws *(in progress)*. |
| `containers/` | Build containers: the dev image (host-matched toolchain) and release images (oldest supported Qt). |
| `docs/xp2plasma/` | xp2plasma's README and visual notes, kept for reference. |

## Running from a checkout

```sh
python3 -m venv .venv
.venv/bin/pip install Pillow pefile PySide6-Essentials pycdlib
.venv/bin/python3 app/xplasma.py --help
.venv/bin/python3 app/xplasma.py edit        # the Display Properties editor
```

XPLasma adopts what xp2plasma already imported on a machine (the asset
library, saved schemes, the downloaded XP media) instead of fetching it again;
see `app/paths.py`. Nothing Microsoft-copyrighted is in this repository.

## Tests

```sh
for t in app/tests/test_*.py; do QT_QPA_PLATFORM=offscreen .venv/bin/python3 "$t"; done
```

Tests that need the official styles or the XP media find them through
`app/tests/fixtures.py` (override with `XPLASMA_CORPUS`), and skip when
they are absent.
