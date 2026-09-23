# native/

XPLasma's C++ side: the Qt style plugin that draws widgets straight from a
`.msstyles`, and the tools around it. One source tree, built once per Qt major.
PLAN.md describes the architecture (§3) and the phases (§8).

## Building

The host needs no compiler; everything builds in podman.

```sh
podman build -t xplasma-dev:3.8.1x -f containers/dev.Containerfile containers   # once
podman build -t xplasma-release:1  -f containers/release.Containerfile containers  # once

native/build.sh dev       # host-matched Qt, for iterating
native/build.sh release   # Qt 6.6.3 / 5.15.2: the portable, shipping build
```

Both install to `app/plugins/qt{6,5}/styles/libxplasma.so`, which is where
`app/widget_style.py` picks the plugins up from.

## The portability rules

A plugin built against Qt *6.N* loads into every Qt ≥ *6.N*, **provided** it
uses only public Qt API and binds nothing else to Qt. Measured (PLAN.md §11):
the Qt 6.6.3 build runs on the host's 6.9.1 and in PySide6's 6.11.2; the
5.15.2 build runs on 5.15.17. `check-abi.sh` enforces the rules on every
release build:

- no `*_PRIVATE_API` Qt symbols (no `_p.h` headers, no `Qt::*Private` targets);
- no non-Qt symbol bound to a Qt version. The Qt Company's 5.15 binaries export
  sized `operator delete`, which is why libstdc++ is linked first;
- only `qt_plugin_instance` / `qt_plugin_query_metadata*` exported;
- glibc no newer than 2.34.

## Trying it

```sh
QT_PLUGIN_PATH=$PWD/app/plugins/qt6 native/build-dev-qt6/xplasma-probe --out /tmp/probe.png
QT_PLUGIN_PATH=$PWD/app/plugins/qt6 dolphin -style XPLasma
XPLASMA_SAFE=1 …    # the plugin draws nothing of its own (classic only)
```
