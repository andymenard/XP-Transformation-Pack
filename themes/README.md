# Independent application dark mode

Every XPlasma theme has an independent Dark application colors checkbox in
Appearance. Shell bitmaps, layout values and font metrics remain those of the
selected theme. The bundled application overlay builder colors application
controls and text; Classic schemes receive a palette overlay. Cached variants
are keyed by the source file and builder bytes, so changes regenerate them.
The canonical runtime resources are native/src/app/data/dark-mode, embedded in
the native app. tools/dark-theme retains the standalone developer builder.

Dark mode persists across theme changes. IE6 Firefox receives the desktop's
appearance flag in ~/.local/share/xplasma/appearance.json; its chrome updates
live. Web-page artwork and the user's toolbar choices remain untouched.

Initial palette reference: Sufficient-Pea-9716's Night Mode Classic:
https://www.reddit.com/r/windows7/comments/1fgvoo0/the_night_mode_classic_theme_for_windows_7/
Its files are not redistributed. The palette here was authored independently.
