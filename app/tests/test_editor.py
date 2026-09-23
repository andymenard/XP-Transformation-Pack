#!/usr/bin/env python3
"""Self-test for the XP Display Properties clone. Never touches the live
session: every apply entry point is stubbed.

Run:  QT_QPA_PLATFORM=offscreen .venv/bin/python3 tests/test_editor.py
"""
import hashlib
import os
import sys
import tempfile
from unittest.mock import patch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

from PySide6.QtWidgets import QApplication, QComboBox  # noqa: E402
from PySide6.QtGui import QFont

app = QApplication.instance() or QApplication(["xplasma-edit"])
app.setStyle("Fusion")
# Fixed XP geometry is measured with the reference font, not whichever font
# the host happens to use after an installer reset to Breeze.
app.setFont(QFont("Tahoma", 8))

import appearance_editor as ae      # noqa: E402
from appearance_editor import _same_file  # noqa: E402
import classic_gen as cg            # noqa: E402
import fonts_gen                    # noqa: E402
import hostexec                     # noqa: E402
import preview_gen                  # noqa: E402
import theme_file as tf             # noqa: E402
import theme_index as tx            # noqa: E402
import xplasma                      # noqa: E402

# Saved schemes are user data, not a fixture. Keep every test write isolated
# from the real profile so this suite has no desktop-state prerequisite.
TEST_USER_SCHEMES = tempfile.TemporaryDirectory(prefix="xplasma-test-schemes-")
cg.USER_SCHEMES_DIR = TEST_USER_SCHEMES.name

PASS, FAIL = [], []


def check(name, cond, extra=""):
    (PASS if cond else FAIL).append(name)
    print(f"  {'ok  ' if cond else 'FAIL'} {name}{(' -- ' + str(extra)) if extra and not cond else ''}")


def section(title):
    print(f"\n== {title}")


_orig = (cg.save_applied, xplasma.apply_scheme_live, xplasma.apply_msstyles_live)
CALLS = []


def _install_stubs():
    cg.save_applied = lambda s: "/dev/null"
    xplasma.apply_scheme_live = lambda scheme, name, **kw: (
        CALLS.append(("classic", name, kw.get("theme_file"), scheme)), 0)[1]
    xplasma.apply_msstyles_live = lambda path, name, **kw: (
        CALLS.append(("style", name, kw.get("ini_resource_name"), path)), 0)[1]


def _restore():
    cg.save_applied, xplasma.apply_scheme_live, xplasma.apply_msstyles_live = _orig


def run_job(win):
    """Run the current tab's job synchronously and report what it dispatched."""
    CALLS.clear()
    title = win.tabs.tabText(win.tabs.currentIndex())
    job = win._job_for_themes() if title == "Themes" else win._job_for_appearance()
    if job is None:
        return None
    job.run(lambda _m: None)
    if job.after:
        job.after()
    return CALLS[0] if CALLS else ("none",)


_install_stubs()
win = ae.AppearanceEditor()
win.show()
app.processEvents()

section("window and tabs")
check("fixed width is 398 (the real dialog's content area, measured)", win.width() == 398, win.width())
check("client area is fixed at 398x423",
      win.width() == 398 and win.height() == 423
      and win.minimumSize() == win.maximumSize(), win.size())
check("tab group has a 6px left and 8px top margin",
      (win.tabs.x(), win.tabs.y()) == (6, 8), win.tabs.geometry())
check("Themes sample image is 347x219 inside its border",
      (win.themes_pane.label.width(), win.themes_pane.label.height()) == (347, 219),
      win.themes_pane.label.size())
check("Themes sample outer box includes the 3px border",
      (win.themes_pane.width(), win.themes_pane.height()) == (353, 225), win.themes_pane.size())
# XP's other two tabs, Screen Saver and Settings, are not part of xp2plasma and
# are gone rather than shown disabled -- a tab that can never do anything is
# worse than a tab that isn't there.
check("tab titles are the three XPLasma implements",
      [win.tabs.tabText(i) for i in range(win.tabs.count())]
      == ["Themes", "Desktop", "Appearance"])
check("every tab is enabled",
      all(win.tabs.isTabEnabled(i) for i in range(win.tabs.count())))
check("every tab starts with a rendered preview",
      all(pane.label.pixmap() is not None and not pane.label.pixmap().isNull()
          for pane in (win.themes_pane, win.desktop_pane, win.appearance_pane)))
# One row per imported wallpaper. (xp2plasma's old in-tree dump held ~200
# images, most of them installer bitmaps; setup's pruned import holds ~29.)
check("the Background list leads with (None) and lists the wallpapers",
      win.wallpaper_list.item(0).text() == "(None)"
      and win.wallpaper_list.count() == 1 + len(ae._wallpaper_library()) > 20,
      win.wallpaper_list.count())
# (None) is the one row with no icon, as in XP -- it is the absence of a
# wallpaper, not a file.
check("(None) carries no icon",
      win.wallpaper_list.item(0).icon().isNull())
check("every wallpaper row carries XP's image-file icon",
      all(not win.wallpaper_list.item(i).icon().isNull()
          for i in range(1, win.wallpaper_list.count())))
check("Position offers XP's three, in XP's order",
      [win.position_combo.itemText(i) for i in range(win.position_combo.count())]
      == ["Center", "Tile", "Stretch"])
check("the Desktop tab seeds itself from the live desktop",
      win.position_combo.currentText() in ("Center", "Tile", "Stretch")
      and len(win.desktop_color.color()) == 3)
# Every tab's controls, keyed by title rather than index. The index-keyed
# version this replaced named tab 3 for Appearance; when Screen Saver and
# Settings were removed that silently stopped checking Appearance at all.
_PAGE_CONTROLS = {
    "Themes": lambda: [win.themes_pane, win.theme_combo, win.save_btn, win.delete_btn],
    "Desktop": lambda: [win.desktop_pane, win.wallpaper_list, win.browse_btn,
                        win.position_combo, win.desktop_color, win.customize_btn],
    "Appearance": lambda: [win.appearance_pane, win.style_combo, win.colour_combo,
                           win.fontsize_combo, win.effects_btn, win.advanced_btn],
}
heights = set()
for i in range(win.tabs.count()):
    win.tabs.setCurrentIndex(i)
    app.processEvents()
    heights.add(win.height())
    page = win.tabs.widget(i)
    check(f"{win.tabs.tabText(i)} controls fit inside the fixed page",
          all(page.rect().contains(c.geometry()) for c in _PAGE_CONTROLS[win.tabs.tabText(i)]()))
check("height is fixed across every tab", len(heights) == 1, heights)
win.tabs.setCurrentIndex(0)

section("Desktop tab")
# This tab edits the *desktop*, which is not the visual style and not the
# classic scheme -- touching it must leave both alone.
win.tabs.setCurrentIndex(1)
app.processEvents()
scheme_before = win.scheme.name
style_before = win._style()
win.wallpaper_list.setCurrentRow(0)                       # (None)
check("choosing (None) clears the wallpaper", win._wallpaper_choice is None)
check("choosing a background marks the dialog dirty", win._dirty)
check("the desktop is not a scheme edit", win.scheme.name == scheme_before)
check("the desktop does not change the visual style", win._style() is style_before)
win.wallpaper_list.setCurrentRow(1)
check("choosing a wallpaper records a real file",
      bool(win._wallpaper_choice) and os.path.isfile(win._wallpaper_choice))
win.position_combo.setCurrentIndex(1)                     # Tile
win._on_position_changed(1)
check("Tile selects the Tile fill mode", win._fill_mode() == tf.FILL_TILE)
win.position_combo.setCurrentIndex(2)                     # Stretch
win._on_position_changed(2)
check("Stretch selects the Stretch fill mode", win._fill_mode() == tf.FILL_STRETCH)
check("XP's three positions map onto three distinct fill modes",
      len({dict(ae.POSITIONS)[n] for n in ("Center", "Tile", "Stretch")}) == 3)

with patch.object(ae.apply_mod, "apply_wallpaper") as wall, \
        patch.object(ae.apply_mod, "apply_wallpaper_color") as solid:
    win.position_combo.setCurrentIndex(1)
    win._job_for_desktop().run(lambda _message: None)
    check("Apply passes the image, the position and the colour",
          wall.call_args.args == (win._wallpaper_choice, tf.FILL_TILE,
                                  win.desktop_color.color()), wall.call_args)
    check("a wallpaper Apply is not a flat colour", not solid.called)
    win.wallpaper_list.setCurrentRow(0)                   # (None)
    win._job_for_desktop().run(lambda _message: None)
    check("(None) applies the desktop colour instead",
          solid.call_args.args == (win.desktop_color.color(),), solid.call_args)
    check("(None) does not apply a wallpaper", wall.call_count == 1)
check("the Desktop job regenerates nothing",
      win._job_for_desktop().after is None and not win._job_for_desktop().sync_palette)
win.wallpaper_list.setCurrentRow(1)
win.tabs.setCurrentIndex(0)
app.processEvents()

section("Themes combo")
stock_luna = next(e for e in tx.list_themes() if e.theme_file
                  and os.path.basename(e.theme_file) == "Luna.theme")
check("Luna.theme uses its Windows XP display name", stock_luna.label == "Windows XP")
check("browsing Luna uses the same display name",
      tx.entry_from_path(stock_luna.theme_file).label == "Windows XP")
rows = win._rows
kinds = {}
for r in rows:
    kinds[r.kind] = kinds.get(r.kind, 0) + 1
check("every theme file is listed", kinds.get("file", 0) == len(tx.list_themes()), kinds)
check("Add more... is the only utility entry", kinds.get("add") == 1
      and not kinds.get("browse") and not kinds.get("setup"))
check("all editor combos are selection-only",
      all(not c.isEditable() and c.lineEdit() is None for c in win.findChildren(QComboBox)))
picked = win.theme_combo.currentText()
win.theme_combo.setEditText("not a theme")
check("arbitrary text cannot replace the selected theme", win.theme_combo.currentText() == picked)
check("the full theme list stays available", win.theme_combo.count() == len(rows))

section("Add more opens the theme directory without selecting a theme")
before = next(r for r in rows if r.kind == "file")
win._select_row(before)
win._show_theme(before)
win._set_dirty(False)
with tempfile.TemporaryDirectory() as folder, patch.object(ae.paths, "THEMES", folder), \
        patch.object(ae.host, "run") as opened:
    win._select_row(next(r for r in rows if r.kind == "add"))
    win._on_theme_picked()
    check("opens the themes directory", opened.call_args.args[0] == ["xdg-open", folder])
    check("keeps the selected theme", win._current_row() is before)
    check("does not dirty Apply", not win._dirty and not win.apply_btn.isEnabled())
    check("refreshes the library on return", win._refresh_library_on_activate)
    win._reload_themes()
    check("reloading preserves the selection", win._current_row().entry.path == before.entry.path)
    win._refresh_library_on_activate = False

section("picking a theme")
for label in ("Windows XP", "Windows Classic"):  # complete presets, not bare styles
    row = next((r for r in rows if r.kind == "file" and r.entry.label == label), None)
    if row is None:
        check(f"row {label!r} exists", False)
        continue
    win._fill_theme_combo()
    win._select_row(row)
    win._show_theme(row)
    app.processEvents()
    check(f"{label}: preview rendered",
          win.themes_pane.label.pixmap() is not None and not win.themes_pane.label.pixmap().isNull())
    check(f"{label}: description written", len(win.theme_desc.fullText()) > len(label), win.theme_desc.fullText())

section("Themes tab dispatches the right pipeline")
win.tabs.setCurrentIndex(0)
row = next(r for r in rows if r.kind == "file" and r.entry.label == "Windows Classic")
win._select_row(row)
win._show_theme(row)
got = run_job(win)
check("a bundle with no visual style applies as a classic scheme", got and got[0] == "classic", got)
check("and carries the bundle, for its wallpaper", bool(got and got[2] and got[2].endswith(".theme")), got)

# The ISO's own Windows XP.theme: a bundle that does name a visual style.
row = next(r for r in rows if r.kind == "file" and r.entry.label == "Windows XP")
win._select_row(row)
win._show_theme(row)
got = run_job(win)
check("the stock XP bundle applies Luna, in the variant it asks for",
      got and got[0] == "style" and got[2] == "NORMALBLUE_INI", got)
check("bare visual styles are not theme presets", all(r.entry.theme_file for r in rows if r.kind == "file"))
# A .theme naming a non-default variant: Windows spells it the style's way
# ("Metallic"), which only the style's own COLORNAMES/FILERESNAMES tables tie
# to the DARKROYALE resource.
import tempfile                                    # noqa: E402
style_path = next(s.path for s in win._styles if s.path and s.path.endswith("Royale Noir.msstyles"))
bundle = os.path.join(tempfile.mkdtemp(), "Royale Noir.theme")
with open(bundle, "w", encoding="utf-8") as fh:
    fh.write(f"[VisualStyles]\nPath={style_path}\n"
             "ColorStyle=Metallic\nSize=NormalSize\n")
entry = tx.ThemeEntry("Royale Noir", style_path, theme_file=bundle)
check("a bundle's ColorStyle=Metallic resolves to the DarkRoyale variant",
      tx.bundle_variant(entry, tx.visual_style_from(style_path)) == "NORMALDARKROYALE_INI",
      tx.bundle_variant(entry, tx.visual_style_from(style_path)))
for r in win._rows:
    if r.kind == "add":
        win._select_row(r)
check("Add more... has no job of its own", win._job_for_themes() is None)

section("Appearance drop-downs")
noir_row = ae.ThemeRow("Royale Noir", "file", entry=entry)
win._rows.insert(0, noir_row)
win._fill_theme_combo()
win._select_row(noir_row)
win._show_theme(noir_row)
check("Royale Noir theme selects the dark colour", win._colour_key() == "DARKROYALE", win._colour_key())
check("Royale Noir apply uses the same dark variant", run_job(win)[2] == "NORMALDARKROYALE_INI")
win.tabs.setCurrentIndex(3)
styles = win._styles
check("the style list is exactly the index", len(styles) == len(tx.list_visual_styles()), len(styles))
check("Windows Classic style leads", styles[0].is_classic and styles[0].label == tx.CLASSIC_LABEL)
luna_i = next(i for i, s in enumerate(styles) if s.path and s.path.endswith("Royale Noir.msstyles"))
check("a style uses XP's DisplayName, with folder disambiguation for duplicates",
      styles[luna_i].label == "Royale" or styles[luna_i].label.startswith("Royale ("), styles[luna_i].label)
win.style_combo.setCurrentIndex(luna_i)
win._on_style_changed(luna_i)
colours = [(win.colour_combo.itemText(i), win.colour_combo.itemData(i)) for i in range(win.colour_combo.count())]
check("it offers XP's colour schemes, in XP's order and words",
      colours == [("Royale", "ROYALE"), ("Royale Noir", "DARKROYALE")], colours)
check("Advanced is off for a visual style", not win.advanced_btn.isEnabled())
i = win.colour_combo.findData("DARKROYALE")
win.colour_combo.setCurrentIndex(i)
win._on_colour_changed(i)
sizes = [(win.fontsize_combo.itemText(j), win.fontsize_combo.itemData(j)) for j in range(win.fontsize_combo.count())]
check("it offers XP's three sizes, in XP's words",
      sizes == [("Normal", "NORMAL"), ("Large Fonts", "LARGEFONTS"), ("Extra Large Fonts", "EXTRALARGE")], sizes)
single = next((i for i, s in enumerate(styles)
               if not s.is_classic and len(s.sizes(s.colours()[0].key)) == 1), None)
if single is not None:   # none in the stock set; the parked collection has them
    win.style_combo.setCurrentIndex(single)
    win._on_style_changed(single)
    check("a style with one size offers only that one", win.fontsize_combo.count() == 1)

section("colour variants actually change the preview")
win.style_combo.setCurrentIndex(luna_i)
win._on_style_changed(luna_i)
hashes = {}
for colour in ("ROYALE", "DARKROYALE"):
    i = win.colour_combo.findData(colour)
    win.colour_combo.setCurrentIndex(i)
    win._on_colour_changed(i)
    for size in ("NORMAL", "LARGEFONTS"):
        j = win.fontsize_combo.findData(size)
        win.fontsize_combo.setCurrentIndex(j)
        win._on_fontsize_changed(j)
        img = win._draw_appearance((240, 120))
        hashes[f"{colour}/{size}"] = hashlib.sha1(img.tobytes()).hexdigest()[:10]
check(f"all {len(hashes)} colour/size variants preview differently",
      len(set(hashes.values())) == len(hashes), hashes)
info = win._appearance_info()
check("a visual style's preview carries its frame art",
      info.frame_left is not None and info.frame_bottom is not None and info.top_h >= info.caption_h)

section("classic style")
win.style_combo.setCurrentIndex(0)
win._on_style_changed(0)
bases = [win.colour_combo.itemData(i) for i in range(win.colour_combo.count())]
check("classic lists XP's stock schemes", len(bases) == len(tx.classic_base_schemes()), len(bases))
check("no (large) suffix in the base list", not any("(large)" in b or "(extra large)" in b for b in bases))
check("Advanced is on for the classic", win.advanced_btn.isEnabled())
i = win.colour_combo.findData("Windows Standard")
win.colour_combo.setCurrentIndex(i)
win._on_colour_changed(i)
sizes = [win.fontsize_combo.itemData(j) for j in range(win.fontsize_combo.count())]
check("Windows Standard offers size variants beyond Normal", "LARGEFONTS" in sizes, sizes)
j = win.fontsize_combo.findData("LARGEFONTS")
win.fontsize_combo.setCurrentIndex(j)
win._on_fontsize_changed(j)
check("picking Large Fonts loads '(large)'", win.scheme.name == "Windows Standard (large)", win.scheme.name)
ref = preview_gen.render(win.scheme, (240, 120)).tobytes()
check("the classic sample is classic_gen's own composition",
      win._draw_appearance((240, 120)).tobytes() == ref)
got = run_job(win)
check("classic Appearance applies as a classic scheme", got and got[0] == "classic", got)
solo = next((b for b in bases if not tx.classic_size_available(b, "LARGEFONTS")), None)
if solo is None:
    check("found a base with no large variant", False)
else:
    i = win.colour_combo.findData(solo)
    win.colour_combo.setCurrentIndex(i)
    win._on_colour_changed(i)
    check(f"{solo!r} offers only Normal",
          [win.fontsize_combo.itemData(j) for j in range(win.fontsize_combo.count())] == ["NORMAL"])

section("Advanced dialog")
win.style_combo.setCurrentIndex(0)
win._on_style_changed(0)
i = win.colour_combo.findData("Windows Standard")
win.colour_combo.setCurrentIndex(i)
win._on_colour_changed(i)
win._on_fontsize_changed(0)
win.scheme = cg.load_scheme(scheme_name="Windows Standard")
win._edited = False
dlg = ae.AdvancedDialog(win.scheme, win)
check("all Advanced combos are selection-only",
      all(not c.isEditable() and c.lineEdit() is None for c in dlg.findChildren(QComboBox)))
before = win.scheme.c("buttonface")
dlg.grid.item_combo.setCurrentText("3D Objects")
dlg.grid.color_btn.set_color((1, 2, 3))
dlg.grid.color_btn.picked.emit()
check("an edit lands in the shared scheme", win.scheme.c("buttonface") == (1, 2, 3), win.scheme.c("buttonface"))
dlg.grid.item_combo.setCurrentText("Active Title Bar")
dlg.grid.font_combo.setCurrentText("DejaVu Serif")
dlg.grid._on_font_picked()
check("a title-bar font edit lands in the scheme", win.scheme.caption_font[0] == "DejaVu Serif", win.scheme.caption_font)
win._on_advanced_changed()
check("the editor marks the scheme edited", win._edited is True)
check("the scheme keeps its name as a drop-down anchor", win.scheme.name == "Windows Standard", win.scheme.name)
got = run_job(win)
check("Apply sends the *edited* scheme, title-bar font included",
      got and got[0] == "classic" and got[3].caption_font[0] == "DejaVu Serif" and got[3].c("buttonface") == (1, 2, 3),
      got and (got[3].caption_font, got[3].c("buttonface")))
check("Apply stashes it in the hidden scheme", cg.applied_scheme_path().endswith(".applied.json"))
dlg.reject()
check("Cancel puts the old colour back", win.scheme.c("buttonface") == before, win.scheme.c("buttonface"))
check("and the old font", win.scheme.caption_font[0] != "DejaVu Serif", win.scheme.caption_font)
dlg.deleteLater()

section("Save As / Delete round-trip")
NAME = "xp2plasma self-test scheme"
with tempfile.TemporaryDirectory(prefix="xplasma-test-schemes-") as schemes_dir:
    saved_schemes_dir = cg.USER_SCHEMES_DIR
    cg.USER_SCHEMES_DIR = schemes_dir
    try:
        win.scheme = cg.load_scheme(scheme_name="Windows Standard")
        win.scheme.name = NAME
        path = cg.save_scheme(win.scheme, NAME)
        check("saving writes a user scheme file", os.path.exists(path) and cg.user_scheme_path(NAME) == path)
        win._reload_themes()
        check("saved colors are not full Themes", not any(r.label == NAME for r in win._rows))
        win.tabs.setCurrentIndex(3)
        win.style_combo.setCurrentIndex(0)
        win._fill_colour_combo(NAME)
        win._fill_size_combo("NORMAL")
        win._on_appearance_changed()
        got = run_job(win)
        check("a saved scheme applies as a classic scheme", got and got[0] == "classic", got)
        os.remove(path)
        win._reload_themes()
        check("deleting removes it from the list", not any(r.kind == "user" and r.label == NAME for r in win._rows))
        check("stock schemes are not deletable", cg.user_scheme_path("Windows Standard") is None)
    finally:
        cg.USER_SCHEMES_DIR = saved_schemes_dir

section("classic preview pieces")
s = cg.load_scheme(scheme_name="Windows Standard")
info = tx.info_from_scheme(s)
want = preview_gen._glyph_box(s, cg.GLYPHS["close"], (s.btn_w, s.btn_h)).tobytes()
check("classic caption buttons are Marlett glyphs, not the word 'close'", info.buttons[2].tobytes() == want)
check("the classic desktop preview draws the scheme's own window", info.scheme is s)

section("fonts go live")
calls = []
real_run = hostexec.run
hostexec.run = lambda cmd, **kw: calls.append(cmd)
try:
    fonts_gen.apply_font_specs(("Tahoma", 8, True), ("Tahoma", 8, False))
finally:
    hostexec.run = real_run
check("kdeglobals writes use --notify", all("--notify" in c for c in calls if c[0] == "kwriteconfig6"))
check("the platform theme is told to refresh fonts",
      any("org.kde.KDEPlatformTheme.refreshFonts" in c for c in calls), calls)

section("every theme and style renders")
bad_style = []
for st in win._styles[1:]:
    try:
        colour, size = st.colours()[0].key, st.sizes()[0].key
        preview_gen.render_style(tx.info_from_style(st.path, st.resolve(colour, size),
                                                    tf.find_theme_file_for(st.path), st.label), (160, 80))
    except Exception as exc:
        bad_style.append(f"{st.label}: {type(exc).__name__}: {exc}")
check(f"all {len(win._styles) - 1} visual styles preview", not bad_style, bad_style[:4])
bad_theme, unsupported = [], []
for e in tx.list_themes():
    try:
        preview_gen.render_desktop(tx.info_for_entry(e), (160, 80))
    except (ValueError, OSError) as exc:
        unsupported.append(e.label)
    except Exception as exc:
        bad_theme.append(f"{e.label}: {type(exc).__name__}: {exc}")
check(f"all {len(tx.list_themes())} themes preview or fail cleanly", not bad_theme, bad_theme[:4])
check("every entry that fails to preview says so up front",
      all(not e.supported for e in tx.list_themes() if e.label in unsupported), unsupported)
unreadable = next((r for r in win._rows if r.kind == "file" and not r.entry.supported), None)
if unreadable is not None:   # Vista-format styles live in the parked collection
    win.tabs.setCurrentIndex(0)
    win._select_row(unreadable)
    win._show_theme(unreadable)
    check("selecting it explains itself instead of raising", "NOT SUPPORTED" in win.theme_desc.fullText())
    check("and Apply refuses it", win._job_for_themes() is None)

section("widget style")
import widget_style as ws         # noqa: E402
royale = next(st for st in win._styles if st.path and st.path.endswith("Royale Noir.msstyles"))
real_ws = (ws.ACTIVE_DIR, ws.CONFIG)
with tempfile.TemporaryDirectory(prefix="xplasma-test-style-") as scratch:
    ws.ACTIVE_DIR, ws.CONFIG = os.path.join(scratch, "active"), os.path.join(scratch, "xplasmarc")
    try:
        frozen = ws.freeze(royale.path)
        check("the applied style is frozen under its content hash",
              os.path.basename(frozen) == hashlib.sha256(open(royale.path, "rb").read()).hexdigest() + ".msstyles")
        check("freezing again reuses the same copy", ws.freeze(royale.path) == frozen)
        ws.write_config(frozen, "NORMALDARKROYALE_INI", royale.path)
        text = open(ws.CONFIG, encoding="utf-8").read()
        check("xplasmarc names the frozen copy and its variant",
              f"Path={frozen}" in text and "Variant=NORMALDARKROYALE_INI" in text, text)
        stale = os.path.join(ws.ACTIVE_DIR, "0" * 64 + ".msstyles")
        open(stale, "wb").close()
        ws.prune_frozen(keep=frozen)
        check("older frozen copies are pruned, the live one kept",
              not os.path.exists(stale) and os.path.exists(frozen))
    finally:
        ws.ACTIVE_DIR, ws.CONFIG = real_ws
win.tabs.setCurrentIndex(3)
win.style_combo.setCurrentIndex(0)
win._on_style_changed(0)
seen = {}
xplasma.apply_scheme_live = lambda scheme, name, **kw: seen.update(kw) or 0
win._job_for_appearance().run(lambda _m: None)
check("Apply defaults to building widgets (checkbox commented out for now)",
      seen.get("widgets") is True, seen)
_install_stubs()

section("the applied theme is remembered")
import applied_state                 # noqa: E402
import tempfile                      # noqa: E402
check("the editor opens on the Themes tab", win.tabs.tabText(0) == "Themes")

real_state = applied_state.STATE_PATH
applied_state.STATE_PATH = os.path.join(tempfile.mkdtemp(), "applied.json")
try:
    # A visual style applied from the CLI or the editor, variant and all.
    xp = next(e for e in tx.list_themes() if e.label == "Windows XP")
    applied_state.save(kind="style", name="Windows XP", msstyles=xp.style_path,
                       ini="NORMALMETALLIC_INI", theme_file=xp.theme_file)
    check("state round-trips", applied_state.load().get("ini") == "NORMALMETALLIC_INI")
    reopened = ae.AppearanceEditor()
    check("it reopens on the Themes tab",
          reopened.tabs.tabText(reopened.tabs.currentIndex()) == "Themes")
    picked = reopened._current_row()
    check("a variant differing from the preset is marked modified", picked is not None and picked.kind == "current",
          picked and picked.label)
    check("and the Appearance tab on that style",
          reopened._style().path and _same_file(reopened._style().path, xp.style_path),
          reopened._style().label)
    check("and on the variant that was applied",
          (reopened._colour_key(), reopened._size_key()) == ("METALLIC", "NORMAL"),
          (reopened._colour_key(), reopened._size_key()))
    reopened.deleteLater()

    # A classic scheme applied from the command line.
    applied_state.save(kind="classic", name="Windows Classic", scheme="Rainy Day", theme_file=None)
    cg.save_scheme(cg.load_scheme(scheme_name="Rainy Day"), "Rainy Day")   # a user row to match
    reopened = ae.AppearanceEditor()
    picked = reopened._current_row()
    check("a saved classic scheme is a modified current theme", picked is not None and picked.kind == "current"
          and reopened._colour_key() == "Rainy Day",
          picked and picked.label)
    reopened.deleteLater()
    os.remove(cg.user_scheme_path("Rainy Day"))
finally:
    applied_state.STATE_PATH = real_state

section("default button and dialog keys")
from PySide6.QtCore import Qt
from PySide6.QtTest import QTest
from PySide6.QtWidgets import QDialog
keys = ae.AppearanceEditor()
keys.show()
keys.activateWindow()
app.processEvents()
check("editor uses Qt dialog keyboard handling", isinstance(keys, QDialog))
keys.theme_combo.setFocus()
app.processEvents()
check("OK is default while a dropdown has focus", keys.ok_btn.isDefault())
clicked = []
keys.ok_btn.clicked.disconnect()
keys.ok_btn.clicked.connect(lambda: clicked.append("ok"))
keys.cancel_btn.clicked.disconnect()
keys.cancel_btn.clicked.connect(lambda: clicked.append("cancel"))
QTest.keyClick(keys.theme_combo, Qt.Key_Return)
check("Enter on the closed theme dropdown activates OK", clicked == ["ok"], clicked)
keys.cancel_btn.setFocus()
app.processEvents()
check("focused Cancel becomes the default", keys.cancel_btn.isDefault() and not keys.ok_btn.isDefault())
QTest.keyClick(keys.cancel_btn, Qt.Key_Return)
check("Enter on Cancel activates Cancel", clicked == ["ok", "cancel"], clicked)
keys.theme_combo.setFocus()
app.processEvents()
check("default returns to OK when focus leaves Cancel", keys.ok_btn.isDefault())
keys._set_busy(True, "Testing")
QTest.keyClick(keys, Qt.Key_Return)
check("Enter does not activate disabled OK during apply", clicked == ["ok", "cancel"], clicked)
keys._set_busy(False, "")
QTest.keyClick(keys, Qt.Key_Escape)
check("Escape dismisses the editor", not keys.isVisible())
keys.deleteLater()

section("Shift force-Apply")
force = ae.AppearanceEditor()
force.show()
force.activateWindow()
force.theme_combo.setFocus()
app.processEvents()
force._set_dirty(False)
check("Apply starts disabled with no pending changes", not force.apply_btn.isEnabled())
forced_calls = []
force._apply = lambda: forced_calls.append("apply")
QTest.keyPress(force.theme_combo, Qt.Key_Shift)
check("holding Shift in a child control enables Apply", force.apply_btn.isEnabled())
check("Shift does not mark the theme dirty", not force._dirty)
QTest.mouseClick(force.apply_btn, Qt.LeftButton, Qt.ShiftModifier)
check("Shift-click calls the existing Apply action", forced_calls == ["apply"], forced_calls)
QTest.keyRelease(force.apply_btn, Qt.Key_Shift)
check("releasing Shift disables unchanged Apply", not force.apply_btn.isEnabled())
force._set_dirty(True)
QTest.keyPress(force.theme_combo, Qt.Key_Shift)
QTest.keyRelease(force.theme_combo, Qt.Key_Shift)
check("real pending changes keep Apply enabled after release", force.apply_btn.isEnabled())
force._set_busy(True, "Testing")
QTest.keyPress(force.theme_combo, Qt.Key_Shift)
force._set_dirty(True)
check("Shift and dirty changes cannot re-enable Apply while busy", not force.apply_btn.isEnabled())
QTest.keyRelease(force.theme_combo, Qt.Key_Shift)
force._set_busy(False, "")
force._set_dirty(False)
QTest.keyPress(force.theme_combo, Qt.Key_Shift)
from PySide6.QtCore import QEvent
app.sendEvent(force, QEvent(QEvent.WindowDeactivate))
check("losing window activation clears the Shift override", not force.apply_btn.isEnabled())
QTest.keyRelease(force.theme_combo, Qt.Key_Shift)
force.close()
force.deleteLater()

_restore()
TEST_USER_SCHEMES.cleanup()
print(f"\n{len(PASS)} passed, {len(FAIL)} failed")
if FAIL:
    print("\nFAILURES:")
    for f in FAIL:
        print("  -", f)
sys.exit(1 if FAIL else 0)
