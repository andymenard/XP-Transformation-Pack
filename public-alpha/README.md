# XP Transformation Pack — Alpha 1.0

A KDE Plasma transformation pack **based on [hotsocket's XPlasma](https://tangled.org/hotsocket.fyi/xplasma/)**. The native visual-style engine, Qt style, Properties editor and desktop generators originate in that project. This repository contains our ports, integration and changes; it does not claim authorship of the upstream foundation.

The alpha includes the XP desktop by default and the IE6 Firefox theme optionally. XP Explorer, Control Panel, accessories, games, custom tray panels, Codex colors and Clippy are excluded. Existing Linux applications still receive XP widget styling. XPlasma Properties and its theme configuration remain included.

Desktop features include Start/taskbar/clock, decorations, dark application colors, icon-resolution options, fonts/cursors/sounds/wallpapers, desktop shortcuts, checkered selection/snapping, caption animations, session dialogs, lock/login/boot screens and the XP Welcome session splash.

The installer follows the blue graphical XP Setup sequence, with inset component-selection dialogs, progress/sidebar stages and rotating information pages. Visual reference: [Gough Lui's Windows XP install video](https://www.youtube.com/watch?v=vTheKjAtnKs&t=134s), from 2:14 onward, and the user's own XP installation media. Branding identifies this transformation pack and its upstream author.

## Attribution and licensing

Read [CREDITS.md](CREDITS.md), [LICENSE](LICENSE), [native/THIRD-PARTY-NOTICES](native/THIRD-PARTY-NOTICES) and [installer/THIRD-PARTY-NOTICES](installer/THIRD-PARTY-NOTICES). Retain each reused component's notices and licenses. This modified project is not endorsed by upstream developers or Microsoft.

## Assets and builds

This public repository is source only. Original Microsoft fonts, visual styles, sounds and imagery and third-party stock photographs are not uploaded. See [ASSET-DISTRIBUTION.md](ASSET-DISTRIBUTION.md). A local installer assembled from the user's supplied assets is a separate test artifact; this source checkout alone is not that full installer.

Build Desktop with `cmake -S native -B build/desktop -G Ninja -DQT_MAJOR=6` and `cmake --build build/desktop`. Native components must match the receiving machine's KDE/Qt. The optional browser sources are under `browser/ie6`. Source includes the caption effect and original third-party notices.

## Updates

The public updater uses anonymous HTTPS GitHub release requests to `andymenard/XP-Transformation-Pack`; users do not need GitHub CLI or an account. Alpha releases use semantic versions such as `1.0.0-alpha.1`. The alpha channel can select complete signed prereleases; stable clients do not select prereleases. The bundled publisher key still verifies release manifests and file hashes before installation. No private signing key or authentication token is shipped.

Until a complete signed alpha release is published, Check for updates reports that no complete signed alpha update is available. Public-repository creation does not publish proprietary assets or make the private development repository public.
