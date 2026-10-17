# XP-rendered pixel Tahoma

The capture utility runs inside actual Windows XP 5.1. It selects Tahoma at
11 pixels (XP's normal 8-point UI size at 96 DPI), separately at weights 400 and
700, and reads monochrome, hinted glyph bitmaps with `GetGlyphOutlineW` using
`GGO_BITMAP | GGO_GLYPH_INDEX`. Font smoothing is explicitly disabled. It refuses
other Windows versions. The output files are `C:\XPFONT\regular.xpg` and
`C:\XPFONT\bold.xpg`; incomplete captures are rejected by the builder.

Build the helper with a 32-bit Windows cross-compiler, targeting Windows 5.1 and
linking kernel32, gdi32 and user32. With an existing Zig 0.13 compiler:

```
python3 build_capture.py --zig /path/to/zig capture.exe
```
 It uses no C runtime. Compile `capture.c` with
`mainCRTStartup` as its entry point. The VM used for development has no network
adapter and uses a separately supplied XP installation ISO.

On Linux, install fonttools in an isolated environment and run:

```
python build.py regular.xpg xplasma-tahoma-pixel.ttf
python build.py bold.xpg xplasma-tahoma-pixel-bold.ttf
```

No FreeType/Wine rendering is used to generate these fonts. Every captured pixel
run becomes an exact rectangular outline, preserving GDI bearings and advances.
The output JSON records the XP build, capture hash and generated font hash.
The optional validation tests use FreeType only to check that these completed
outlines render back to the captured pixels.

The family is `XPlasma Tahoma Pixel`, with real regular and bold faces. Crisp
mode requests monochrome rendering and rounds the size to physical pixels.
Integer multiples reproduce uniform blocks; fractional scales distribute the
widths/heights over neighboring integer pixel sizes. This preserves hard edges
and Tahoma's captured proportions, but some blocks must be unequal at fractional
scales. At normal size, the source is XP's real rendering rather than a Linux
approximation. Larger font settings scale this 11-pixel design.

The generator and capture code are GPL-3.0-or-later. Tahoma's source letterforms
are Microsoft assets; the code license does not relicense those letterforms.
Generated font assets are kept separately from source in `private-assets`, with
Microsoft attribution and capture provenance. Existing legacy pixel-font credits
remain in the parent CREDITS.md.

## Optical designs for fractional scales

`optimize.py` creates separate regular/bold families at 14, 17, 19, 25 and
28 physical pixels (normal UI text at 125%, 150%, 175%, 225% and 250%).
These are adaptations of the XP capture, not claimed to be original XP
rendering at those sizes. A constrained integer-grid design balances repeated
long stems and assigns most rounding adjustments to counters and spacing.
Baselines and advances are anchored; every source grid cell stays at least
one pixel wide/high, preserving openings and connectivity. Complex glyphs
can relax a stem constraint to keep the full glyph within its target bounds;
per-face JSON reports record the counts.

Qt and IE6 select a matching optical family automatically by physical size;
other sizes fall back to the faithful captured face. Integer scales retain
that faithful face. The fonts have no grayscale edges, but fractional designs
remain optical compromises rather than perfectly uniform magnifications.

```
python optimize.py regular.xpg xplasma-tahoma-pixel-28.ttf --size 28
python optimize.py bold.xpg xplasma-tahoma-pixel-28-bold.ttf --size 28
```
