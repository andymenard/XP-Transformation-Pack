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

The bundled native Plasma integrations support **Plasma 6.6 / Qt 6.10+** and **Plasma 6.7 / Qt 6.11+**. **Plasma 6.8 is not fully supported in 1.0.1**: taskbar integration, session/logout dialogs, lock screen and caption animations are disabled by the installer's version checks. A generic installer does not bypass these limits.

APT, DNF, pacman and Zypper dependency adapters are included. This is not a promise of compatibility with every distribution. Unsupported features are disabled with an explanation in Setup. Login/session integrations require systemd; boot splash needs an existing splash-enabled Plymouth setup using dracut or update-initramfs.

Continue to [Installation](Installation.md).
