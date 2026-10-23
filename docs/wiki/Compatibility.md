# Compatibility and downloads

## Check before installing

1. Back up your personal files and desktop/system configuration. Save your work.
2. Open **System Settings → About this System** (the location/name can vary), or run `plasmashell --version` in a terminal.
3. Check your distribution release and architecture. `uname -m` reports `x86_64` for Intel/AMD 64-bit or `aarch64` for ARM64.
4. Choose an installer from the [1.0.1 release](https://github.com/andymenard/XPlasma-Plus/releases/tag/v1.0.1).

## Available installers

| Filename ending | Intended matching prebuilt target |
| --- | --- |
| `kubuntu-26.04-amd64.tar.gz` | Kubuntu/Ubuntu 26.04, Intel/AMD 64-bit |
| `kubuntu-26.04-arm64.tar.gz` | Kubuntu/Ubuntu 26.04, ARM64 |
| `fedora-44-amd64.tar.gz` | Fedora 44, Intel/AMD 64-bit |
| `fedora-44-arm64.tar.gz` | Fedora 44 ARM64; also includes a matching Fedora Asahi Remix build |
| `generic.tar.gz` | Builds locally on supported KDE systems |

**Build availability is not hardware testing.** Only Asahi Linux ARM64 and Kubuntu Intel/AMD have received hands-on testing. Four native CI targets passed automated build/regression checks.

Prebuilt components are accepted only when distribution, release, architecture, Qt, Plasma and source identity match. The caption effect also checks the exact KWin build. Otherwise supported components build locally, which takes longer. Internet access is needed for Linux dependencies, including when using a prebuilt installer. Original XP resources are bundled; an XP ISO is not needed.

## Plasma and Qt versions

The generic source installer accepts **Plasma 6.x** and builds its core theme engine locally against **Qt 6.6 or newer**. The engine applies XP window borders, application controls, colors, icons and panel artwork using public interfaces. When XP widget replacements cannot load against the host, compatibility mode retains the distribution's launcher, task manager, clock and notification widgets and its panel layout. This includes stock Debian Trixie (Plasma 6.3 / Qt 6.8). Compatibility mode does not provide the exact XP Start menu or notification UI.

Optional widget build recipes are provided for **Plasma 6.6 / Qt 6.10+**, **Plasma 6.7 / Qt 6.11+** and **Plasma 6.8 / Qt 6.12+** (including Plasma 6.7.9x betas). The 6.8 recipe uses 6.7.91 sources. Session/logout, lock-screen and caption-animation integrations still require a matching recipe; Setup explains when these are unavailable. The custom tray integration requires Plasma 6.6 or newer. A generic installation can proceed with the remaining features.

Accepting a Plasma version is not evidence of live testing on that release. The source changes have been compiled with Qt 6.11 on ARM64 and exercised with mocked Plasma sessions; early Plasma 6 / Qt 6.6 builds, live Trixie/Void installations, and the 6.8 widget build still need distribution testing. The published 1.0.1 installer must be rebuilt to include these changes.

APT, DNF, pacman, Zypper and Void Linux XBPS dependency adapters are included in the source installer. Void support is experimental and has not received a live installation test. Non-systemd sessions use an XDG autostart watcher for theme and cursor changes. Automatic login-screen/display-manager management still requires systemd; configure your non-systemd display manager separately. Boot splash needs an existing splash-enabled Plymouth setup using dracut or update-initramfs. Unsupported features are disabled with an explanation in Setup.

If APT dependency installation fails, copy the output before the final `CalledProcessError`. The current installer refreshes package indexes before selecting library names and reports packages without installation candidates explicitly. A final pkexec exit code alone cannot distinguish authentication, repository, network or dependency errors.

Continue to [Installation](Installation.md).
