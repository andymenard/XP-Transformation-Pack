# xp2plasma

Point it at a Windows XP `.msstyles` visual style and it generates, installs and
applies the matching KDE Plasma 6 desktop: window decoration, colour scheme,
taskbar and Start button, fonts, wallpaper, and a Qt widget style (Kvantum) for
buttons, scroll bars and tabs.

It reads the style's own bitmaps rather than approximating them, so Microsoft's
Luna, Royale, Royale Noir, Zune and Embedded themes work, along with the fan
recolours. Windows Classic is synthesised from a colour scheme instead, with an
editor that recreates XP's Display Properties dialog.

## Installer, not an app

The AppImage is **the installer** — a Windows-setup-style wizard you run once. It
installs the real thing *natively* on the host and then gets out of the way:

```
~/.local/lib/xp2plasma/           venv (if needed) + the app
~/.local/bin/xp2plasma            launchers
~/.local/share/applications/xp2plasma.desktop
```

Theming runs natively because its whole job is talking to a live Plasma session
— restarting plasmashell, driving KWin over D-Bus, writing into `~/.config`.
That is the compatibility story: the AppImage is only a convenient one-file
delivery mechanism, not the runtime, and it can be deleted once it has installed.

The AppImage carries its own Python and Qt, so it needs nothing installed to
start — no Flatpak, no system Python, no pip. Its glibc floor is **2.34**,
measured from the shipped binaries by `appimage/verify.sh`: it runs on Debian
12 and newer, Ubuntu 22.04+, Mint 21+, Pop!_OS 22.04+, Zorin 17+, Fedora 35+
and RHEL 9+. That floor is Qt's — PySide6's wheels are built for
`manylinux_2_34` — and it is why the build runs in a Debian 12 container:
building on a newer distribution would silently raise it and break those
releases, with the failure happening in the dynamic loader before any of our
code runs.

## What isn't here

The Windows XP artwork, visual styles and fonts are Microsoft's, so they are not
in this repository and are not shipped. Installing downloads XP's own install
media from archive.org (a one-time ~589 MB fetch, cached in
`~/.cache/xp2plasma/downloads/` so it is never repeated) and takes the fonts,
the Luna style, every `.theme` preset (including Windows Classic), wallpapers
and icons from it. The same setup pass imports the
official themes and wallpaper packs, and prepares the Kvantum widget style.
Third-party collections are never downloaded by the installer. Failed downloads
or extraction stop setup with Details and Retry;
completed downloads are reused on retry. Native application files are updated
only after asset preparation succeeds.

Setup extracts a labelled icon resource catalog into
`~/.local/share/xp2plasma/assets/icons/` and prepares the generated
**Windows XP** icon theme, activated when you Apply a theme. It uses the real shell artwork at its native sizes;
unmapped icons inherit the previous theme. The catalog keeps every original ICO
and its source/resource ID. Microsoft's binaries and images are never shipped
with the app. Reset removes only the marked generated icon theme.

Wi-Fi strength uses `xpsp2res.dll` icon groups **2010–2015**; limited/offline
connections retain distinct warning icons. Clipboard uses `shell32.dll` **133**,
text documents **152**, and symlink overlays **30**. Battery states come from `batmeter.dll` bitmap
**301** (16px) and **300** (32px): ten horizontal frames, magenta transparency,
and frame 7 overlaid for charging, as in XP. Charge levels use XP's four bands
(above 66%, above 33%, above 9%, critical), quantized to Plasma's decile names.
The catalog retains both strips; the generated theme's `xp2plasma-icons.json`
records resource types, IDs, crop rectangles and overlay frames for each name.
Modern power-profile badges retain native icons because XP has no equivalent.

## Requirements (for the native install)

- **A running KDE Plasma 6 session.** The wizard refuses to start outside one
  — one dialog, then it exits — because it configures the session in front of
  you and has nothing to offer anywhere else. Installing without one is
  possible via `xp2plasma install`, but applying a theme still needs it: that
  step drives `kwriteconfig6`, `qdbus6`, `plasma-apply-colorscheme`,
  `plasma-apply-desktoptheme` and `systemctl --user`
- A Python 3.10+ on the host, with the `venv` module, to build the native venv
- Release installers include private, statically linked `cabextract` and
  `bsdtar` binaries; no host installation of either utility is needed.
  Unbuilt source checkouts can fall back to host utilities. The ISO container
  itself is normally read in Python via `pycdlib`.
- For the widget style, either a compatible cached build, Qt 6 development
  files plus a C++ toolchain, or `podman`/`docker`.

The installer AppImage bundles its own Python, PySide6, Pillow and pefile so the
*wizard* always runs — the host Python is only needed to create the native venv.
The two archive utilities are also copied into the native app, so removing the
AppImage does not remove them. They are invoked by explicit paths, never added
to the global PATH. Their compression libraries and musl libc are linked in;
they do not require host gzip, xz or zstd helpers. Sources, notices, checksums
and the build recipe live in `bundled-tools/` (see its README).

On an Arch-family host (SteamOS included) the native install links the editor
against the **host's own Qt** instead of building a venv with a pip-installed
PySide6: it downloads and extracts Arch's own `pyside6`/`shiboken6` packages (no
root needed — see `system_qt.py`), so the editor's `QStyleFactory.create()`
genuinely loads whatever widget style the session has configured — Kvantum,
Breeze, anything — exactly like any other native KDE app. A pip-installed
PySide6 bundles its own, different Qt build and cannot load native style
plugins at all; that venv path is the fallback used everywhere else.

## Install

**The AppImage installer** (recommended; portable across distributions):

```sh
sh bundled-tools/build.sh   # build the private tools first; requires Podman
appimage/build.sh           # -> dist/xp2plasma-1.0.0-x86_64.AppImage
appimage/verify.sh dist/xp2plasma-1.0.0-x86_64.AppImage
```

Both build steps need Podman. The AppImage build runs in a Debian 12 container
so the result keeps a low glibc floor (`appimage/Containerfile` explains why),
and the wheels are downloaded on the host first so the container itself needs
no network.

To hand someone else the installer, give them that single `.AppImage` file. It
needs no Flatpak, no Python and no pip — the one thing it does need is the
executable bit, which a browser download usually strips. Either `chmod +x` it,
or in the file manager use Properties → Permissions → *Is executable*; after
that, double-clicking it starts the wizard.

(That step is the remaining rough edge in the install story. Everything else
about a Windows user's first five minutes is handled by the wizard.)

The wizard checks the system and its bundled utilities, installs the native
copy, downloads and imports the XP
install media as part of that, and can launch the editor on finish. Cancel
works at any point and rolls back whatever the install had done, including a
cancel mid-download (the archive itself, once fully downloaded, is left in the
cache either way — only a *cancelled* download is discarded). Run the wizard
again on an installed machine and it offers to update (or repair) the install,
or to uninstall it. Once it has installed, the AppImage itself can be deleted —
the product is the native tool underneath.

**Headless** (same result, no GUI):

```sh
xp2plasma install             # venv, pip deps, launchers, and the XP install media
xp2plasma install --no-iso    # ...skip asset/theme/icon/widget preparation for now
xp2plasma install --widgets   # ...and also build the Kvantum widget style
xp2plasma uninstall          # remove it and put the desktop back to Breeze (default
                             # panel layout and wallpaper); --keep-desktop leaves it,
                             # --assets also removes imported assets and saved schemes
xp2plasma reset              # just put the desktop back to Breeze, keep the install
```

**From a source checkout** (what the code expects; run it in place):

```sh
python3 -m venv .venv
.venv/bin/pip install Pillow pefile PySide6-Essentials pycdlib
.venv/bin/python3 xp2plasma.py --help
```

The AppImage is meant for sideloading rather than a store or a package archive:
because the installer writes a native copy and drives a live session, it is not
a self-contained application in the usual sense — it is a delivery mechanism
for the installer that then gets out of the way.

## Setup

A fresh install has no styles to work with. Either:

```sh
xp2plasma setup --check                      # is everything present?
xp2plasma setup --media /path/to/WinXP.iso   # import fonts, Luna, wallpapers
xp2plasma setup                              # fetch the archive.org theme packs
xp2plasma setup --all-packs                  # ...including the ~218 MB fan collection
```

Normally the installer handles all of this in one pass. **Add more...** is the
only utility entry in the editor's Themes list: it opens
`~/.local/share/xp2plasma/assets/themes/`. Put extracted `.theme` / `.msstyles`
files (and their companion folders) there; returning to the editor refreshes the
list without changing the pending theme. Downloads never run from this entry.

The editor has XP's **Themes**, **Desktop** and **Appearance** tabs. XP's other
two — Screen Saver and Settings — are not implemented, and are absent rather
than shown disabled.

The **Themes** tab lists complete `.theme` presets; multiple presets may share
the same visual style. **Appearance** lists `.msstyles` packs (plus Windows
Classic), their color schemes, and font sizes. Appearance applies preserve the
current wallpaper and icon theme. Modified combinations appear as **Current
Theme (modified)** rather than masquerading as a saved preset.

The **Desktop** tab is XP's: the wallpaper list, Position (Center / Tile /
Stretch) and the desktop Color behind it, previewed on the monitor XP's own
Display Properties draws. The monitor and the wallpaper list both come out of
the user's XP media — the artwork is `themeui.dll`'s, extracted at setup time
like the fonts and Luna, never shipped. Without that media the preview falls
back to the wallpaper alone rather than inventing a monitor.

Only the Desktop tab uses the monitor, which is what XP does: Themes and
Appearance preview a *window* instead. **Apply on the Desktop tab is cheap** —
it sets a wallpaper and a colour, with no package regeneration and no
plasmashell restart. **Customize Desktop...** (the Desktop Items dialog) is
disabled, as are the Effects... and Advanced-adjacent parts of Appearance that
xp2plasma has nothing to say about.

**Save As...** writes a real `.theme` under `assets/themes/Saved/`, including
the selected style/variant or Classic metrics and the image/solid-color desktop
settings. It references local assets; it is not a portable asset archive. Other
wallpaper plugins or differing per-desktop wallpapers are reported as unsupported
for saving. Existing presets are never overwritten. Delete only removes presets
saved by this editor, retaining a recoverable `.theme.deleted` copy. Saved Classic
color schemes remain in Appearance, not in Themes.

## Usage

```sh
xp2plasma path/to/Luna.msstyles     # convert, install and apply
xp2plasma path/to/Luna.theme        # a .theme bundle (picks the right variant)
xp2plasma classic                   # Windows Classic, from a colour scheme
xp2plasma classic --scheme list     # XP's stock appearance schemes
xp2plasma edit                      # the Display Properties editor
xp2plasma --remove-widget-style     # undo the Kvantum style and restore yours
```

Useful flags: `--no-apply` (generate and install only), `--no-widgets`,
`--no-panel`, `--no-layout`, `--no-wallpaper`, `--panel-height`,
`--positioning`, `--name`.

## How it fits together

| Module | Job |
| --- | --- |
| `msstyles_parse.py` | reads the PE resources of a `.msstyles` into parts, states and properties |
| `aurorae_gen.py` | KWin window decoration package |
| `plasmatheme_gen.py` | Plasma panel/desktop theme and the Start button |
| `panel_widgets.py` | Managed user-local tray/clock QML: XP tray artwork, horizontal hidden-icon expansion, 96-DPI clock sizing; native actions retained |
| `xp_icons.py` | Extracts original PE icon groups, native-size PNGs and labelled catalogs; maps verified resource IDs to a reversible Windows XP icon theme with native fallback |
| `kvantum_gen.py` | Kvantum widget theme (SVG + kvconfig) |
| `classic_gen.py` | the Windows Classic look, from a colour scheme |
| `colorscheme_gen.py`, `fonts_gen.py`, `panel_layout.py`, `start_applet.py` | colours, fonts, taskbar layout, forked Kickoff launcher |
| `apply.py`, `widget_style.py` | install and apply to the live session |
| `appearance_editor.py` | the Display Properties editor |
| `install.py` | the native installer (Qt bindings + app + launchers + desktop entry) |
| `system_qt.py` | links the editor against the host's own Qt on Arch-family hosts, so native style plugins load |
| `xp_style.py` | SVG-based approximation of xp2plasma's own theme, used only when the real style plugin can't load |
| `revert.py` | back to Breeze: default look, panel layout, wallpaper; removes our generated packages |
| `setup_ui.py` | shared job UI: a check-mark steps table with the raw log behind a Details toggle, and a download progress pop-up |
| `wizard.py` | the XP-styled setup wizard the AppImage runs |
| `hostexec.py` | the single seam for every external command and host file read |
| `paths.py` | where the app's own files live |
| `setup_assets.py` | first-run asset import and download |

Two invariants are worth knowing before changing anything:

- **Every external command goes through `hostexec.run()`.** Running from the
  AppImage, a bare `subprocess.run` inherits the bundle's `PATH` and library
  paths, so it can pick up the AppImage's own `python3` instead of the host's,
  or load the bundle's Qt into a host program. Both fail in ways that look like
  success — the worst being a native venv built by an interpreter that vanishes
  when the mount goes away. `hostexec` strips those variables from everything it
  spawns, subtracts the mount from `PATH`, and installs a guard that turns a
  call which bypassed it into a loud error.
- **Paths come from `expanduser("~")`, never from `XDG_*` environment
  variables.** What matters is writing where the running session will look, and
  the AppImage prepends its own directories to the `XDG_*_DIRS` search paths —
  so a theme written to an XDG-derived path could install "successfully" into
  the mount and change nothing. `paths.py` is the single source of truth.

## Tests

```sh
QT_QPA_PLATFORM=offscreen .venv/bin/python3 tests/test_editor.py
```

Each `tests/test_*.py` runs standalone the same way. The suite never touches
the live session; every apply entry point is stubbed.

`appimage/verify.sh` is the other kind of check: it runs the built AppImage for
real, and reads the glibc floor out of the shipped binaries, which the wheel
tags alone do not tell you.
