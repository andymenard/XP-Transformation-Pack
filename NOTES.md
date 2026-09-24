# Notes and follow-ups

Findings made along the way that are real but out of the current step's scope.
Each says where it came from, so it can be picked up without re-deriving it.

## Open

- **The copy dialog shows Minimize and Maximize** (2026-09-23). XP's has
  Close only; the dialog asks for that (CustomizeWindowHint, no min/max
  hints), which X11 honours but Qt's Wayland platform doesn't pass to KWin.
  A KWin window rule or a decoration hint would be needed.
- **A failed transfer leaves no balloon**, as with Plasma's own popups: a
  job terminated with an error (JobViewV3) leaves the popup model at once;
  the app shows its own error. XpPopup's error-balloon path stays for a job
  that is still listed when its error arrives.

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
- **MDI captions: the icon and (visual styles) the caption glyphs.** XP
  shows the stock application icon (a Microsoft resource, not reproduced),
  Qt its window icon; under a visual style the caption is Trebuchet MS
  bold from GDI against FreeType's. Everything else in an MDI child --
  caption art, frame, buttons in all states, text position and shadow,
  Classic's gradient to the pixel -- matches.
- **An editable combo's text isn't selected while its list is down.** XP
  selects the edit's text when the list drops (the list is a window that
  never takes focus); Qt's list popup takes the focus, and the line edit
  paints no selection meanwhile. The drop-down list itself matches XP
  exactly under every style (combo `open`).
- **A maximized MDI child's menu titles sit 4px left of XP's.** XP gives
  the child's icon a menu item SM_CXMENUSIZE + 2 wide with the icon 4px in;
  Qt's QMdi::ControlLabel is exactly its icon and QMenuBar lays it out by
  its sizeHint, which no style hook reaches. The buttons (themed WP_MDI*
  parts, or Classic's DFC ones) and the bar itself match.
- **The line under a menu bar is the bar's** (measured 2026-09-23 in an
  ordinary overlapped window): SM_CYMENU is the menu height + 1, the last
  row COLOR_MENU with flat menus, COLOR_3DFACE without. (A bare WS_POPUP
  host shows COLOR_WINDOWFRAME there instead, which is why the oracle's
  `menubar:1` uses a real frame.)
- **Classic trackbar: the point's first row, disabled or held.** XP left
  it undithered on a 22px thumb (Luna's leftover metrics) and dithered it on
  a 21px one (Classic's own); 2px, rule unknown from two samples.
- **The oracle's Classic had Luna's window metrics** (found 2026-09-23):
  EnableTheming(FALSE) leaves the visual style's sizes in place, so the
  Classic captures had 17px scroll bars and 25px captions. uxdump's
  `ncmetrics` now applies a scheme's NonclientMetrics (as Display Properties
  does), and refreshes comctl32's cached copy -- which it reads at load and
  otherwise keeps (an up-down's width, a trackbar's thumb) -- before any
  control is made.
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

- ~~**xp2plasma's parser (Pillow) mis-decodes three DIB formats**~~ (moot
  2026-09-23: the Python parser is gone). RLE4/RLE8, 16 bpp 5-5-5 and 8 bpp
  with a short palette, found by the old conformance test and settled by XP's
  GDI through the oracle; the C++ core matches GDI on all of them.
- **XP won't open unsigned (fan) styles** in the oracle: `OpenThemeFile`
  returns E_FAIL for Viatz and Professional even with default colour and size,
  consistent with uxtheme's signature check. The VM stays unpatched (the
  theme-transfer README's rule), so uxtheme-level oracle answers come from
  Microsoft's five styles; GDI-level questions (decoding, keying) work for
  any bitmap.

- ~~**Wallpaper import drops XP's tiled wallpapers**~~ (resolved 2026-09-23):
  the import reads XP's own list from its setup INFs -- SHL_IMG.INF's
  Wallpaper.CopyFiles (Web\Wallpaper) and ACCESSOR.INF's "Desktop
  Wallpaper" component -- and names the files as XP did (31 on SP3). An
  older import's 8.3 names are renamed and what XP didn't list removed
  (the desktop and saved themes follow). The Sample Pictures (Blue hills,
  Sunset, ...) went with them: XP kept those in My Pictures, not the list.
- ~~**`test_tray_click` needs KDE's QML modules**~~ (resolved 2026-09-23):
  the C++ app's QML tests (`test-startmenu`, `plasmoid_smoke.sh`) run under
  the host's Qt, where KDE's modules are.
- ~~**Generated packages still say `xp2plasma`**~~ (resolved 2026-09-23): the
  C++ app generates XPLasma's names (`fyi.hotsocket.xplasma.start`,
  `xplasma-icons`, the `X-XPLasma-*` markers), and reset still recognises
  xp2plasma's.
- ~~**`test_native_buttons`, `test_native_combos` and `test_native_startmenu`
  still target kvantum-xp.**~~ (moot 2026-09-23: the Python tests are gone;
  `tests/oracle/styles_matrix.py` is the style's parity gate.)
- **Screenshots of the live desktop failed while the user was away**
  (2026-09-23): `spectacle -b -n -f -o FILE` exits 0 and writes nothing,
  although the session isn't locked and the panel output is enabled. It
  worked at 01:44 the same day. Live-app checks (Dolphin with the XPLasma
  style) wait for the user, or for a headless nested KWin; the gallery and
  the control oracle cover the style meanwhile.
- ~~**XP-style text rendering**~~ (done 2026-09-23, `src/app/xptext.cpp`):
  apply sets the whole desktop to XP's "Standard" smoothing -- a fontconfig
  rules file (full hinting, no colour; XP's own fonts unkerned and one-bit
  where their gasp tables say), included at the end of KDE's fonts.conf so
  it's read after KDE's settings; kdeglobals' Xft keys to match; and the
  style turns kerning off in Qt apps (xplasmarc [Text] Kerning=false), which
  fontconfig can't. Reset restores the user's. Measured against XP's Display
  Properties: tab widths now land where XP's do (FreeType's unhinted 10.67 px
  Tahoma had made "Themes" 1px wider, shifting every tab after it).
- **The XP VM's image is on Luna Silver** (found 2026-09-23): comparisons
  with a live Luna Blue need the VM switched first (desk.cpl, Appearance,
  Color scheme). Its ClearType is on too, unlike XP's default.
- **Task buttons are laid out as XP's only for visual styles**: the
  hint-xplasma-taskband marker is in the Luna/style panel theme, not in
  Windows Classic's, whose task-button geometry hasn't been measured.
- ~~Classic tray has no sunken frame art~~ (done 2026-09-23): XP's static
  edge round the tray and clock, and explorer.exe's << >> on a flat button
  for hidden icons (measured on a Windows Standard capture, 800x600).
- ~~**The XP VM has ClearType on**~~ (resolved 2026-09-23): the image
  boots with ClearType; uxdump now sets XP's default "Standard" smoothing
  for the session at the start of every job (SPI_SETFONTSMOOTHINGTYPE, thrown
  away with the snapshot), so captures no longer depend on what an earlier
  run left behind. (A long-running VM session had quietly been on Standard;
  a reboot brought ClearType back and every label differed.)
- **A newly installed style plugin isn't visible to running apps**
  (2026-09-23, first live switch). Qt's plugin loader scans the plugin
  directories once per process, so after `libxplasma.so` first appears,
  KDE's StyleChanged broadcast makes running apps ask for "XPLasma", fail,
  and keep their current style (they all stayed on kvantum-xp; none
  crashed). Apps started afterwards load it. Once the plugin is in a
  process, switching .msstyles themes is live. The installer should
  therefore put the plugin in place at install time -- before the first
  Apply -- or say plainly that open apps switch after a restart.
