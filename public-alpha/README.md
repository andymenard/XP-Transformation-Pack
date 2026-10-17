# XP Transformation Pack — Alpha 1.0

A KDE Plasma transformation pack **based on [hotsocket's XPlasma](https://tangled.org/hotsocket.fyi/xplasma/)**. The native visual-style engine, Qt style, Properties editor and desktop generators originate in that project. This repository contains our ports, integration and changes; it does not claim authorship of the upstream foundation.

The alpha includes the XP desktop by default and the IE6 Firefox theme optionally. XP Explorer, Control Panel, accessories, games, custom tray panels, Codex colors and Clippy are excluded. Existing Linux applications still receive XP widget styling. XPlasma Properties and its theme configuration remain included.

Desktop features include Start/taskbar/clock, decorations, dark application colors, icon-resolution options, fonts/cursors/sounds/wallpapers, desktop shortcuts, checkered selection/snapping, caption animations, session dialogs, lock/login/boot screens and the XP Welcome session splash.

The installer follows the blue graphical XP Setup sequence, with inset component-selection dialogs, progress/sidebar stages and rotating information pages. Visual reference: [Gough Lui's Windows XP install video](https://www.youtube.com/watch?v=vTheKjAtnKs&t=134s), from 2:14 onward, and the user's own XP installation media. Branding identifies this transformation pack and its upstream author.

## Attribution and licensing

Read [CREDITS.md](CREDITS.md), [LICENSE](LICENSE), [native/THIRD-PARTY-NOTICES](native/THIRD-PARTY-NOTICES) and [installer/THIRD-PARTY-NOTICES](installer/THIRD-PARTY-NOTICES). Retain each reused component's notices and licenses. This modified project is not endorsed by upstream developers or Microsoft.

## Assets and builds

This public repository is source only. Original Microsoft fonts, visual styles, sounds and imagery and third-party stock photographs are not uploaded. See [ASSET-DISTRIBUTION.md](ASSET-DISTRIBUTION.md). The complete installer is provided separately as an alpha prerelease asset; it includes the publisher-supplied XP resources and retains their separate copyright notices. This source checkout alone is not that full installer.

Build Desktop with `cmake -S native -B build/desktop -G Ninja -DQT_MAJOR=6` and `cmake --build build/desktop`. Native components must match the receiving machine's KDE/Qt. The optional browser sources are under `browser/ie6`. Source includes the caption effect and original third-party notices.

## New releases

Prereleases use complete installer downloads. OTA updates are not included and
there is no background update client. **Download New Releases** in Properties
opens the GitHub releases page in your browser. Signed checksums are provided
for verifying each installer download.

Read the new release's installation and migration notes before changing an
existing installation. The current installer requires removal of an overlapping
installation before reinstalling; an in-place upgrade is not supported yet.

## Download the alpha installer

See [Alpha 1.0](https://github.com/andymenard/XP-Transformation-Pack/releases/tag/v1.0.0-alpha.7).
Extract the installer tar.gz, then launch **Start-Installer.desktop** from the
extracted folder as your desktop user. Linux dependencies require internet
access; an XP ISO is not needed. Desktop is selected by default; Firefox is optional.
The release is a testing prerelease, with compatibility and validation limits
listed in its notes. Signed checksums authenticate the installer download; this
release does not activate an OTA payload.

## Interface fonts

The installer includes **Tahoma OS** regular and bold, renamed from Microsoft
Tahoma to separate XP interface rendering from smooth Tahoma app content. The
font assets retain their separate Microsoft rights; see
[font provenance and notices](installer/assets/fonts/tahoma-os/NOTICE.txt).
