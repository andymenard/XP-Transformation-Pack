# Baked custom colors

Live desktop tint rendering has been retired. Custom Colors keeps its color and
independent Auto/White/Black text controls, title/taskbar linking, red Close,
scrollbars, Start-menu colors/dividers, and system accent colors.

Color choices generate a static msstyles using the former live shader's
luminance mapping, including its highlights and shadows. Apply installs ordinary
Start, panel and Aurorae artwork and sets the system palette. No live scopes,
shader layers, image recoloring, configuration polling or tint renderers run
while moving or resizing windows. Close retains its red artwork. Start text
shadows are black for white text and disabled for black text.

Old `.theme` files containing `[XPlasma] LiveColors` are preserved and converted
to static styles when selected or applied through the command line. New themes
save the generated static style plus editable color preferences marked static. Separate Start-menu text is stored in generated
style metadata and in custom Classic schemes. Cached generated styles avoid
rebuilding an unchanged profile. Profiles can take longer to apply than live
uniform edits; normal window rendering has no tint overlay overhead.

The former live QML scopes are inert compatibility components. The native module
remains available for Start-menu resize handling, but no tint settings singleton
or tint surface is instantiated by the desktop. The widget style no longer
watches live configuration or accepts runtime tint injection. The legacy live
configuration API only disables overlays and restores owned palette keys.
