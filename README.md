# XPlasma

Windows XP visual styles, natively, on KDE Plasma. The sequel to xp2plasma.

Point it at a Windows XP `.msstyles` (Luna, Royale, Royale Noir, Zune,
Embedded, or any of the fan styles) and it themes the whole Plasma desktop from
it: window decorations, taskbar and Start menu, tray, fonts, colours,
wallpaper, icons, and **every Qt application's widgets**, drawn by a native Qt
style that reads the `.msstyles` itself.

**Status:** the style is done to the pixel against real XP (see PLAN.md and
NOTES.md). The app around it -- the XPlasma Properties editor, the generators,
setup and the CLI -- is C++/Qt 6 on the same core, rewritten from the Python
xp2plasma carried.

## Layout

| Path | What |
| --- | --- |
| `src/core/` | The `.msstyles` reader (PE, DIB, INI cascade, schema) and the renderer, pixel-exact against XP; `.theme` files and Windows Classic schemes (NONCLIENTMETRICS, LOGFONT). |
| `src/style/` | The Qt 5/6 style plugin, drawing every widget from the core. |
| `src/app/` | The app: the library under everything (archives, host programs, state, the theme library, the generators, apply and reset, setup), the `xplasma` CLI and the `xplasma-edit` editor (`editor/`). `data/schemes/` holds XP's 36 stock Classic schemes, decoded from its `HIVEDEF.INF` by `xplasma-schemes`. |
| `src/app/start-menu/` | XPlasma's Start plasmoid (`fyi.hotsocket.xplasma.start`): the Start button and XP's menu on Plasma's own application models. Only its data changes with the look. |
| `src/third_party/` | libarchive, built static and linked in (ZIP, RAR, 7z, TAR, XP's CAB-compressed files). |
| `src/tools/` | `xplasma-render` (oracle renders, `--export`), `xplasma-dump`, `xplasma-gallery`, `xplasma-probe`. |
| `tests/core/` | The core's unit test and fuzz targets, with their vectors. |
| `tests/app/` | QtTest for the app. |
| `tests/oracle/` | Dev tooling (Python) that holds the style against the Windows XP reference VM (`styles_matrix.py`, …). |
| `refharness/` | The programs that run inside the XP VM and dump what real uxtheme draws. |
| `containers/` | Build containers: the dev image (host-matched toolchain), the release image (oldest supported Qt), and one per Plasma series for `build-forks.sh`. |
| `project.ini` | The project's own settings: its version, its issue tracker, where it checks for a newer XPlasma, and the Internet Archive. The build writes them into `app/project.h`. |
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
(XPlasma Properties), the style plugins in `lib/xplasma/qt{6,5}/styles/`, and
the launcher in `share/applications/`. The app is Qt 6 only; the style is built
for both majors. Nothing else is needed on the host: archives (the XP ISO, its
CAB-compressed files, the theme packs' ZIPs) are read by the linked-in
libarchive and XPlasma's own ISO 9660 reader.

A plugin built against Qt *6.N* loads into every Qt ≥ *6.N*, **provided** it
uses only public Qt API and binds nothing else to Qt (the 6.6.3 build runs on
the host's 6.9.1 and in PySide6's 6.11.2; the 5.15.2 build on 5.15.17).
`check-abi.sh` enforces that on every release build: no private Qt symbols, no
non-Qt symbol bound to a Qt version (the Qt Company's 5.15 binaries export sized
`operator delete`, which is why libstdc++ is linked first), only the plugin
entry points exported, glibc no newer than 2.34.

```sh
QT_PLUGIN_PATH=$PWD/build/dev/lib/xplasma/qt6 build/dev-qt6/xplasma-probe --out /tmp/probe.png
QT_PLUGIN_PATH=$PWD/build/dev/lib/xplasma/qt6 dolphin -style XPlasma
XPLASMA_SAFE=1 …    # the plugin draws nothing of its own (classic only)
```

## Installing and running

The one file to hand out is `XPlasma-Setup`, a Windows Installer–style
wizard with XPlasma packed inside it (a self-extracting archive; nothing of
Windows ships in it: its look is XPlasma's own Classic drawing, its pictures
its own). Run, it downloads Windows XP from the Internet Archive (or uses the
copy already downloaded), takes out what XPlasma uses, installs XPlasma into
`~/.local`, installs XP's pointer schemes as cursor themes, and offers to
launch XPlasma. Choosing a look is XPlasma Properties' job, never the
wizard's. Run again, it repairs or removes XPlasma.

```sh
./build.sh release && ./package.sh     # build/XPlasma-Setup
```

From a checkout, for development:

```sh
./install.sh [dev|release] [PREFIX]    # default: dev into ~/.local
xplasma setup --download-media         # or --media PATH/TO/XP.iso (or its I386 folder)
xplasma check                          # whether this desktop can take the XP look (--json for Setup)
xplasma list                           # the styles, themes and Classic schemes it found
xplasma apply ~/.local/share/xplasma/assets/themes/Luna/Luna.theme
xplasma apply 'classic:Windows Standard'   # LOOK: a .msstyles[:VARIANT_INI], a .theme, or classic:SCHEME
xplasma reset                          # back to Plasma's own look
xplasma-edit                           # XPlasma Properties: Themes, Desktop, Appearance, Pointers, Sounds (also in the app menu)
xplasma-edit --date-time               # Date and Time Properties (double-click the clock)
xplasma-edit --sounds                  # ... opened on its Sounds tab: XP's sound schemes
xplasma-edit --check-updates --now     # XP's update globe, if project.ini's UpdatesUrl names a newer XPlasma (run at login)
xplasma sounds 'Windows Default'       # or 'No Sounds', a saved scheme, or off (kept off by later applies)
```

From Plasma 6.5 on, Plasma builds some of its widgets (the tray,
notifications, the task manager) into their plugins, where a home-folder copy
can't stand in for them. XPlasma builds its own of those from Plasma's source,
one per Plasma series, in that series' container:

```sh
./build-forks.sh 6.6 6.7   # build/plasma/SERIES/out: the tray, notifications, task manager and panel per Plasma series
```

Apply installs the build that loads in the running Plasma, or refuses the
whole look if none does. After a Plasma upgrade to another series, a check at
login (`xplasma check-forks`, from XPlasma's plasma-workspace env script)
swaps in that series' build, or takes XPlasma's off so Plasma's own widgets
come back, before Plasma's shell starts.

Flatpak's Qt 6 apps are styled too: apply puts the style plugin into each
installed KDE runtime (6.x) as an `org.kde.KStyle.XPlasma` extension --
a plain folder under `~/.local/share/flatpak/extension`, nothing to build or
download -- and lets every app read the look (`~/.local/share/xplasma/active`,
read-only, through the user's global Flatpak override). A runtime installed
later gets it at the next login; reset takes both back out
(`src/app/flatpak.cpp`).

GTK 3 and GTK 4 apps get a generated theme (`~/.local/share/themes/XPlasma-*`,
on GTK's own Adwaita, with XP's controls as images XPlasma's Qt style draws),
switched to through Plasma's GTK settings; libadwaita apps, which ignore
themes, get the XP layer through `~/.config/gtk-4.0/gtk.css`
(`src/app/gtk.cpp`).

XPlasma adopts what xp2plasma already imported on a machine (the asset
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
