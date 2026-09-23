# Start menu layout prototype

This standalone preview does not replace Kickoff or launch applications,
log off, or shut down. Rows are illustrative fixtures, not live KDE models.
It now shares `start-menu/StartMenu.qml` and `startmenu_gen.py` with the live
launcher. `start-menu/XpBackend.qml` supplies KDE models/actions to the live
applet; the standalone preview remains inert.

Run from the checkout:

```sh
.venv/bin/python3 prototypes/start_menu/preview.py
```

Uses the currently applied visual style; `--style PATH --ini NAME` selects a
different theme. `--screenshot /tmp/start-menu.png` captures only the client
area. Generated original artwork and its provenance are temporary, never
shipped. No network, VM disk access, or desktop configuration writes.

## Reference measurements

- Overall: **380 × 478**, measured by the user in the XP VM.
- Luna blue footer: fixed **48px left / 1px stretched center / 48px right**.
  INI says 49/47; preserve both facts in the exported data. Do not apply this
  override to other themes.
- Programs separators: native **172 × 2**, centered in the 190px left column;
  separate pinned, recent, and All Programs areas.
- System-shortcut separators: native **134 × 2**, centered in the right column
  between groups.

## Read from the theme

Header 64px; footer 40px; left column 190px; base All Programs row 30px; the remaining
body is 374px high at the measured overall size. Source PNGs, slicing margins,
content insets, text colors, fonts, username shadow offsets and hover artwork
are read from StartPanel and SysMetrics. DefaultPaneSize is read from each
part's own section: inheriting StartPanel's 380 × 440 value for UserPicture
would be incorrect.
Flyout text uses StartMenu::Toolbar's normal/hot colors independently of the
main StartPanel text: Zune and Royale Noir have dark panels but white flyouts.

## Provisional / still to compare

Latest visual correction: All Programs gets 14px more height (44px total), with
the footer and overall size unchanged. Its separator has a 2px top gap, with
6px extra space below the label. The highlight keeps the theme's bottom inset.
The program pane fits eight 38px rows, including the pinned/recent separator.

Pinned and recent rows 38px, right rows 30px; separator gaps 8px; picture
position (7,4); name-to-picture gap 8px. These are explicit `provisional` values
in the generated JSON, not claimed as measurements. Main-row hover fill uses
SysMetrics.Highlight pending comparison. Standard icon sample choices and
sample row wording are not a final Linux-to-XP semantic mapping.

Live integration: the launcher's full Plasma favorites list, in its original
order, followed by at most `max(0, 8 - pinned)` recent applications excluding
those favorites. The left pane scrolls when needed without changing the menu
dimensions. Dynamic XP-style menu growth is explicitly deferred. There is no separate
pin store or fallback pins for an empty favorites list. Themed right-side shortcuts,
KRunner for Run/Search, confirmation-required session actions, and an All
Programs submenu. All Programs opens after 400ms of hover or immediately on
click, remains highlighted while open, and uses KDE category folders with
cascading, scrollable flyouts. Classic styles retain stock Kickoff.
Program-folder icons use the dedicated `folder-programs` mapping from
`shell32.dll` group 37; ordinary folders remain group 4. Clicking All Programs
again, hovering another main-menu entry, or backing out with Escape closes the
flyout without closing Start. Focus returning to an ancestor closes only that
branch; clicking outside the menu family dismisses the launcher.
Recent Files uses `shell32.dll` group 21 and anchors its flyout to its own row.
Both root flyouts close after the pointer leaves their button and all descendant
menus for 400ms, clearing mouse-driven focus; keyboard-only opening stays open.
