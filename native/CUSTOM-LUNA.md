Custom Luna is available in XPlasma Plus Properties. Choose four colors for the Start button, taskbar, titlebars and borders, and titlebar buttons. Close always retains its original red; the button color applies to Minimize, Maximize, Restore and Help. The preview updates immediately; Apply updates the desktop. Cancel leaves the desktop unchanged. Reset to original Luna selects the unmodified Blue Luna, including its red Close button. Selecting another appearance or theme exits Custom Luna.

The editor generates standalone, immutable .msstyles files under ~/.local/share/xplasma/custom-luna, with a JSON sidecar retaining the source and color choices. It preserves Luna's bitmap dimensions, shading, alpha, transparency keys, and other color variants. No renderer or installed style plugin changes are needed. Custom looks also work with Save As presets.

Source was copied from the existing XPlasma/native source tree into this workspace. The updated editor is installed at ~/.local/share/fedora-xp-installer/runtime/bin/xplasma-edit. The original editor is retained there as xplasma-edit.pre-custom-luna.bak.

Validation performed:
- Qt 6 native editor build.
- test-customluna against the installed original Luna: all four areas change; bitmap sizes, alpha, transparency keys, and other variants are preserved; regeneration is stable; missing input fails.
- Offscreen screenshots of all seven tabs, Advanced, and Extras, including reopening a saved custom palette.
- Offscreen UI smoke checks (tests/manual/customluna_ui.cpp) for restored palette, reset, enable, choosing a new color, and cancel. Reset was also checked with the asset library accessed through a symbolic link.

Build: cmake -S native -B native/build-dev -G Ninja -DQT_MAJOR=6
Then: cmake --build native/build-dev --target xplasma-edit test-customluna
Generator check: native/build-dev/tests/app/test-customluna /path/to/Luna/luna.msstyles

Apply crash fix (2026-10-15): the reported core and journal showed a Plasma shutdown abort (QPixmap creation after QGuiApplication teardown), during the second of two immediate restarts. Existing XP panels now receive Start data and pending AppletOrder changes before one combined restart. The applyAndReset regression asserts exactly one stop for an existing XP panel. Both that test and restartWithoutSystemd pass. A live reapply of the user's saved custom colors completed with one restart and no new core dump. Fresh installations retain the package-discovery restart when the XP Start applet is not yet present.

Follow-up crash and color fix: a second core showed SIGBUS in KSvg::SvgPrivate::eraseRenderer during live artwork replacement. Apply now stages the Start artwork and stops plasmashell before installing packages, replacing SVGs/plugins or publishing color changes. Existing panel layout data is prepared before the stop and saved during the same transaction. The regression verifies the previous panel SVG is untouched when the stop begins. Two consecutive live applies of the user’s corrected palette completed, with clean service stops and no new core dumps.

Generator version 2 preserves all Close button artwork, recolors the baked-in blue Start edge using the taskbar color, and normalizes the tray’s separate cyan midtone to the selected taskbar color. Existing custom palettes regenerate on reopening Properties. Generator tests verify red Close artwork remains identical and the Start edge follows the taskbar hue.

Automatic Start menu/accent colors: generator version 3 also recolors Luna's Start menu artwork from the taskbar color, and records that color as SysMetrics Highlight with automatic black/white highlight text. Header/footer text follows the same contrast choice. Dark application mode preserves this custom selection accent. Tests cover recolored Start menu bitmaps, all Blue-variant highlight properties, and dark-mode accent preservation for dark and light taskbar colors. This update was installed without changing the user's currently selected non-custom theme.

All default themes (Custom Colors tab): select the base skin, color scheme and font size on Appearance, then open Custom Colors. The swatches start from that skin. Color edits preserve the selected skin and variant; Reset restores it. The same controls now work with all installed default .msstyles (including neutral Silver/Noir skins), and all Windows Classic schemes. Start menu/highlights follow the taskbar color and custom Close buttons stay red. Existing Custom Luna presets remain readable.

Validation: all 42 installed style variants generate and reload with the chosen highlight and visibly changed Start/taskbar/titlebar artwork. All 36 stock Classic schemes preserve independent shell palettes through .theme serialization. UI checks exercise 36 base selections, variant preservation, reset and reopening saved Classic custom colors. Dark-mode and safe Apply regressions pass.

Royale visual correction (generator 6): the previous HSV remapping expanded source highlights to white even for dark target colors, recolored icon/glyph resources, and derived dark Places text directly from a dark accent. Shading is now bounded relative to the selected midtone; menu panes retain separate tones and contrast-aware text. Foreground glyph sheets, All Programs arrows and logoff icons retain their source data, including their transparency keys. Known red Close pixels are preserved, while inactive/non-red Close backgrounds become red. Source transparency colors are honored in addition to magenta. Caption and Start text/shadows follow the chosen colors.

Visual validation uses the user's exact #464646 Royale palette. Before/after Start menu and window previews are under artifacts/custom-colors. The specific Royale regression checks highlight bounds, unchanged foreground/icon images, readable menu text, and red active/inactive Close buttons. All 42 skin-variant and 36 Classic-scheme checks, dark-mode checks, generator transparency checks and safe Apply checks pass.

Contrast and theme text (generator 7): modestly increased the bounded specular gain and deepened shadow contrast, retaining the protection against white reflections on dark skins. Custom Colors now has Theme text: Auto / White / Black, covering titlebars, Start button/menu, taskbar and selections. Auto retains per-surface contrast selection. The override is saved in generated-theme metadata and Classic .theme fields, restored on reopening and respected by dark application mode. Document/application body text remains governed by the application palette.

Validation: Auto/White/Black generation and restored UI selections; dark-mode highlight text; Classic override serialization; Royale highlight bounds/icon preservation/red Close; all 42 installed skin variants and 36 stock Classic schemes; safe Apply regression. The new control is previewed in artifacts/custom-colors/text-mode-control.png.

Per-option text controls (generator 8): each color row now has its own Auto / White / Black dropdown. Start, taskbar/Start menu/selection, titlebars, and titlebar button symbols are independent. Previous global text preferences migrate onto all four rows. Both generated-style sidecars and Classic schemes retain the four choices. Button glyph transparency keys and alpha are preserved when changing symbols; baked Luna button glyphs are also supported. Close retains its red background.

Validation includes mixed white/black combinations, independent shell colors, keyed/alpha button glyphs, Classic persistence, UI reopening, Luna baked-symbol choices, the full default-skin and Classic checks, and safe Apply regression. Preview: artifacts/custom-colors/per-option-text-controls.png.

KRunner protection (2026-10-15): theme artwork replacement also stops an active
`plasma-krunner.service` and restores it afterward. An inactive runner remains
inactive. This extends the existing Plasma renderer protection to another
consumer of the active SVG theme. The reported 19:11 KRunner buffer-overflow
abort coincided with Apply; its main-thread stack was damaged, so this is a
preventive fix rather than a confirmed diagnosis of the original overflow.

Close keeps the skin's artwork (generator 15, 2026-10-16): Custom Colors no
longer repaints Close at all -- active, inactive, hot and pressed are the
skin's own bitmaps (Luna's faded mauve on an inactive window, Royale's own).
Only its symbol follows the titlebar buttons' White/Black choice. Earlier
generators reddened the inactive frames against the active red's tone, which
came out darker and more saturated than the active button. Tests:
`closeKeepsSkinArtwork`, and Royale's Close compared against Royale's own.
