# Credits and licenses

XP Transformation Pack is based on **XPlasma by hotsocket (© 2026 Sock)**.
The native visual-style engine, Qt style, Properties editor and desktop generators
originate in XPlasma. The transformation-pack contributors provide adaptations,
installer integration and additional desktop features.

The project code is GPL-3.0-or-later unless a file states otherwise. Third-party
code, fonts and artwork retain their own copyrights and licenses. The complete
notices and license texts are available in the installer’s Credits and licenses
window and XPlasma Properties.

## Included projects and artwork

| Creator / team | Project | Contribution and license |
| --- | --- | --- |
| hotsocket | [XPlasma](https://tangled.org/hotsocket.fyi/xplasma/) | Native XP visual-style engine and desktop/Properties foundation; GPL-3.0-or-later. |
| matthewmx86 | [RetroThemesFirefox](https://github.com/matthewmx86/RetroThemesFirefox) | Optional IE6 Firefox theme foundation and legacy chrome assets; BSD-2-Clause. |
| marchmountain | [Windows XP High Resolution Icon Pack](https://github.com/marchmountain/-Windows-XP-High-Resolution-Icon-Pack) | High-resolution XP icon recreations; CC0-1.0. |
| lou | [MS Sans Serif](https://fontstruct.com/fontstructions/show/1384746) | Optional unmodified regular/bold pixel fonts, copyright 2017; CC-BY-SA-3.0. |
| Adam Hammad and Jordan Scales | [XP.css](https://github.com/botoxparty/XP.css), [98.css](https://github.com/jdan/98.css) | Distribution source for the pixel fonts; the fonts retain their separate license. |
| Aleix Pol Gonzalez, David Edmundson and KDE contributors | [Plasma Workspace](https://invent.kde.org/plasma/plasma-workspace) | Session/logout dialog and timer adaptations, GPL-2.0-or-later; lock-screen MainBlock adaptation, LGPL-2.0-or-later. |
| KDE community / KDE e.V. | [Plasma](https://kde.org/plasma-desktop/) | Desktop and widget foundations; individual source licenses apply. Classic KDE logo: LGPL-2.1-or-later. |
| Uri Herrera and KDE Breeze contributors | [Breeze Icons](https://invent.kde.org/frameworks/breeze-icons) | Geometry for specialized XP-styled glyphs; original artwork licenses retained, including LGPL-2.1-or-later and CC-BY-SA-4.0. |
| Larry Ewing and The GIMP; Simon Budig; Garrett LeSage | [Tux](https://isc.tamu.edu/~lewing/linux/) | Original Tux and vector adaptations; acknowledgement notices and CC0 contribution retained. |
| aubymori | [Classic Min/Max Animations](https://windhawk.net/mods/classic-min-max-animations) | Behavioral reference for the independently implemented KWin caption animation; no Windhawk implementation code copied. |
| Mozilla and contributors | [Firefox](https://www.mozilla.org/firefox/) | Optional Firefox ESR runtime; the original distribution includes its component notices. |
| Gough Lui | [Microsoft Windows XP Install Process](https://www.youtube.com/watch?v=vTheKjAtnKs&t=134s) | Visual reference for the installer from 2:14 onward; no video frames or audio redistributed. |
| Microsoft and original asset creators | [Windows XP](https://www.microsoft.com/en-us/legal/intellectualproperty/copyright/permissions) | Original visual styles, icons, fonts, cursors, sounds and artwork retain their separate rights. |

## Libraries and runtime dependencies

- Tim Kientzle and libarchive contributors — [libarchive](https://www.libarchive.org/), BSD-style notices; bundled reader source.
- Julian R. Seward — [bzip2](https://sourceware.org/bzip2/), bzip2 license; statically linked where available.
- Sébastien Eustace and contributors — [TOMLKit](https://github.com/python-poetry/tomlkit), MIT; bundled installer dependency.
- The Qt Company and contributors — [Qt](https://www.qt.io/) and [Qt for Python / PySide](https://doc.qt.io/qtforpython/); Riverbank Computing — [PyQt](https://www.riverbankcomputing.com/software/pyqt/). Runtime components use their distribution-provided licenses.
- Python Software Foundation and contributors — [Python](https://www.python.org/).
- Pillow contributors — [Pillow](https://python-pillow.github.io/), HPND; image processing tools.
- fontTools contributors — [fontTools](https://github.com/fonttools/fonttools), MIT; font generation tools. Freetype-py contributors — [freetype-py](https://github.com/rougier/freetype-py), BSD-3-Clause; font verification tools.
- zlib contributors — [zlib](https://zlib.net/), zlib license; Tukaani contributors — [XZ Utils / liblzma](https://tukaani.org/xz/), component-specific notices. System compression libraries are linked by the archive reader.
- Plymouth contributors — [Plymouth](https://gitlab.freedesktop.org/plymouth/plymouth); SDDM contributors — [SDDM](https://github.com/sddm/sddm). The boot and login themes run on distribution-provided software.

Distribution-installed dependencies and separately supplied runtimes retain their
own bundled license information. Credits for historical applications excluded
from Alpha 1.0 are not presented as included components.

## Fonts and original assets

Optional XPlasma Tahoma Pixel regular/bold and numbered optical families are
captured or adapted from Microsoft Tahoma using Windows XP SP3’s monochrome GDI
renderer. Microsoft retains the original letterform copyright. These generated
fonts are separate from the project code’s GPL license; an embedding flag does
not establish redistribution permission. See the bundled font provenance.

## Wallpaper credits

The following attributions are recorded in the supplied files’ Artist metadata;
they do not establish redistribution licenses. Resized and cropped variants keep
the same attribution. Charles O’Rear’s Bliss attribution is also documented by
[UCR ARTS](https://ucrarts.ucr.edu/education/bliss/).

| Photographer / agency | Files carrying that attribution |
| --- | --- |
| Mark Karrass | Ascent (1200x1920).jpg, Ascent (3840x2160).jpg, Ascent.jpg, Sample Pictures — Blue hills (1200x1920).jpg, Sample Pictures — Blue hills (3840x2160).jpg, Sample Pictures — Blue hills.jpg, Sample Pictures — Winter (1200x1920).jpg, Sample Pictures — Winter.jpg |
| Peter Burian | Autumn (1200x1920).jpg, Autumn (3840x2160).jpg, Autumn.jpg |
| Bill Ross | Azul (1200x1920).jpg, Azul (3840x2160).jpg, Azul.jpg, Tulips.jpg |
| Charles O'Rear | Bliss (1200x1920).jpg, Bliss (3840x2160).jpg, Bliss.jpg, Red moon desert.jpg |
| Warren Morgan | Follow.jpg |
| Chris Carroll | Friend.jpg |
| Richard Hamilton Smith | Moon flower (Unedited Version).jpg |
| Stephen Frink | Plus! — Plus! Aquarium.jpg |
| Kim Westerskov | Plus! — Plus! Aquarium2.jpg |
| InterNetwork Media | Plus! — Plus! Space (1200x1920).jpg, Plus! — Plus! Space.jpg |
| Roger Ressmeyer | Radiance (1200x1920).jpg, Radiance.jpg, Stonehenge (Getty Images Rescan).jpg |
| Craig Aurness | Sample Pictures — Beta — Surfer.jpg |
| Image Plan | Sample Pictures — Media Center Edition — GalleryPlayer — REFLECTION - GALLERYPLAYER.jpg |
| Reed Kaestner | Sample Pictures — Media Center Edition — Overlooking Rio.jpg |
| Mick Roessler | Sample Pictures — Sunset.jpg |
| Timothy Alt | Vortec space.jpg |
| Jeff Vanuga | Wind.jpg |

89 image files have no Artist metadata; their photographers remain unresolved. The supplied SOURCE-NOTES.txt identifies Getty/Corbis and other sources but does not supply complete per-image attribution. The collection compiler is also unidentified.


Original Microsoft and stock-photo assets retain their separate rights; the
project’s code licenses do not grant redistribution rights to those assets.
See [ASSET-DISTRIBUTION.md](https://github.com/andymenard/XP-Transformation-Pack/blob/main/ASSET-DISTRIBUTION.md).

Windows and Windows XP are trademarks of Microsoft Corporation. KDE and the KDE
logo are trademarks of KDE e.V. This project is not endorsed by Microsoft, KDE,
or the upstream projects credited above.
