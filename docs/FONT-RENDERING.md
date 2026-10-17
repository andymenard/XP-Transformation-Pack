# Separate Tahoma interface and content rendering

**Tahoma OS** regular and bold use the original Tahoma glyph outlines, metrics,
hinting programs and style weights. Only naming records and the required SFNT
checksums change. Copyright, trademark, license and embedding metadata remain
unchanged; the files are not relicensed under GPL. Fonts and source/derived hashes
are included under installer/assets/fonts/tahoma-os/.

The standard-library-only generator is native/src/app/data/fonts/tahoma_os.py,
with an identical installer copy. Native apply generates/refreshes the local
faces from the originals and registers them before selecting desktop fonts.

Desktop/interface font choices that specify Tahoma select Tahoma OS instead.
Ordinary Tahoma has antialiasing and full hinting enabled. Generic sans-serif
prefers ordinary Tahoma; serif, monospace and explicit document/code choices
retain their existing defaults. Legacy fontconfig rules match the OS family,
not ordinary Tahoma. Custom font choices remain unchanged.

The **Crisp XP text** toggle is separate: when enabled it selects the existing
XP-captured pixel family (including optical sizes), with its existing fallback.
When disabled it restores the Tahoma OS outline family, size and weight. Qt
widget UI and QML Start-menu/clock text recognize Tahoma OS. No web-content font
rule is tied to the crispy toggle.

Versioned migration runs even when the display scale is unchanged, and retains
later user edits to global rendering settings. Fully quit and reopen applications
to refresh font caches; logout/login refreshes all desktop font selections.
