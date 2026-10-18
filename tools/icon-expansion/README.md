# XP icon coverage

The expanded layer covers every named SVG in the installed Breeze theme and
every themed icon requested by installed KDE desktop entries. The audit is
explicitly scoped to those inventories; application-embedded artwork and
absolute file icons do not use icon-theme lookup. Run audit.py again after
installing new software or upgrading KDE.

Sources are credited in CREDITS.md:

- marchmountain's CC0 XP high-resolution recreations supply XP counterparts.
- Original XPlasma controls supply mask-safe submenu arrows and window glyphs.
- Specialized KDE operations with no XP counterpart retain their original
  geometry with XP blue/bevel styling, rather than losing their meaning to a
  generic gear. Unmodified SVG sources and the KDE license notice accompany
  those derivatives. They are not original Microsoft icons or CC0 artwork.

The user's original XP icons remain in the primary theme. Inheritance and
exact control/state aliases survive preset regeneration. Full-color browser
Back/Forward buttons remain distinct from monochrome submenu arrows.

Build into a new, empty directory using Pillow and PySide6/PyQt6 with QtSvg:

    python3 build.py /path/to/Windows.XP.High.Resolution.Icon.Pack.2.zip /path/to/xplasma-xp-extended
    python3 audit.py /path/to/xplasma-xp-extended --report /tmp/icon-audit.json
    python3 install.py --source /path/to/xplasma-xp-extended

ALIAS-PROVENANCE.json records each alias, source, role and substitution reason.
COVERAGE.json counts the source types; UNMAPPED.json records genuinely unknown
external app names. Recreated brand artwork is not invented for unknown apps.
The image/link/index audit and actual Qt lookups at 16 and 48 pixels must pass
before installation and packaging. The current build has seven size variants,
16 through 128 logical pixels. Reopen applications to refresh their icon cache.

Tests cover submenu-mask silhouettes and RTL, corrected Discover roles, stale
converted-primary refresh, state preservation and repeat installation. A native
Qt test regenerates control glyphs across three theme writes.

Control arrows and other small UI marks are palette-aware SVGs with KDE’s `ColorScheme-Text` class. Both supplying icon themes set `FollowsColorScheme=true`; fixed-color XP pictograms remain PNGs. Installation migrates older black control PNGs to the same vector silhouettes and refreshes owned-file fingerprints.

`tests/palette.cpp` verifies white and black foreground rendering with the native KDE icon loader. Build with Qt6Widgets and KF6IconThemes, then pass the SVG path and optional theme name.
