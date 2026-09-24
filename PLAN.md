# XPLasma — game plan

*Draft 1 · 2026-09-23 · sequel to xp2plasma*

> **Since this draft:** the Python app carried over from xp2plasma (§3, §6) has
> been rewritten in C++/Qt 6 on the core and deleted; see §12. Where this plan
> names Python modules (`msstyles_parse`, `widget_style.py`, `app/…`), read
> them as history. What they learned is in Appendix A and in `src/app/`.

XPLasma keeps everything that already works in xp2plasma (the Display
Properties editor, window decorations, Start menu, taskbar, tray, icons, fonts,
wallpapers) and replaces the one piece that has been fighting us: the Kvantum
widget style. In its place goes **a native Qt style that reads the `.msstyles`
itself and draws with uxtheme's own rules**. It is built for Qt 5 and Qt 6 from
one source tree, and it is designed from the start to be *context-aware*, *live*
and *graceful*. GTK is not in this round, but the structure leaves a slot for it.

---

## 1. What's decided

| Question | Answer |
| --- | --- |
| What renders straight from `.msstyles`? | The Qt widget style. Decorations, Start menu, taskbar, tray and so on are lifted from xp2plasma unchanged. |
| "Context awareness" means | **Widget context** (a button in a toolbar is not a dialog button), **live session awareness** (theme changes reach running apps), **graceful degradation** (missing art never draws wrong). *Not* per-application rules. |
| Toolkits | Qt 6 and Qt 5 now. The shape leaves room for GTK later. |
| Codebase | New repo in `XPLasma/`, carrying xp2plasma's code over. **The installer is left behind.** Its flow was good, but it had flaws of its own; it gets redesigned later, separately. |

---

## 2. Why Kvantum has to go (the evidence)

xp2plasma's widget style works, but most of its cost is spent *translating
uxtheme into Kvantum's model and then patching Kvantum back toward uxtheme*:

- **`kvantum-xp/patch.py` carries 26 source edits** to upstream Kvantum. Ten
  only rename it so it can sit beside a real Kvantum. The other **16 drag its
  rendering back toward uxtheme**: SIZINGMARGINS treated as layout padding, a
  12px floor on tab padding, DEFAULTED treated as an overlay, a focused combo
  drawn as pressed, the progress label drawn over the bar. Each one fights
  Kvantum's model, and each one breaks when upstream releases.
- **`kvantum_gen.py` loses information** on the way in. It has no
  `[Globals]`, no `Class::Subclass` fallback, no `Class(State)` sections, no
  DPI image sets (ImageFile1..5), no TrueSize parts and no font glyphs.
  Kvantum *rotates horizontal scroll art* where XP ships separate vertical art
  (`ThumbBtnVert`, `GripperVert`, `LowerTrackVert`).
- **Deployment is per-host.** A big C++ codebase has to be compiled against the
  host's exact Qt, in podman, with a KF6WindowSystem dependency. That
  requirement is exactly the thing the "stranger can install it" goal forbids.
- **The editor can't load it.** That is why `xp_style.py` (a partial SVG
  approximation) and `system_qt.py` (grafting Arch's PySide6 onto the system
  Python) exist at all.
- A generated SVG plus kvconfig per theme sits between the style file and the
  pixels. The live apps can't see the `.msstyles` at all.

A native style removes all five problems at their root.

---

## 3. Target architecture

```
                       ┌──────────────────────── XPLasma (Python, carried over) ───────────────────────┐
                       │ appearance_editor · theme_index · setup_assets · aurorae_gen · plasmatheme_gen │
                       │ panel_* · start_applet · xp_icons · fonts_gen · colorscheme_gen · classic_gen  │
                       │                                                                                │
                       │ widget_style.py (rewritten) ── writes ~/.config/xplasmarc, installs plugins,   │
                       │                                 announces changes                              │
                       │ targets/  qt ✔   gtk3 ◌   gtk4 ◌      ← toolkit-target registry (GTK later)    │
                       └────────────────────────────────────────────────────────────────────────────────┘
                                        │ config + D-Bus signal            │ subprocess (tests, previews)
                                        ▼                                  ▼
 ┌──────────────── native/ (C++17, one source tree, built per Qt major) ─────────────────────────────┐
 │                                                                                                   │
 │  core/        PE resource reader · INI → typed property model · full uxtheme cascade ·            │
 │               variant tables · DIB decoder · colour-key/alpha · theme cache                       │
 │  render/      DrawThemeBackground / DrawThemeText / GetThemePartSize / content-rect semantics     │
 │               on QPainter, pixmap cache, DPI image selection                                      │
 │  style/       XPlasmaStyle : QProxyStyle(base = Qt's "Windows")                                   │
 │               ├─ mapping   Qt element + option  → class.part(state)                               │
 │               ├─ context   widget/ancestor/QML item → subclass ("Toolbar", "PlacesBar::Toolbar")  │
 │               ├─ metrics   pixelMetric / sizeFromContents from the theme's own sizes              │
 │               └─ live      config watch + D-Bus → reload + repolish running apps                  │
 │  tools/       xplasma-render (part → PNG/JSON, export manifest)                                   │
 │               xplasma-gallery (every control × state)                                             │
 │                                                                                                   │
 │  → libxplasma-qt6.so  (built against the OLDEST supported Qt 6)                                    │
 │  → libxplasma-qt5.so  (built against Qt 5.15)                                                      │
 └───────────────────────────────────────────────────────────────────────────────────────────────────┘
                                        ▲
                                        │ pixel oracle
                       ┌──── refharness/ (the XP VM) ────┐
                       │ uxdump.exe: real uxtheme draws  │
                       │ every class/part/state → BMPs   │
                       │ refdialog.exe: control gallery  │
                       └─────────────────────────────────┘
```

### Key design decisions

**D1. Public Qt API only → one prebuilt binary per Qt major.**
Checked today: the kvantum-xp plugin already needs only the public `Qt_6`
symbol version plus `qt_version_tag Qt_6.9`. The breakage came from the build
floor and from KF6WindowSystem, not from Qt's ABI. A style that uses only public
Qt API, built against Qt 6.*N*, loads into *every* Qt ≥ 6.*N*. Plasma 6 has
never shipped on less than Qt 6.6 (to verify in Phase 0), so **one
`libxplasma-qt6.so` built against Qt 6.6 in an old container covers every
Plasma 6 system**, and one Qt 5.15 build covers Qt 5. That means no toolchain
on the user's machine, no podman and no rebuild after a system update. It is
very likely to load inside a pip-installed PySide6 too (6.11 ≥ 6.6, and the
symbols it needs are public), which would retire `xp_style.py` and possibly
`system_qt.py`. Spike 0.3 settles that.

**D2. `QProxyStyle` over Qt's built-in "Windows" style.**
This copies XP's own architecture. On XP, comctl32 asks uxtheme to draw a
control, and whatever the theme doesn't cover is drawn *classic*. Our proxy
draws what the theme covers and lets the classic Windows style draw the rest,
in the theme's own SysMetrics colours. The base style's internal calls go
through `proxy()`, so even the sub-elements it draws come back through us.

**D3. The plugin reads the `.msstyles` at runtime.**
There are no generated intermediates. On Apply, the app copies the chosen
style into a content-addressed store (`~/.local/share/xplasma/active/<sha256>.msstyles`)
and points the config at that copy. Running apps never see a file change
underneath them, which is the same lesson as the kvantum-xp SIGBUS: always
atomic rename, never an in-place write. The file is read into memory and never
mmap'd.

**D4. Two parsers, one truth.**
The proven Python parser stays for the generators (Aurorae, panel, Start menu).
The C++ core is new. They are kept in lockstep by a **conformance test over
the full 126-style corpus**: both dump a canonical JSON of resolved properties
and bitmap hashes, and the dumps must match. Fixes to the cascade (below) land
in both.

**D5. Faithful uxtheme cascade**, including the parts the Python parser skips.
Lookup order for (subclass, class, part, state, property), as Wine's uxtheme
implements XP's: **(part, state)**, then **(part, 0)**, then **(0, 0)**, each
tried along the override chain `Sub::Class` → `Class` → `[Globals]` before
moving to the next. A class-level `[Class(State)]` section is the (0, state)
step, so it applies to the class's part 0 only. *(Corrected 2026-09-23;
the first draft had a different order from memory. The oracle arbitrates any
remaining doubt.)*
Today's parser files `[ComboBox(Disabled)]` and `[Toolbar(Pressed)]` as
*classes* named `combobox(disabled)` and `toolbar(pressed)`. Luna has several
such sections, and fixing that is part of this work.

**D6. Safety is a feature.**
plasmashell and every KDE dialog will load this plugin, so a crash in it takes
the whole session down. That drives these rules:
- Every parser read is bounds-checked, and the core is fuzzed with libFuzzer
  (clang is available) against malformed PE, INI and DIB input.
- Symbols are hidden (the kvantum-xp lesson).
- `XPLASMA_SAFE=1` degrades to the plain Windows style.
- A theme that fails to load logs once and falls back to classic. It never
  aborts.

---

## 4. The Qt style

### 4.1 Mapping: Qt element → msstyles part

State numbers follow uxtheme. Frame index in the image strip = state − 1, or
frame 0 when the strip is shorter.

| Qt | msstyles class.part | States |
| --- | --- | --- |
| QPushButton bevel | `Button.PushButton` | normal/hot/pressed/disabled/**defaulted** |
| QCheckBox indicator | `Button.CheckBox` | 12: {un,}checked/mixed × 4 |
| QRadioButton indicator | `Button.RadioButton` | 8 |
| QGroupBox frame + title | `Button.GroupBox` (+ its TextColor) | — |
| Tool button (auto-raise / in toolbar) | `Toolbar.Button`, `SplitButton`, `SplitButtonDropDown`, `DropDownButton` | 6 incl. checked/hot-checked |
| QToolBar area / handle / extension | `Rebar` (background), `Rebar.Band`, `Gripper[Vert]`, `Chevron[Vert]` | — |
| Toolbar separator | `Toolbar.Separator[Vert]` | — |
| QScrollBar | `Scrollbar.ArrowBtn` (16), `ThumbBtnHorz/Vert`, `GripperHorz/Vert`, `Lower/UpperTrackHorz/Vert`, `SizeBox` | 4 each |
| QTabBar / QTabWidget | `Tab.TabItem{,LeftEdge,RightEdge,BothEdge}`, `TopTabItem*` for the selected tab, `Tab.Pane` | 5 |
| Tab *page* background in dialogs | `Tab.Body` (XP's dialog texture) | — |
| QProgressBar | `Progress.Bar[Vert]`, `Chunk[Vert]`, ProgressChunkSize/SpaceSize | — |
| QSlider | `Trackbar.Track[Vert]`, `Thumb{,Bottom,Top,Left,Right,Vert}` chosen by tick position, `Tics[Vert]` | 5 |
| QSpinBox buttons | `Spin.Up/Down` (`UpHorz/DownHorz`) | 4 |
| QComboBox | `ComboBox.DropDownButton` + `ComboBox`/`Edit` BorderFill frame | 4 |
| QLineEdit / text frames | `Edit.EditText` (BorderFill) | normal/hot/selected/disabled/focused/readonly |
| QHeaderView section | `Header.HeaderItem` | 3–4 |
| Tree branch expander | `TreeView.Glyph` | closed/opened |
| QStatusBar / QSizeGrip | `Status.Pane`, `GripperPane`, `Gripper` | — |
| QMdiSubWindow | `Window.SmallCaption`/`SmallFrame*`, `MdiCloseButton`… | active/inactive |
| Dock widget title | `Window.SmallCaption`, `SmallCloseButton` | — |
| QMenu / QMenuBar / QToolTip | *unthemed on XP*: SysMetrics colours, FlatMenus, MenuHilight, InfoBk | — |

Metrics come from the theme rather than from guesses: `GetThemePartSize`
(TS_TRUE, TS_DRAW, TS_MIN), ContentMargins for layout padding, SizingMargins
**only** for 9-slicing, and SysMetrics for scroll bar width, caption height,
fonts and colours. Every kvantum-xp patch becomes a normal code path here
instead of a patch (see Appendix A).

### 4.2 Widget context

A **context resolver** turns (element, option, widget or QML item) into the
*subclass* that uxtheme's `SetWindowTheme` would have applied. Theme lookup
then does the `Sub::Class` → `Class` fallback from D5. Rules, first match wins:

1. **Explicit property.** `xplasma-class` on the widget or an ancestor. Our own
   surfaces set it, and apps can too.
2. **Structural rules.** These look at the ancestor chain and the widget role:
   - a tool button inside a QToolBar → `Toolbar`; in a status bar → `Status`
   - a file dialog's places sidebar (QFileDialog / KFileWidget) → `PlacesBar::Toolbar`, which Luna actually ships
   - pages of a QTabWidget inside a dialog → paint `Tab.Body`
   - a QMdiSubWindow or floating dock → `Window` small-caption parts
   - a scroll bar inside a combo popup → same as elsewhere (no special case, as in XP)
3. **Option-type defaults** from the table above.

Resolution is cached per widget and invalidated on `ParentChange`. When
`widget == nullptr` (QtQuick controls through qqc2-desktop-style, i.e. System
Settings, Discover and Plasma dialogs), rules read the `styleObject` and its
item hierarchy instead, and fall back to option-type defaults. No rule may
assume a QWidget exists.

A per-widget theme override (`xplasma-theme = path|INI`) exists for previews,
so an app can show a *non-applied* theme in a sub-tree. The environment
variable `XPLASMA_THEME=path:INI` does the same per process, for tests and
tools.

### 4.3 Live session

- **Config.** `~/.config/xplasmarc`: `[Theme] path=… variant=NORMALBLUE_INI`.
- **Change signal.** The app writes the config atomically, then emits a D-Bus
  signal (`org.xplasma.Style.Changed`). Each process also has a
  `QFileSystemWatcher` backup, for apps without a session bus.
- **On change.** Load the new theme off-thread, swap it in atomically, clear
  the pixmap cache, then unpolish, polish and update every top-level. A
  `StyleChange` event is sent so layouts pick up new metrics (Large and Extra
  Large font sizes are separate INIs, so metrics really do change).
- **Switching to or from another style** (Breeze and so on) is left to KDE's
  own `widgetStyle` + `KGlobalSettings` path. Phase 0 checks whether
  plasma-integration re-creates the style when the name doesn't change. If it
  doesn't, our own signal covers that case.
- **DPI.** Changes to `devicePixelRatio` pick from ImageFile1..5 by MinDpi*n*,
  as XP did at 120 and 144 DPI, and only then scale.
- **XP behaviour.** Focus rectangles and mnemonic underlines stay hidden until
  the keyboard is used (`State_KeyboardFocusChange`, `SH_UnderlineShortcut`).
  Hover tracking is on. There are no state-transition animations, because XP
  had none.

### 4.4 Graceful degradation — an explicit ladder

| Step | What draws | Example |
| --- | --- | --- |
| L0 | Exact art for `Sub::Class.Part(State)` | Luna push button |
| L1 | The same part without the subclass | `Media::Toolbar` → `Toolbar` |
| L2 | A sibling part the theme *does* have | no `TopTabItemLeftEdge` → `TopTabItem`; no `ChunkVert` → `Chunk` rotated |
| L3 | Synthesised from the part's own declared colours (BorderFill / Fill / TextColor) | fan style with only `Edit` colours |
| L4 | Classic Win32 drawing (the proxied Windows style) in the theme's SysMetrics colours | a style with no `Trackbar` class at all |

The ladder never stops at "wrong-looking art". Each theme gets a **coverage
report** listing which parts landed on which rung. It comes from
`xplasma-render report` and is logged at Apply, the way `kvantum_gen`'s
`report` is today.

---

## 5. GTK later: the machinery now

GTK has no style-plugin model, so GTK will always be *generated*. Two things
land in this round so that generating it later is cheap:

1. **`xplasma-render export`.** It emits the *resolved* theme as a manifest.
   For every class, part and state it gives a PNG (DPI variants included), the
   sizing margins, content margins, text colour and shadow, and fonts, plus
   SysMetrics. msstyles 9-slices map directly onto GTK3 CSS `border-image`, so
   a GTK generator only consumes this and never re-derives uxtheme.
2. **A toolkit-target registry** in the Python app. `targets/qt` is real;
   `targets/gtk3` and `targets/gtk4` are stubs with the same generate, apply
   and revert interface, and they report "not yet". Apply and reset walk the
   registry, so turning GTK on later means filling in a stub. The pipeline
   itself doesn't change.

What's already known about that future work: GTK4/libadwaita apps largely
ignore themes, and Flatpak GTK apps need the theme as an extension or an
`~/.themes` override. Both are recorded here so nobody is surprised later.

---

## 6. Carry-over manifest

Files are copied from xp2plasma's **working tree**. Its git repo has no
commits and 38 untracked files and directories (hostexec, paths, setup_assets
and most tests among them), so git alone won't capture it. xp2plasma itself stays
untouched as the reference.

| Fate | Modules |
| --- | --- |
| **Carry unchanged** | `msstyles_parse` (plus the D5 cascade fix), `aurorae_gen`, `plasmatheme_gen`, `classic_gen`, `colorscheme_gen`, `fonts_gen`, `panel_layout`, `panel_widgets`, `start_applet`, `startmenu_gen`, `start-menu/*.qml`, `title_shadow`, `xp_icons`, `xp_assets`, `theme_file`, `theme_index`, `theme_presets`, `applied_state`, `preview_gen`, `hostexec`, `bundled_tools` + `bundled-tools/`, `schemes/`, `prototypes/start_menu/` |
| **Carry, adjusted** | `appearance_editor` (UI identical; only widget-style calls change), `setup_assets` (drop the Kvantum build step), `setup_ui` (drop the `xp_style` fallback), `paths` (new namespace, and it still *reads* xp2plasma's asset library as a legacy root, so nothing is downloaded twice), `revert` (undo the new style), `xp2plasma.py` → `xplasma.py` (no install/uninstall subcommands) |
| **Rewritten** | `widget_style.py`: install the prebuilt `.so` per Qt major (atomic rename), put the qt5 and qt6 plugin dirs on `QT_PLUGIN_PATH`, write `xplasmarc`, announce the change |
| **Retired** | `kvantum_gen.py`, `kvantum-xp/`, `xp_style.py`, and `system_qt.py` if spike 0.3 passes |
| **Left behind (installer)** | `install.py`, `wizard.py`, `appimage/`, `parked/flatpak/` |
| **Not code** | `build/`, `build-dir/`, `.flatpak-builder/`, `cache/`, `backup/`, `iso/`, `samples/`, `parked/ultimate/`. Microsoft's assets never enter the repo; tests find the corpus through a configured path. |

Tests carry over too, except the Kvantum-specific ones. `test_native_buttons`
and `test_native_combos` are re-pointed at `xplasma` and become the first
parity gates.

---

## 7. Verification: the XP VM as oracle

The reference VM becomes a **pixel oracle**. Screenshots are only the
secondary check.

- **`uxdump.exe`** is a small Win32 program cross-compiled with mingw-w64 in
  the dev container. It opens a `.msstyles` and variant and calls the *real*
  `DrawThemeBackground`, `DrawThemeText` and `GetThemePartSize` for every
  class, part and state at a set of sizes, writing BMPs and a JSON of metrics.
  The preferred route uses the file-level uxtheme entry points that XP's own
  Display Properties uses to preview a style that isn't applied. The fallback
  applies each theme in the VM, then dumps. *(Spike 0.4.)*
- **`refdialog.exe`** is a Win32 dialog with the same control layout as
  `xplasma-gallery`, for whole-control comparisons including layout.
- **Transport.** QMP socket (`xp.sh … -qmp unix:…`, which already passes `"$@"`
  through) for `screendump` and `sendkey`. A FAT drive or the user-net host
  address (10.0.2.2) moves files in and out. XP never needs a network beyond
  that.
- **Gates.** Bitmap parts must be **pixel-exact** against uxdump. Text
  regions are held to a *measured margin* rather than masked: FreeType will
  never equal GDI glyph for glyph, but the text box, baseline and weight
  should land within a pixel, and the diff is reported so it trends toward
  XP. Controls are checked for pixel-exact art plus geometry within ±0 px.
- **Other suites.** Parser conformance (Python ↔ C++, 126 styles), fuzzing, the
  carried Python tests, and offscreen `xplasma-gallery` snapshots as
  regression goldens.

---

## 8. Phases

Each phase ends in something that runs. Sizes are relative.

**Phase 0 — Groundwork and spikes (S).** Settles the risky assumptions before
anything is built on them.
- 0.1 Repo, carry-over import (one verbatim commit, then a rename commit), the
  carried tests green.
- 0.2 Dev container: a SteamOS-matched Arch image with gcc, cmake, qt5-base,
  qt6-base and mingw-w64. Release containers use the oldest Qt 6 needed
  (per D1) and Qt 5.15.
- 0.3 **Spike: hello-style.** A `QProxyStyle` plugin that tints push buttons,
  built both ways. It must load in Dolphin and Kate (Qt 6), a Qt 5 app, System
  Settings QML (qqc2), and **the PySide6 6.11 venv**, and the Qt 6.6 build must
  load on the host's 6.9.
- 0.4 **Spike: oracle.** uxdump.exe renders one Luna part inside the VM, and the
  BMP reaches the host.
- 0.5 Check: does KDE re-create a style when `widgetStyle` is unchanged?
  (This decides §4.3.)

**Phase 1 — Core (M).** PE reader, typed property model, full cascade (D5),
variant tables, DIB (1/4/8/16/24/32 bpp, both orientations), colour key and
alpha. *Exit:* conformance matches on 126/126 (or documented divergences where
Python was wrong, fixed there too); the fuzzer runs for an hour clean.

**Phase 2 — Renderer (M).** BgType ImageFile/BorderFill/None;
SizingType Stretch/Tile/TrueSize; ImageSelectType; glyphs (image and font);
MirrorImage; BorderOnly; Uniform/IntegralSizing; alignment; text colour and
shadow; part sizes; content rects. `xplasma-render` CLI. *Exit:* pixel-exact
against uxdump for Luna, Royale, Royale Noir, Zune and Embedded, every part
and state.

**Phase 3 — The style (L).** Every row of §4.1, the metrics, and XP
behaviours. Qt 5 and Qt 6. `xplasma-gallery`. *Exit:* the gallery matches
refdialog on the five official styles; the carried native tests pass; a day of
use on this Deck with no regressions compared with kvantum-xp.

**Phase 3b — Classic (M).** Windows Classic drawn by the plugin from the
scheme: DrawEdge bevels in all four 3D colours, scroll and caption metrics from
NONCLIENTMETRICS, flat or 3D menus per the scheme. It is also the L4 fallback
for visual styles. *Exit:* the gallery matches refdialog with visual styles
off, under Windows Standard, Brick, High Contrast Black and one large variant.

**Phase 4 — Context (M).** Resolver, rule table, QML path, override property
and variable. *Exit:* toolbar, places bar, dialog tab body and MDI cases
verified in real KDE apps; qqc2 controls correct in System Settings.

**Phase 5 — Live (S–M).** Config, D-Bus, reload, repolish, DPI. *Exit:*
switching Luna Blue → Royale Noir → Luna Large Fonts in the editor repaints an
already-open Dolphin, Kate and System Settings with correct metrics, and
nothing restarts.

**Phase 6 — App integration (M).** New `widget_style.py`, retirements,
revert/reset, and migration on this machine (`kvantum-xp` → `xplasma`,
reversibly). *Exit:* the editor applies all five official themes end to end;
`reset` returns cleanly to Breeze.

**Phase 7 — GTK machinery (S).** Export manifest, target registry, stubs.
*Exit:* `xplasma-render export` output validated; the gtk targets report "not
yet" through the normal Apply path.

**Later, not this round:** installer redesign (§10); a Flatpak
`org.kde.KStyle.XPlasma` extension so Flatpak Qt apps get the style (ghostwriter,
qBittorrent, Prism Launcher and Elisa here are all Flatpak Qt apps and would
stay unthemed without it); a real GTK3 generator; optionally a KDecoration
plugin on the same core; real rendering in the editor's previews via
`xplasma-render`; Windows Classic through the same plugin.

---

## 9. Risks

| Risk | Mitigation |
| --- | --- |
| Plugin crash takes down plasmashell or KWin | D6: bounds checks, fuzzing, sanitizers in the dev build, safe mode, atomic install |
| D1 fails (old-Qt build won't load on newer, or not in PySide6) | Spike 0.3 before any real code. Fallback: per-host build as today, but a *small* codebase with no KF deps |
| No widget in QML controls breaks context | Rules must work from `styleObject`; System Settings is a Phase 4 gate |
| Undocumented uxtheme corners | The VM oracle decides; no guessing |
| Text rendering mismatches | Masked in pixel gates; fonts come from XP media (already imported) |
| Qt 5 / Qt 6 API drift | One source tree, thin `#if QT_VERSION` shims, both builds in every CI run |
| Performance in heavy apps | Pixmap cache keyed on (theme, part, state, size, dpr); benchmark Dolphin scrolling |
| Two parsers diverge | Conformance suite is a required gate |

---

## 10. Decisions (answered 2026-09-23)

1. **Names — yes.** Style key `XPlasma`, config `~/.config/xplasmarc`, data
   under `~/.local/share/xplasma`, reading xp2plasma's asset library as a legacy
   root.
2. **Windows Classic comes into the plugin.** It won't borrow Qt's "Windows"
   style. Our own classic renderer honours the whole scheme: all 29
   colours (3D light, highlight, dark shadow, gradient captions …) and the
   NONCLIENTMETRICS sizes, which Qt's Windows style mostly ignores. That also
   makes L4 of the degradation ladder *our* code, so the proxy base in D2
   becomes a safety net, not the classic look. It's a new phase, 3b, after the
   visual-style mapping.
3. **The installer gets rethought around the portable plugin.** Once the style
   is one prebuilt binary per Qt major and theming needs no build step, most of
   the old installer's build and host-shell machinery (podman, venv surgery,
   system-Qt grafting) has no reason to exist. Its **look** changes too: a
   knock-off of **Windows XP Setup** (the blue graphical setup with the step
   list down the left, "Setup will complete in approximately: 39 minutes").
   It's a later phase; the design waits for Phase 6.
4. **kvantum-xp** is removed at Phase 6 parity, reversibly (backed up, not
   deleted outright).
5. **Text.** It will never be identical, but it should be close. The gates
   allow a margin on text regions instead of masking them, and the renderer
   chases XP's text behaviour: Tahoma 8 pt, XP's "Standard" smoothing
   (no antialiasing at UI sizes), full hinting and GDI-like baselines, with
   each difference measured against the VM rather than guessed. The margin is
   tuned per region once the first numbers are in.

---

## Appendix A — Hard-won facts from xp2plasma that must survive

These were measured the hard way, and in xp2plasma they live as Kvantum patches
or generator comments. In XPLasma each becomes plain behaviour with a test.

- **Push buttons.** SIZINGMARGINS are *visual slices only*; layout padding is
  CONTENTMARGINS (Luna: 8,8,9,9 vs 3,3,3,3). DEFAULTED (frame 5) is a complete
  bitmap for the normal state only; hot, pressed and disabled keep their own
  art. Luna's 73×21 visible button has a transparent rim, so the widget rect
  is 75×23.
- **Tabs.** About 5 px of padding a side (Kvantum's 12 px floor made every tab
  8 px too wide). The selected tab uses TopTabItem. The last art row is pane
  colour where the tab merges, so it needs the corner junctions or you get a
  1 px notch.
- **Combo box.** A focused non-editable combo fills its field with Highlight.
  `State_Selected` is *not* pressed. Editable and non-editable combos use the
  same XP button.
- **Progress.** The percentage sits beside the bar, not over it. Chunks are
  ProgressChunkSize plus ProgressSpaceSize and stay inside the groove on all
  sides.
- **Scroll bars.** Sixteen arrow frames (four directions × four states) with a
  glyph composited on top. The gripper is centred on the thumb. Separate
  vertical art: never rotate.
- **Trackbar.** The thumb is the pentagon pointing at the ticks
  (ThumbBottom etc.); the rectangular Thumb is only for ticks on both sides.
- **Menus and tooltips.** Unthemed on XP: system colours, FlatMenus,
  MenuHilight.
- **Frames.** A generic sunken static frame has no msstyles part; it is a
  classic 3 px edge. The focus rectangle is a 1 px 50 % dotted checker.
- **Variants.** The default variant is `FILERESNAMES[0]`, *not* the
  alphabetically first NORMAL INI. A `.theme`'s ColorStyle/Size maps through
  COLORNAMES/SIZENAMES.
- **Colours.** KDE `[Colors:Window]` is XP **ButtonFace**; XP `Window` is KDE
  `[Colors:View]`.
- **Fonts.** They reach running apps only on
  `org.kde.KDEPlatformTheme.refreshFonts`.
- **Plugins in use.** Never overwrite a loaded plugin in place (SIGBUS across
  the session); use write-then-rename.
- **Performance.** `pefile` needs `fast_load` plus the resource directory only
  (89 s → 0.3 s warm for the index).

---

## 11. Phase 0 results

**0.1 — done.** Two commits: a verbatim import, then the adaptation. The
carried suites pass as they did in xp2plasma (119/119 editor checks; the only
failing suite, `test_tray_click`, failed identically before). XPLasma adopts
xp2plasma's asset library, schemes and 647 MB of downloads in place.

**0.3 — D1 holds on the host side.** The spike plugin (`native/`, a
`QProxyStyle` over "Windows") built in the dev container:

| Loaded into | Qt | Result |
| --- | --- | --- |
| Host probe (Qt 6 build) | 6.9.1 | ✔ drawn by XPLasma |
| Host probe (Qt 5 build) | 5.15.17 | ✔ drawn by XPLasma |
| **Editor's pip PySide6** | **6.11.2** | ✔ created and drawn: this is what `xp_style.py` and `system_qt.py` existed to work around, and it's gone |

The plugin needs only `Qt_6` symbols plus the `Qt_6.9` build-floor tag, and
exports nothing but Qt's two plugin entry points.

**The portable build works.** Built in `containers/release.Containerfile`
(Debian 12 plus the Qt Company's own 6.6.3 and 5.15.2):

| Build | Runs on | Result |
| --- | --- | --- |
| Qt 6.6.3 | host 6.9.1, PySide6 6.11.2 | ✔ loads, draws, tears down cleanly |
| Qt 5.15.2 | host 5.15.17 | ✔ after one fix (below) |

The fix: official Qt 5.15 binaries export sized `operator delete`
(`_ZdlPvm@Qt_5`), and linking Qt before libstdc++ bound the plugin to it, so
distro Qt 5 couldn't resolve it. libstdc++ is now linked first, and
`native/check-abi.sh` gates every release build against the whole class:
private API, non-Qt symbols bound to Qt, extra exports, glibc > 2.34. Both
plugins pass with a glibc floor of 2.2.5, which in practice means no floor
at all. `native/build.sh release` is the one command.

**0.5 — KDE re-creates the style on every StyleChanged, even when the name is
unchanged.** Measured with a probe on its own `XDG_CONFIG_HOME`: unchanged
XPLasma → new instance; Fusion → Fusion; XPLasma → new instance; unchanged →
new instance. So in KDE apps, a live theme switch needs only an updated
`xplasmarc` plus the broadcast xp2plasma already sends. That gives a fresh
style, full re-polish and relayout for free. The plugin's own config watcher
(§4.3) is only needed for Qt apps without the KDE platform theme.

**0.4 — the oracle works.** `refharness/` boots XP under `-snapshot`, hot-swaps
a job ISO into its CD drive over QMP, XP autoruns it, and results come back
over HTTP in about 2 s per round trip. XP SP3's uxtheme exports the file-level
entry points (ordinals 2/3/16), so **any `.msstyles` can be rendered without
applying it**, exactly as XP's own Display Properties previews. The first
measurement is Royale's push button (SizingMargins 8,8,9,9, content inset 3,
matching Appendix A), and it already set a rule for the Phase 2 gate: compare
renders *composited* over black and white, because un-premultiplying is only
exact to ±1. Two transport dead ends are recorded in `refharness/README.md`.

**Phase 1 — core, first pass (2026-09-23).** `native/core/`: a bounds-checked
PE resource reader, a DIB decoder, the INI model with D5's cascade, and
`Theme`. `test-core` passes on Qt 6 and Qt 5. **Conformance: 128/128 styles
(every variant, every section, every bitmap) read identically to
xp2plasma's parser**, except 25 bitmaps in three formats where they disagree.
All three went to XP's GDI through the oracle, and **the C++ core matched GDI
exactly every time**, including a real Qt bug: its BMP reader leaves pixels an
RLE stream never sets uninitialised, so the core decodes RLE itself (35/35
corpus RLE bitmaps now match GDI). The oracle also showed that XP refuses
unsigned fan styles (uxtheme's signature check), so uxtheme-level answers come
from Microsoft's five styles. *Remaining for Phase 1:* fuzzing.

**Phase 2 — renderer, first pass (2026-09-23).** `native/core/render` plus
`xplasma-render`, and `native/tests/oracle_compare.py`, which puts every part
of a style, in every state, at natural, stretched and squeezed sizes, next to
real uxtheme with **no tolerance**, composited over black and over white.
Every rule came from XP's answers, each recorded in the code where it lives
(truncating premultiply, bottom-up vertical sampling, the ImageFile/glyph
selection model, TrueSize scaling, MulDiv content-margin squeeze, frame-layout
default, BorderFill minimums, TS_MIN = content margins). The oracle also found
a parser bug that both parsers shared (comments after a section header).
Results on Microsoft's five styles, none tuned for except Royale:

| Style | Pixel-exact |
| --- | --- |
| Royale | 1014 / 1023 |
| Luna | 1053 / 1062 |
| Royale Noir | 1053 / 1062 |
| Embedded | 1042 / 1062 |
| Zune | 1033 / 1062 |
| **All** | **5195 / 5271 (98.6%)** |

Metrics (min, true and draw sizes, content rects, margins) are exact for
every part. The remainder is a handful of families (Tile-mode edges on
`Window.MaxCaption`, a squeezed split-button glyph, small window buttons in
Zune/Embedded); see NOTES.md. Font glyphs (`GlyphType=FontGlyph`) and
gradient `BorderFill`s aren't exercised by these styles yet.

**Phase 1 — done (2026-09-23).** The hour-long fuzz run finished clean:
8.6 M theme/PE, 1.8 M DIB and 5.1 M INI inputs under AddressSanitizer and
UBSan, no crash, leak or UB (the one denial of service it found in its first
minute, a 217-byte RLE header claiming a gigantic image, was fixed and kept as
a test vector).

**Phase 3 — in progress.** The control oracle (`refharness` `control`
directive + `xplasma-gallery --controls`) compares Qt widgets with XP's real
comctl32 v6 controls. So far **pixel-exact**: progress bars (14/14, all
values and widths), trackbars (15/15), scroll bars (9/9, gripper rule
included). Combo frames and buttons exact, tabs laid out as TCM_GETITEMRECT
says, labels placed within about 1px (glyph shapes differ; XP-style text
rendering is recorded as a font setting). Qt 5 and Qt 6 draw identically
apart from glyphs, once the tab height is computed from the font's OS/2
metrics as GDI does.

**Phase 3 / 3b — control fidelity, themed and Classic (2026-09-23).** The
control oracle now covers 79 controls and states (`native/tests/controls-1..3
.job`: buttons, check boxes, radios, group boxes, edits, combos, spin boxes,
progress bars, trackbars, scroll bars, tabs including overflowing rows,
list views (report, check boxes, selections), tree views (nesting, lone
roots, check boxes, selections), list boxes, menus and menu bars, toolbars,
status bars, tooltips; normal, hot, pressed, focused and disabled states),
and `native/tests/styles_matrix.py` runs all of it under every Microsoft
style *applied for real in XP* (uxdump `apply`: SetSystemVisualStyle plus the
style's system colours) and under Windows Classic (uxdump `classic`:
EnableTheming(FALSE) plus a scheme's colours from its .theme file), against
the plugin rendering the same .msstyles or .theme:

| Style | Pixel-exact controls (79 → 113 cases, 2026-09-23 evening) |
| --- | --- |
| Luna Silver, Royale, Royale Noir, Zune, Embedded | 76 / 79 → 107 / 113 each |
| Windows Classic, and a synthetic scheme with every colour its own | 76 / 79 → 105 / 113 |
| Luna Blue, Luna Olive | 65 / 79 → 92 / 113 |

(The 113 add disabled and mouse-pressed states, property-sheet pages,
MDI children normal, inactive and maximized, and a menu bar in a real
frame. Classic now runs with its own window metrics -- 16px scroll bars,
18px captions and menus -- as Display Properties applies them; the oracle
had been leaving Luna's in place. The MDI cases differ only in the
application icon, the caption glyphs under a visual style, and a maximized
child's menu titles (see NOTES).)

The 3 left everywhere are Qt-side, not drawable from a style: QTabBar
scrolls an overflowing row by pixels where XP scrolls by whole tabs (2),
and QStatusBar hard-codes its message's position. Luna Blue/Olive's extra
11 are ±1 colour rounding on antialiased edges (Qt's raster blend
approximates /255). Text is compared too, with Qt in the XP-text mode
(hinted, unantialiased, unkerned Tahoma 8), where FreeType matches GDI.
Qt 5 matches Qt 6 apart from glyph rasterisation (and the gallery can't
show a Qt 5 tooltip offscreen).

**Phase 4 — context (in progress, 2026-09-23).**
- *Resolver:* `xplasma-class` on a widget or any ancestor (QtQuick: the
  style object or its parents, or a declared `xplasmaClass`) sets the
  `Sub::` for every part the style looks up during that call; parts the
  theme has no `[Sub::Class]` for fall back to the class, as in uxtheme.
- *Dialog tab body:* a QTabWidget page inside a dialog is painted with
  TABP_BODY as EnableThemeDialogTexture does -- a true-size pattern from the
  page's corner. Exact against a real property-sheet page (uxdump
  `propsheet`), a check box drawn over it included.
- *Places bar:* a file dialog's places panel (KFilePlacesView, QSidebar)
  gets `PlacesBar::Toolbar`: its background (matching XP's Open dialog,
  uxdump `filedialog`, to the pixel) and its buttons for hot and current
  places.
- *MDI:* QMdiSubWindow draws XP's caption, frame and caption buttons
  (active, inactive, hot, pressed; the window region's cut corners), and in
  Classic the gradient caption (GDI's 16.16 GradientFill), DFC_CAPTION
  buttons and sizing border -- exact against a real MDICLIENT child apart
  from the application icon (uxdump `mdi`). Maximized, its buttons sit in
  the frame's menu bar as XP's do (themed WP_MDI* parts, Classic DFC
  buttons).
- *QML:* qqc2-desktop-style controls in System Settings draw through the
  same paths (checked offscreen with `XPLASMA_DUMP`, which grabs QtQuick
  windows too).
- *Classic's own metrics:* the plugin reads a scheme's NonclientMetrics
  (scroll bars, captions, menus, fonts); XP's thumb length rounds (MulDiv)
  where Qt's truncates; Marlett's arrows sit in the button's square.

**Phase 7 — GTK machinery (done, 2026-09-23).** `xplasma-render --export
THEME VARIANT OUTDIR` writes the resolved theme: every class, `Sub::Class`,
part and state as a PNG at its true size (the 9-slice source), with 120/144
DPI variants where the art differs, resolved sizing and content margins,
the properties a CSS generator needs, and [SysMetrics], in
`manifest.json` (Luna: 529 part states, 566 images, 2.7 MB);
`native/tests/export_check.py` validates it for all six styles.
`app/targets.py` is the toolkit registry: Apply and Reset walk Qt (real)
and GTK 3 / GTK 4 (stubs that report "not yet"); one target's failure is
reported and the rest still run.

## 12. The app in C++ (2026-09-23)

One language: everything that ships is C++/Qt 6 on `src/core`. The XP-VM
oracle tooling (`refharness/`, `tests/oracle/`) stays Python, dev-only
(`requirements-dev.txt`).

- **`src/app/`**, the `xplasma-app` library: state (`state.json`, replacing
  `applied.json` / `config.json`), one-time adoption of xp2plasma's library
  (moved, with symlinks left behind), host programs through QProcess and
  QtDBus, archives (static libarchive plus an ISO 9660 reader of its own), the
  theme library (`.theme` read and written as XP does; user schemes are
  `.theme` files), and the generators: colours and fonts, the Aurorae
  decoration, the panel theme, the Start button and its StartPanel art, icons,
  the tray and clock overrides, title shadows.
- **The Start menu** is XPLasma's own plasmoid, `fyi.hotsocket.xplasma.start`:
  a complete package, not a patched Kickoff (Plasma's stock launcher). Only
  its data (`theme.json`, the images) changes per look. The Start flag comes
  from `explorer.exe` (bitmap 143; 176 for Classic), and a pill built without
  it is an error.
- **Notifications as XP's balloons** (a patched `org.kde.plasma.notifications`,
  active when the panel theme has `widgets/xp-notify`): one balloon at a
  time, oldest first, from the sender's own tray icon or a temporary one;
  the rest wait as icons, and one that timed out unread stays, blinking to
  a beat of its own, until clicked (it shows again). File transfers are
  XP's copy dialog (shell32's animations, Cancel kills the job). Colours
  come from the application palette (`Window.palette` has the tooltip
  pair; `SystemPalette` doesn't), not Kirigami's, which are the panel's.
- **`xplasma`** (CLI) and **`xplasma-edit`** (Display Properties, Qt Widgets
  in the system's own widget style; only the samples are drawn by XPLasma).
- **Date and Time Properties** (`xplasma-edit --date-time`, the clock's
  double-click): laid out from timedate.cpl's own templates in dialog units,
  its zones from HIVESFT.INF (75, each mapped to a Linux zone by Qt's
  Windows-to-IANA table), its world map timedate.cpl's bitmap 50 scrolled as
  XP scrolls it (`338 + offset * 356 / 1440`, measured on XP), the calendar
  and clock drawn as XP draws them. It sets the clock, zone and network time
  through systemd's timedated (the desktop's polkit agent asks for a
  password).
- **Build and install:** `build.sh` installs into `build/<mode>/`
  (`bin/`, `lib/xplasma/qt{6,5}/styles/`, `share/applications/`);
  `install.sh` copies that into a prefix (default `~/.local`). No AppImage or
  Flatpak machinery, and no bundled tool binaries.
- **Retired:** the Python app and its tests, the parser conformance test
  (Python ↔ C++; there is one parser now), `prototypes/start_menu/`.

