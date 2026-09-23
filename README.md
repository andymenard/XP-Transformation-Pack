# XPLasma

Windows XP visual styles, natively, on KDE Plasma. The sequel to xp2plasma.

Point it at a Windows XP `.msstyles` (Luna, Royale, Royale Noir, Zune,
Embedded, or any of the fan styles) and it themes the whole Plasma desktop from
it: window decorations, taskbar and Start menu, tray, fonts, colours,
wallpaper, icons, and **every Qt application's widgets**, drawn by a native Qt
style that reads the `.msstyles` itself.

**Status:** the style is done to the pixel against real XP (see PLAN.md and
NOTES.md). The app around it -- the Display Properties editor, the
generators and the CLI -- is being rewritten from the Python carried over from
xp2plasma into C++/Qt 6 on the same core; until that lands, the Python app in
`app/` is what runs.

## Layout

| Path | What |
| --- | --- |
| `src/core/` | The `.msstyles` reader (PE, DIB, INI cascade, schema) and the renderer, pixel-exact against XP; `.theme` files and Windows Classic schemes (NONCLIENTMETRICS, LOGFONT). |
| `src/style/` | The Qt 5/6 style plugin, drawing every widget from the core. |
| `src/app/` | The C++ app replacing `app/`: the library under everything (archives, host programs, state, the theme library), and the `xplasma` CLI. `data/schemes/` holds XP's 36 stock Classic schemes, decoded from its `HIVEDEF.INF` by `xplasma-schemes`. |
| `src/app/start-menu/` | XPLasma's Start plasmoid (`fyi.hotsocket.xplasma.start`): the Start button and XP's menu on Plasma's own application models. Only its data changes with the look. |
| `src/third_party/` | libarchive, built static and linked in (ZIP, RAR, 7z, TAR, XP's CAB-compressed files). |
| `src/tools/` | `xplasma-render` (oracle renders, `--export`), `xplasma-dump`, `xplasma-gallery`, `xplasma-probe`. |
| `tests/core/` | The core's unit test and fuzz targets, with their vectors. |
| `tests/app/` | QtTest for the C++ app. |
| `tests/oracle/` | Dev tooling that holds the style against the Windows XP reference VM (`styles_matrix.py`, …). |
| `refharness/` | The programs that run inside the XP VM and dump what real uxtheme draws. |
| `app/` | The Python app carried over from xp2plasma, being replaced (see above). |
| `containers/` | Build containers: the dev image (host-matched toolchain) and the release image (oldest supported Qt). |
| `docs/xp2plasma/` | xp2plasma's README and visual notes, kept for reference. |

## Building

The host needs no compiler; everything builds in podman.

```sh
podman build -t xplasma-dev:3.8.1x -f containers/dev.Containerfile containers   # once
podman build -t xplasma-release:2  -f containers/release.Containerfile containers  # once

./build.sh dev       # host-matched Qt, for iterating: build/dev-qt{6,5}/
./build.sh release   # Qt 6.6.3 / 5.15.2: the portable, shipping build
```

Both install the style to `app/plugins/qt{6,5}/styles/libxplasma.so`. The
Qt 6 tree also builds the C++ app: `build/dev-qt6/src/app/xplasma`
(`--version`, `list [--json]`; more with each phase of the rewrite).

A plugin built against Qt *6.N* loads into every Qt ≥ *6.N*, **provided** it
uses only public Qt API and binds nothing else to Qt (the 6.6.3 build runs on
the host's 6.9.1 and in PySide6's 6.11.2; the 5.15.2 build on 5.15.17).
`check-abi.sh` enforces that on every release build: no private Qt symbols, no
non-Qt symbol bound to a Qt version (the Qt Company's 5.15 binaries export sized
`operator delete`, which is why libstdc++ is linked first), only the plugin
entry points exported, glibc no newer than 2.34.

```sh
QT_PLUGIN_PATH=$PWD/app/plugins/qt6 build/dev-qt6/xplasma-probe --out /tmp/probe.png
QT_PLUGIN_PATH=$PWD/app/plugins/qt6 dolphin -style XPLasma
XPLASMA_SAFE=1 …    # the plugin draws nothing of its own (classic only)
```

## Running from a checkout

```sh
python3 -m venv .venv
.venv/bin/pip install Pillow pefile PySide6-Essentials pycdlib
.venv/bin/python3 app/xplasma.py --help
.venv/bin/python3 app/xplasma.py edit        # the Display Properties editor
```

XPLasma adopts what xp2plasma already imported on a machine (the asset
library, saved schemes, the downloaded XP media) instead of fetching it again.
The C++ app moves them into its own places on first run, leaving symlinks
behind (`src/app/adopt.cpp`); the Python app uses them wherever they are. Nothing Microsoft-copyrighted is in this repository.

## Tests

```sh
build/dev-qt6/test-core                              # the core
build/dev-qt6/tests/app/test-app                     # the C++ app (run on the host to include the XP ISO)
build/dev-qt6/tests/app/test-startmenu               # the Start menu's QML, drawn offscreen
tests/app/plasmoid_smoke.sh THEME.json               # the Start plasmoid loaded by Plasma (host only)
tests/oracle/styles_matrix.py OUT                     # the style against real XP (needs refharness/vm.sh)
for t in app/tests/test_*.py; do QT_QPA_PLATFORM=offscreen .venv/bin/python3 "$t"; done   # the Python app
```

Tests that need the official styles or the XP media find them through
`app/tests/fixtures.py` (override with `XPLASMA_CORPUS`), and skip when
they are absent.
