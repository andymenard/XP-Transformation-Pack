# XPLasma

Windows XP visual styles, natively, on KDE Plasma. The sequel to xp2plasma.

Point it at a Windows XP `.msstyles` (Luna, Royale, Royale Noir, Zune,
Embedded, or any of the fan styles) and it themes the whole Plasma desktop from
it: window decorations, taskbar and Start menu, tray, fonts, colours,
wallpaper, icons, and **every Qt application's widgets**, drawn by a native Qt
style that reads the `.msstyles` itself.

**Status:** the style is done to the pixel against real XP (see PLAN.md and
NOTES.md). The app around it -- the Display Properties editor, the generators,
setup and the CLI -- is C++/Qt 6 on the same core, rewritten from the Python
xp2plasma carried.

## Layout

| Path | What |
| --- | --- |
| `src/core/` | The `.msstyles` reader (PE, DIB, INI cascade, schema) and the renderer, pixel-exact against XP; `.theme` files and Windows Classic schemes (NONCLIENTMETRICS, LOGFONT). |
| `src/style/` | The Qt 5/6 style plugin, drawing every widget from the core. |
| `src/app/` | The app: the library under everything (archives, host programs, state, the theme library, the generators, apply and reset, setup), the `xplasma` CLI and the `xplasma-edit` editor (`editor/`). `data/schemes/` holds XP's 36 stock Classic schemes, decoded from its `HIVEDEF.INF` by `xplasma-schemes`. |
| `src/app/start-menu/` | XPLasma's Start plasmoid (`fyi.hotsocket.xplasma.start`): the Start button and XP's menu on Plasma's own application models. Only its data changes with the look. |
| `src/third_party/` | libarchive, built static and linked in (ZIP, RAR, 7z, TAR, XP's CAB-compressed files). |
| `src/tools/` | `xplasma-render` (oracle renders, `--export`), `xplasma-dump`, `xplasma-gallery`, `xplasma-probe`. |
| `tests/core/` | The core's unit test and fuzz targets, with their vectors. |
| `tests/app/` | QtTest for the app. |
| `tests/oracle/` | Dev tooling (Python) that holds the style against the Windows XP reference VM (`styles_matrix.py`, …). |
| `refharness/` | The programs that run inside the XP VM and dump what real uxtheme draws. |
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

Each installs into `build/<mode>/`: `bin/xplasma` (the CLI), `bin/xplasma-edit`
(Display Properties), the style plugins in `lib/xplasma/qt{6,5}/styles/`, and
the launcher in `share/applications/`. The app is Qt 6 only; the style is built
for both majors. Nothing else is needed on the host: archives (the XP ISO, its
CAB-compressed files, the theme packs' ZIPs) are read by the linked-in
libarchive and XPLasma's own ISO 9660 reader.

A plugin built against Qt *6.N* loads into every Qt ≥ *6.N*, **provided** it
uses only public Qt API and binds nothing else to Qt (the 6.6.3 build runs on
the host's 6.9.1 and in PySide6's 6.11.2; the 5.15.2 build on 5.15.17).
`check-abi.sh` enforces that on every release build: no private Qt symbols, no
non-Qt symbol bound to a Qt version (the Qt Company's 5.15 binaries export sized
`operator delete`, which is why libstdc++ is linked first), only the plugin
entry points exported, glibc no newer than 2.34.

```sh
QT_PLUGIN_PATH=$PWD/build/dev/lib/xplasma/qt6 build/dev-qt6/xplasma-probe --out /tmp/probe.png
QT_PLUGIN_PATH=$PWD/build/dev/lib/xplasma/qt6 dolphin -style XPLasma
XPLASMA_SAFE=1 …    # the plugin draws nothing of its own (classic only)
```

## Installing and running

```sh
./install.sh [dev|release] [PREFIX]    # default: dev into ~/.local
xplasma setup --download-media         # or --media PATH/TO/XP.iso (or its I386 folder)
xplasma list                           # the styles, themes and Classic schemes it found
xplasma apply ~/.local/share/xplasma/assets/themes/Luna/Luna.theme
xplasma apply 'classic:Windows Standard'   # LOOK: a .msstyles[:VARIANT_INI], a .theme, or classic:SCHEME
xplasma reset                          # back to Plasma's own look
xplasma-edit                           # Display Properties (also in the app menu)
```

XPLasma adopts what xp2plasma already imported on a machine (the asset
library, saved schemes, the downloaded XP media) instead of fetching it again:
it moves them into its own places on first run, leaving symlinks behind
(`src/app/adopt.cpp`). Nothing Microsoft-copyrighted is in this repository.

## Tests

```sh
build/dev-qt6/test-core                              # the core
build/dev-qt6/tests/app/test-app                     # the C++ app (run on the host to include the XP ISO)
build/dev-qt6/tests/app/test-startmenu               # the Start menu's QML, drawn offscreen
tests/app/plasmoid_smoke.sh THEME.json               # the Start plasmoid loaded by Plasma (host only)
QT_QPA_PLATFORM=offscreen build/dev-qt6/src/app/xplasma-edit --screenshots OUT   # every tab of the editor
tests/oracle/styles_matrix.py OUT                     # the style against real XP (needs refharness/vm.sh)
./fuzz.sh [SECONDS]                                   # the core's parsers under libFuzzer
```

Tests that need the official styles or the XP media find them where setup
imported them, and skip when they are absent. The oracle tooling is the only
Python left: `python3 -m venv .venv && .venv/bin/pip install -r requirements-dev.txt`.
