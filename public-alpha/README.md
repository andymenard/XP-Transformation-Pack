# XPlasma Plus — 1.0.1 Public Beta

A KDE Plasma transformation pack and **modified fork of [hotsocket's XPlasma](https://tangled.org/hotsocket.fyi/xplasma/)**. The native visual-style engine, Qt style, Properties editor and desktop generators originate in that project. This repository contains our ports, integration and changes; it does not claim authorship of the upstream foundation.

The alpha includes the XP desktop by default and the IE6 Firefox theme optionally. XP Explorer, Control Panel, accessories, games, custom tray panels, Codex colors and Clippy are excluded. Existing Linux applications still receive XP widget styling. XPlasma Properties and its theme configuration remain included.

Desktop features include Start/taskbar/clock, decorations, dark application colors, icon-resolution options, fonts/cursors/sounds/wallpapers, desktop shortcuts, checkered selection/snapping, caption animations, session dialogs, lock/login/boot screens and the XP Welcome session splash.

The installer follows the blue graphical XP Setup sequence, with inset component-selection dialogs, progress/sidebar stages and rotating information pages. Visual reference: [Gough Lui's Windows XP install video](https://www.youtube.com/watch?v=vTheKjAtnKs&t=134s), from 2:14 onward, and the user's own XP installation media. Branding identifies this transformation pack and its upstream author.

## Licence

The source code in this repository — essentially any *text* files, such as C++, QML, Python, JavaScript and shell scripts — is licensed under the GPL; see [LICENSE](LICENSE). Reused components keep their own notices and licenses: read [CREDITS.md](CREDITS.md), [native/THIRD-PARTY-NOTICES](native/THIRD-PARTY-NOTICES) and [installer/THIRD-PARTY-NOTICES](installer/THIRD-PARTY-NOTICES). This modified project is not endorsed by upstream developers or Microsoft.

This licence does **not** cover the assets from Windows XP (images, icons, cursors, sounds, fonts and visual styles): those are still Microsoft's copyright. They are in this repository for convenience, all in one folder, [`non-free/`](non-free/NOTICE.md), and the installer marks the components that use them as non-free. Windows program files and installers, third-party stock photographs and the high-resolution photo rescans are not in the repository. See [ASSET-DISTRIBUTION.md](ASSET-DISTRIBUTION.md).

## Builds

The complete installer is provided separately as an alpha prerelease asset, with its resources' copyright notices; this source checkout alone is not that full installer.

Build Desktop with `cmake -S native -B build/desktop -G Ninja -DQT_MAJOR=6` and `cmake --build build/desktop`. Native components must match the receiving machine's KDE/Qt. The optional browser sources are under `browser/ie6`. Source includes the caption effect and original third-party notices.

## New releases

Prereleases use complete installer downloads. OTA updates are not included and
there is no background update client. **Download New Releases** in Properties
opens the GitHub releases page in your browser. Signed checksums are provided
for verifying each installer download.

Read the new release's installation and migration notes before changing an
existing installation. Running a newer installer over an existing installation
upgrades it in place, keeping your theme and settings; Remove still restores the
desktop from before the first installation.

## Download the installer

See [1.0.1 Public Beta](https://github.com/andymenard/XPlasma-Plus/releases/tag/v1.0.1).
Extract the installer tar.gz, then launch **Start-Installer.desktop** from the
extracted folder as your desktop user. Linux dependencies require internet
access; an XP ISO is not needed. Desktop is selected by default; Firefox is optional.
This is a public beta: please report problems on the repository's issue tracker.
Compatibility notes are in the release notes. Signed checksums authenticate the
installer download; this release does not activate an OTA payload.

## Interface fonts

The installer includes **Tahoma OS** regular and bold, renamed from Microsoft
Tahoma to separate XP interface rendering from smooth Tahoma app content. The
font assets retain their separate Microsoft rights; see
[font provenance and notices](installer/assets/fonts/tahoma-os/NOTICE.txt).
