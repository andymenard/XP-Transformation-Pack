# Notes and follow-ups

Findings made along the way that are real but out of the current step's scope.
Each says where it came from, so it can be picked up without re-deriving it.

## Open

- **Inactive selections follow SHOWSELALWAYS, deliberately.** Plain XP
  list and tree views hide the selection while unfocused; nearly every real
  XP app (Explorer, regedit, MMC) sets LVS_/TVS_SHOWSELALWAYS, which shows
  it in the button face. XPLasma draws the latter (controls-2.job holds
  listview:3 / treeview:6; listview:2 / treeview:5 unfocused would differ).
- **Report views highlight the label only**, as XP does without
  LVS_EX_FULLROWSELECT -- a candidate for a per-app/context option later.

- **±1 blend rounding on Luna Blue/Olive** (styles_matrix, 2026-09-23):
  a push button's and a tab's antialiased edge pixels come out 1 unit off
  over the button face. GDI's AlphaBlend rounds dst*(255-a)/255 exactly;
  Qt's raster engine uses the (t + (t>>8) + 0x80) >> 8 approximation. Only
  pre-compositing over a known background would fix it, and a style
  doesn't know what's behind a widget. Luna Silver, Royale, Noir, Zune and
  Embedded happen to hit no such values.

- **Tab rows that overflow scroll differently.** XP scrolls a tab row by
  whole tabs (the first visible tab flush at x=2); QTabBar scrolls by
  pixels, just far enough to show the current tab (makeVisible), with no
  style hook. The up-down, clipping and art match; the offset doesn't
  (controls-2.job: tabs:2 181x60, tabs:1 150x60).

- **Drop shadows** (2026-09-23): XP gives menus and tooltips a 4px
  CS_DROPSHADOW; the oracle captures now crop to the window rect. Under
  KWin a shadow would need the window-shadow protocol (what Breeze uses
  via KWindowShadow) or a translucent margin drawn by the style.
- **QStatusBar::showMessage text sits at x=6** (XP: 2). The offset is
  hard-coded in QStatusBar's paintEvent with no style hook; labels added
  with addWidget follow the layout instead.
- **Qt 5 gallery tooltip**: QToolTip under Qt 5 offscreen never creates
  the label, so control_tooltip has no Qt 5 twin (the style code is shared).
- **Qt 5 spin boxes select their text on each step.** XP's up-down leaves
  the buddy's caret alone; Qt 6.6+ asks the style (SH_SpinBox_SelectOnStep,
  answered no), Qt 5 selects unconditionally.
- **Focus cues in the oracle follow the last input** (found 2026-09-23):
  XP shows or hides a new window's focus rectangles by whether the last
  input was keyboard or mouse, so one mouse click flipped every later
  `focused` capture. uxdump now pins the state (WM_CHANGEUISTATE): cues
  shown for `focused`, hidden otherwise -- the gallery mirrors it with
  WA_KeyboardFocusChange only for `focused`.

- ~~XP's report list view header 1px further in~~ (resolved 2026-09-23):
  the list view's client edge is border + a 1px ring of fill, so the
  frame is 2px and the rows' text insets were re-derived from that; the
  area past the last column is the header class's own background.
- **Tile-mode edges: tile or stretch?** (renderer, 2026-09-23). Tiling the
  edges along their length makes 1014/1023 Royale renders exact, including
  every progress bar and spin button, but Royale's `Window.MaxCaption`
  (SizingType Tile, a 3-row middle band) is *stretched* along its left edge
  by XP (each row repeated 14-15 times at 69 px high). Stretching all edges
  instead drops the total to 987. Something separates the two -- the window
  class, the band length, a property -- and more oracle data (Luna, Zune,
  Embedded, Royale Noir) should show what. Low stakes: window-frame parts
  are Aurorae's, not the Qt style's.

- **xp2plasma's parser (Pillow) mis-decodes three DIB formats** (found by the
  conformance test, settled by XP's GDI through the oracle, 2026-09-23): RLE4/
  RLE8 (undecodable; 35 corpus bitmaps, used by 10 fan styles' grippers and
  Luna Beta's scroll shaft), 16 bpp 5-5-5 (channel expansion off), and 8 bpp
  with a short black/white palette (drawn black instead of white). The C++
  core matches GDI on all of them. The generators don't draw these parts
  today, so it's low impact, but per PLAN D4 `msstyles_parse._dib_to_image`
  should decode them the same way (RLE by hand, 5-5-5 by bit replication,
  palettes by index).
- **XP won't open unsigned (fan) styles** in the oracle: `OpenThemeFile`
  returns E_FAIL for Viatz and Professional even with default colour and size,
  consistent with uxtheme's signature check. The VM stays unpatched (the
  theme-transfer README's rule), so uxtheme-level oracle answers come from
  Microsoft's five styles; GDI-level questions (decoding, keying) work for
  any bitmap.

- **Wallpaper import drops XP's tiled wallpapers** (found 2026-09-23, carrying
  over tests). `setup_assets._prune_non_wallpapers` filters by *size*, so the
  small tile bitmaps XP listed in Background (Blue Lace 16, Coffee Bean, Soap
  Bubbles, Zapotec, Santa Fe Stucco, …) are removed, while full-size
  non-wallpapers (`setup_w.bmp`, `table.bmp`, `wpaback.jpg`, `newmark*.jpg`) are
  kept. XP's own list is the files in `%windir%` and `%windir%\Web\Wallpaper`
  (see `I386/LAYOUT.INF` / `HIVEDEF.INF`), which would be an exact allowlist.
- **`test_tray_click` needs KDE's QML modules** (`org.kde.ksvg`,
  `org.kde.kirigami`, …), which a pip PySide6 venv doesn't have. It failed the
  same way in xp2plasma. It should run under the host's Qt instead (the same
  question spike 0.3 answers for the style plugin).
- **Generated packages still say `xp2plasma`**: the `org.xp2plasma.start` applet,
  `xp2plasma-icons`, and the `generated by xp2plasma` markers `revert` uses to
  recognise its own work. They are live on this desktop, so they are renamed
  in Phase 6 as a migration (revert has to recognise both names), not by
  search and replace.
- **`test_native_buttons`, `test_native_combos` and `test_native_startmenu`
  still target kvantum-xp.** They are the first parity gates for the XPLasma
  style and are re-pointed at it in Phase 3; until then they are not run.
- **Screenshots of the live desktop failed while the user was away**
  (2026-09-23): `spectacle -b -n -f -o FILE` exits 0 and writes nothing,
  although the session isn't locked and the panel output is enabled. It
  worked at 01:44 the same day. Live-app checks (Dolphin with the XPLasma
  style) wait for the user, or for a headless nested KWin; the gallery and
  the control oracle cover the style meanwhile.
- **XP-style text rendering, measured to pixel-exact** (2026-09-23, XP VM
  switched to Standard smoothing by the user): full hinting + no
  antialiasing + **no kerning** (GDI's ExtTextOut never kerned) makes push
  buttons, radios, check boxes and edits pixel-exact against XP, labels
  included. Design: an opt-in "XP text" setting. Hinting and AA go through
  fontconfig (fonts_gen, the "exclude range from anti-aliasing" rule);
  kerning can't, so the style turns it off in polish(QApplication*) when
  xplasmarc says so.
- **XP-style text rendering: a session font setting worth adding**
  (measured 2026-09-23 with the control oracle). Rendering Tahoma 8 with
  full hinting and no antialiasing, as XP's "Standard" smoothing did,
  brings the 20 compared controls from 6333 to 4691 differing pixels; the
  push-button label's box becomes identical to XP's and check-box labels
  match in size (87x11). It isn't the style's job: `fonts_gen` should
  offer it on Apply, through the same fontconfig rule KDE's Fonts module
  writes for "exclude range from anti-aliasing" (0-8 pt).
- **The XP VM has ClearType on** (seen 2026-09-23: coloured fringes such as
  229,241,237 around control labels). XP's default was "Standard"
  smoothing, which the XPLasma look aims at, so exact text colours can't
  be read off the VM as it stands. Switching it to Standard (Display
  Properties > Appearance > Effects) is a persistent VM change, so it's
  the user's call (`XPVM_PERSIST=1 refharness/vm.sh`).
- **A newly installed style plugin isn't visible to running apps**
  (2026-09-23, first live switch). Qt's plugin loader scans the plugin
  directories once per process, so after `libxplasma.so` first appears,
  KDE's StyleChanged broadcast makes running apps ask for "XPLasma", fail,
  and keep their current style (they all stayed on kvantum-xp; none
  crashed). Apps started afterwards load it. Once the plugin is in a
  process, switching .msstyles themes is live. The installer should
  therefore put the plugin in place at install time -- before the first
  Apply -- or say plainly that open apps switch after a restart.
