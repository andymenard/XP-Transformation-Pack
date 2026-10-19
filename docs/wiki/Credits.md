# Credits and licenses

XPlasma Plus is a **modified fork of [XPlasma by hotsocket](https://tangled.org/hotsocket.fyi/xplasma/)**. The native visual-style engine, Qt style, Properties editor and desktop generators originate there. This pack adds its own ports, integration and features; it does not claim authorship of the upstream foundation.

The main inspiration is [Rozniak's Windows XP Total Conversion for XFCE](https://github.com/rozniak/xfce-winxp-tc). Please check out that project, especially if you use XFCE.

Other included foundations include [matthewmx86's RetroThemesFirefox](https://github.com/matthewmx86/RetroThemesFirefox), [marchmountain's Windows XP High Resolution Icon Pack](https://github.com/marchmountain/-Windows-XP-High-Resolution-Icon-Pack), KDE components, credited fonts and their dependencies. This is not the complete list.

## Thank you to the projects that made this possible

XPlasma Plus exists because other people shared their work. Thank you to the creators, maintainers, artists, translators, testers and contributors behind these projects. Please visit their projects, support their work and give them credit when sharing this pack.

### Desktop, browser, artwork and inspiration

| People / project | What we are thanking them for |
| --- | --- |
| **hotsocket (Sock) — [XPlasma](https://tangled.org/hotsocket.fyi/xplasma/)** | The upstream project we forked: the native XP visual-style engine, Qt application style, Properties editor and desktop generators. This is the foundation of XPlasma Plus. |
| **Rozniak and contributors — [Windows XP Total Conversion for XFCE](https://github.com/rozniak/xfce-winxp-tc)** | The main inspiration for this project. Please check out their work, especially if you use XFCE. This is an inspiration credit, not a claim that their code or assets are bundled here. |
| **matthewmx86 — [RetroThemesFirefox](https://github.com/matthewmx86/RetroThemesFirefox)** | The foundation and legacy chrome assets for our optional IE6-style Firefox theme. |
| **marchmountain — [Windows XP High Resolution Icon Pack](https://github.com/marchmountain/-Windows-XP-High-Resolution-Icon-Pack)** | The high-resolution XP icon recreations used by our HiDPI option. |
| **KDE contributors, including Aleix Pol Gonzalez and David Edmundson — [Plasma Workspace](https://invent.kde.org/plasma/plasma-workspace), [Plasma Desktop](https://invent.kde.org/plasma/plasma-desktop), [KWin](https://invent.kde.org/plasma/kwin) and [KDE Frameworks](https://develop.kde.org/products/frameworks/)** | The desktop, compositor, widgets, session/logout and lock-screen foundations, libraries and integration tools that make the pack possible. |
| **Uri Herrera and KDE Breeze contributors — [Breeze Icons](https://invent.kde.org/frameworks/breeze-icons)** | The original geometry behind specialized XP-styled replacement glyphs. Their original artwork notices and licenses are retained. |
| **lou — [MS Sans Serif](https://fontstruct.com/fontstructions/show/1384746); Adam Hammad and Jordan Scales — [XP.css](https://github.com/botoxparty/XP.css) and [98.css](https://github.com/jdan/98.css)** | The optional pixel fonts and the projects through which those font files were distributed. |
| **Larry Ewing and The GIMP; Simon Budig; Garrett LeSage — [original Tux](https://isc.tamu.edu/~lewing/linux/), [GIMP](https://www.gimp.org/), [Simon’s vector artwork](https://www.home.unix-ag.org/simon/penguin/README) and [Garrett’s Tux](https://github.com/garrett/Tux)** | Tux and its vector adaptations, used for the optional Start logo. |
| **aubymori — [Classic Min/Max Animations](https://windhawk.net/mods/classic-min-max-animations)** | The behavioral reference for our independently implemented KWin caption animation. No Windhawk implementation code was copied. |
| **Mozilla and contributors — [Firefox](https://www.mozilla.org/firefox/)** | The modern Firefox ESR browser beneath the optional IE6 appearance. |
| **[Plymouth](https://gitlab.freedesktop.org/plymouth/plymouth) and [SDDM](https://github.com/sddm/sddm) contributors** | The boot-splash and login-manager software on which our themes run. |

### Libraries, tools and the Linux foundation

- **The Qt Company and Qt contributors; Riverbank Computing** — [Qt](https://www.qt.io/), [Qt for Python / PySide](https://doc.qt.io/qtforpython/) and [PyQt](https://www.riverbankcomputing.com/software/pyqt/) provide the native interface, QML and Python GUI foundations.
- **The Python Software Foundation and Python contributors** — [Python](https://www.python.org/) powers the installer and integration tools.
- **Tim Kientzle and libarchive contributors; Julian R. Seward; Sébastien Eustace and TOMLKit contributors** — [libarchive](https://www.libarchive.org/), [bzip2](https://sourceware.org/bzip2/) and [TOMLKit](https://github.com/python-poetry/tomlkit) provide archive reading, compression and configuration support.
- **Pillow, fontTools, freetype-py and FreeType contributors** — [Pillow](https://python-pillow.github.io/), [fontTools](https://github.com/fonttools/fonttools), [freetype-py](https://github.com/rougier/freetype-py) and [FreeType](https://freetype.org/) support image processing, font generation and font verification.
- **zlib, zlib-ng and Tukaani contributors** — [zlib](https://zlib.net/), [zlib-ng](https://github.com/zlib-ng/zlib-ng) and [XZ Utils / liblzma](https://tukaani.org/xz/) provide compression libraries used by the build and archive stack.
- **GNOME, GTK and Fontconfig contributors** — [GTK](https://www.gtk.org/), [GLib / GSettings](https://gitlab.gnome.org/GNOME/glib) and [Fontconfig](https://www.freedesktop.org/wiki/Software/fontconfig/) support application styling, settings and font selection.
- **The Linux kernel, Asahi Linux and distribution communities** — [Linux](https://www.kernel.org/), [Asahi Linux](https://asahilinux.org/), [Kubuntu](https://kubuntu.org/), [Ubuntu](https://ubuntu.com/), [Fedora](https://fedoraproject.org/), [Arch Linux](https://archlinux.org/) and [openSUSE](https://www.opensuse.org/) provide the platforms, packages and build environments used or targeted by the pack. Inclusion here does not mean every platform has been tested.
- **The wider Linux desktop and graphics communities** — [Wayland](https://wayland.freedesktop.org/), [X.Org](https://www.x.org/), [Mesa](https://www.mesa3d.org/), [D-Bus](https://www.freedesktop.org/wiki/Software/dbus/), [systemd](https://systemd.io/) and [polkit](https://gitlab.freedesktop.org/polkit/polkit) provide essential display, session and authorization infrastructure.
- **Build-tool maintainers and contributors** — [CMake](https://cmake.org/), [Ninja](https://ninja-build.org/), [Extra CMake Modules](https://invent.kde.org/frameworks/extra-cmake-modules), [pkgconf](https://github.com/pkgconf/pkgconf), [GCC](https://gcc.gnu.org/), [LLVM / Clang](https://llvm.org/) and [Git](https://git-scm.com/) make building, testing and maintaining this work possible.

These thanks accompany the **[full credits and licenses](https://github.com/andymenard/XPlasma-Plus/blob/main/CREDITS.md)**, [native notices](https://github.com/andymenard/XPlasma-Plus/blob/main/native/THIRD-PARTY-NOTICES) and [installer notices](https://github.com/andymenard/XPlasma-Plus/blob/main/installer/THIRD-PARTY-NOTICES); they do not replace them or imply endorsement by the original authors. The dependency stack contains additional contributors acknowledged in each project's own notices.

Original Microsoft XP assets and the separately credited wallpaper photography are not open-source project code. Their rights and attributions remain separate; see [asset distribution and provenance](https://github.com/andymenard/XPlasma-Plus/blob/main/ASSET-DISTRIBUTION.md). Thank you also to **[Gough Lui](https://www.youtube.com/watch?v=vTheKjAtnKs&t=134s)** for the visual reference used for the installer.

The authoritative attribution and license records are:

- [Complete credits, creators and project links](https://github.com/andymenard/XPlasma-Plus/blob/main/CREDITS.md)
- [Native third-party notices](https://github.com/andymenard/XPlasma-Plus/blob/main/native/THIRD-PARTY-NOTICES)
- [Installer third-party notices](https://github.com/andymenard/XPlasma-Plus/blob/main/installer/THIRD-PARTY-NOTICES)
- [Asset distribution and provenance](https://github.com/andymenard/XPlasma-Plus/blob/main/ASSET-DISTRIBUTION.md)
- [Project code license](https://github.com/andymenard/XPlasma-Plus/blob/main/LICENSE)

Setup's **Credits and licenses** button and Properties' **About** tab also expose notices. Project code is GPL-3.0-or-later unless a file states otherwise; third-party components retain their licenses. Original Microsoft XP assets and fonts retain separate rights and are not relicensed by the project's GPL. This project is not endorsed by Microsoft, KDE or the upstream authors.
