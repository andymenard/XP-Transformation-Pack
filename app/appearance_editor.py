#!/usr/bin/env python3
"""A working clone of the Windows XP "Display Properties" dialog.

Two live tabs, matching XP's:

  * **Themes**     -- complete .theme presets, including saved desktop presets,
                      with the desktop preview and Save As.../Delete/Browse.
  * **Appearance** -- XP's three drop-downs ("Windows and buttons",
                      "Color scheme", "Font size") driving the same preview.
                      **Advanced...** opens the Windows 2000 item grid, which
                      is exactly what Advanced... does in XP.

Applying goes through the same `xplasma.py` pipelines the CLI uses, so the
editor and the command line produce byte-identical output. Nothing here is
live except the preview: the editor's own colours change when you press Apply.

A `.theme` bundle names its variant as `[VisualStyles] ColorStyle`/`Size`
("Metallic", "NormalSize"); `theme_index.bundle_variant` maps that onto the
style's INI resource through the style's own COLORNAMES/SIZENAMES/FILERESNAMES
tables, so a bundle applies in the variant it asks for.
"""
from __future__ import annotations

import copy
import os
import sys
import threading
from dataclasses import dataclass
from typing import Callable

from PySide6.QtCore import QEvent, QPoint, QSize, Qt, QThread, QTimer, Signal
from PySide6.QtGui import (QBrush, QColor, QFont, QIcon, QImage, QPainter, QPalette,
                           QPen, QPixmap, QPolygon)
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QColorDialog, QComboBox, QDialog, QFileDialog, QFrame,
    QGridLayout, QHBoxLayout, QInputDialog, QLabel, QListWidget, QListWidgetItem,
    QMessageBox, QPushButton, QSizePolicy, QSpinBox, QTabWidget, QVBoxLayout, QWidget,
)

import apply as apply_mod
import applied_state
import classic_gen as cg
import hostexec as host
import paths
import preview_gen
import setup_assets
import setup_ui
import theme_file as tf
import theme_index as tx
import theme_presets
import xp_icons

LIVE_NAME = "Windows Classic"
FONT_SIZES = [str(n) for n in range(6, 25)]

# Display Properties was a fixed-size dialog and stays one here. Measured off
# a real XP dialog: the content area inside the window decoration is 398x423,
# the Themes sample image is 347x219 inside its 3px sunken edge.
WINDOW_WIDTH = 398
WINDOW_HEIGHT = 423
LAYOUT_GAP = 8
BORDER = 3
CONTROL_HEIGHT = 21          # drop-downs have no transparent outer button rim
# Luna's button bitmap includes a transparent pixel outside each visible edge.
# XP's measured 73x21 border therefore needs a 75x23 widget rectangle. The
# two transparent columns between adjacent buttons also count toward the
# measured 8px gap, leaving 6px of layout spacing.
BUTTON_HEIGHT = 23
BUTTON_WIDTH = 75
BUTTON_GAP = 6
PANE_WIDTH = 347 + 2 * BORDER
PANE_HEIGHT = 219 + 2 * BORDER
APPEARANCE_PANE_HEIGHT = 186
ADVANCED_PANE_HEIGHT = 168

# XP's tabs, minus Screen Saver and Settings. Those two are not part of
# xp2plasma and are not going to be, and a permanently dead tab is worse than a
# missing one -- XP's dialog is the model, not the contract.
TAB_TITLES = ["Themes", "Desktop", "Appearance"]

# XP's Position: drop-down, in XP's order. The values are Plasma FillMode
# integers: exactly what theme_file maps Windows' WallpaperStyle onto, and what
# apply.apply_wallpaper writes back.
POSITIONS = (("Center", tf.FILL_CENTER), ("Tile", tf.FILL_TILE), ("Stretch", tf.FILL_STRETCH))

# Which of XP's three positions to show for a fill mode XP has no name for --
# a .theme from a later Windows can ask for Fit or Fill. Both of those fill the
# screen, so Stretch is the honest neighbour; Fit leaves the background visible
# like Center does. Nothing is applied from this, it only decides which row of
# a three-item drop-down is selected.
_NEAREST_POSITION = {tf.FILL_FIT: "Center", tf.FILL_CROP: "Stretch"}

_WALLPAPER_SUFFIXES = (".jpg", ".jpeg", ".png", ".bmp", ".gif")
WALLPAPER_LIST_HEIGHT = 122       # about seven rows, as XP's shows


@dataclass(frozen=True)
class ItemSpec:
    """One entry of the "Item:" list and the scheme fields it drives."""
    color: str | None = None         # item background colour key
    color2: str | None = None        # second colour (title-bar gradient end)
    size: str | None = None          # Scheme attribute the Size spinner writes
    size_range: tuple[int, int] = (12, 60)
    font: str | None = None          # "caption" | "ui" | None
    font_color: str | None = None    # colour key for the font
    note: str = ""


# The classic item list, with the field sets Windows 2000 exposes for each.
ITEMS: dict[str, ItemSpec] = {
    "Desktop": ItemSpec(color="background",
                        note="The flat desktop colour, used when a scheme has no wallpaper."),
    "Inactive Title Bar": ItemSpec(color="inactivetitle", color2="gradientinactivetitle", size="caption",
                                   font="caption", font_color="inactivetitletext",
                                   note="Title bar of unfocused windows. Size sets the caption height."),
    "Active Title Bar": ItemSpec(color="activetitle", color2="gradientactivetitle", size="caption",
                                 font="caption", font_color="titletext",
                                 note="Title bar of the focused window. Color 2 is the right-hand end of the gradient."),
    "Window": ItemSpec(color="window", font="ui", font_color="windowtext",
                       note="Document/client area background; the font colour is the window text."),
    "Menu": ItemSpec(color="menu", size="menu", font="ui", font_color="menutext",
                     note="Menu background. Size is kept for the widget-style phase."),
    "Selected Items": ItemSpec(color="hilight", font="ui", font_color="hilighttext",
                               note="Selection highlight and the selected text."),
    "ToolTip": ItemSpec(color="infowindow", font="ui", font_color="infotext",
                        note="Tooltip background and text."),
    "Button": ItemSpec(color="buttonface", font="ui", font_color="buttontext",
                       note="Push-button face and label."),
    "Message Box": ItemSpec(color="buttonface", font="ui", font_color="buttontext",
                            note="Classic message boxes are painted with the 3D Objects face, so this shares that colour."),
    "3D Objects": ItemSpec(color="buttonface",
                           note="The raised 3D face: window frames, taskbar, button edges."),
    "Application Background": ItemSpec(color="background",
                                       note="Same colour as Desktop in the classic scheme."),
    "Scrollbar": ItemSpec(color="scrollbar", size="scroll", size_range=(8, 40),
                          note="Scrollbar trough. Size is kept for the widget-style phase."),
    "Icon Spacing (Horizontal)": ItemSpec(size="icon_h", size_range=(16, 200),
                                          note="Saved with the scheme; no Plasma equivalent yet."),
    "Icon Spacing (Vertical)": ItemSpec(size="icon_v", size_range=(16, 200),
                                        note="Saved with the scheme; no Plasma equivalent yet."),
    "Caption Buttons": ItemSpec(size="btn_w", size_range=(8, 40),
                                note="Width of the min/max/close buttons; height follows the caption."),
}


def pil_to_pixmap(img) -> QPixmap:
    rgba = img.convert("RGBA")
    qimg = QImage(rgba.tobytes("raw", "RGBA"), rgba.width, rgba.height, QImage.Format_RGBA8888)
    return QPixmap.fromImage(qimg.copy())     # copy: the bytes() buffer is temporary


class ColorButton(QPushButton):
    """The Win2000 colour combo: a swatch with a dropdown arrow."""

    picked = Signal()

    def __init__(self) -> None:
        super().__init__()
        self.setFixedSize(58, 22)
        self.setCursor(Qt.PointingHandCursor)
        self._rgb = (0, 0, 0)
        self.clicked.connect(self._choose)

    def color(self) -> tuple[int, int, int]:
        return self._rgb

    def set_color(self, rgb) -> None:
        self._rgb = tuple(int(v) for v in rgb)
        self.update()

    def paintEvent(self, _event) -> None:
        # Every palette value is held in a local: PySide6 wraps QPalette/QBrush
        # in temporaries, and an inline one can be collected while the painter
        # still references it.
        pal = self.palette()
        face = pal.button().color()
        line = pal.buttonText().color()
        enabled = self.isEnabled()

        p = QPainter(self)
        try:
            full = self.rect().adjusted(0, 0, -1, -1)
            p.fillRect(full, face)
            p.setPen(QPen(line))
            p.drawRect(full)

            if enabled:
                swatch = self.rect().adjusted(3, 3, -19, -3)
                p.fillRect(swatch, QColor(*self._rgb))
                p.setPen(QPen(QColor(0, 0, 0)))
                p.drawRect(swatch)

            p.setPen(QPen(line))
            p.drawLine(QPoint(full.right() - 16, full.top() + 2),
                       QPoint(full.right() - 16, full.bottom() - 2))
            cx, cy = full.right() - 8, full.center().y()
            p.setPen(Qt.NoPen)
            p.setBrush(QBrush(line))
            p.drawPolygon(QPolygon([QPoint(cx - 4, cy - 3), QPoint(cx + 4, cy - 3), QPoint(cx, cy + 3)]))
        finally:
            p.end()

    def _choose(self, _checked: bool = False) -> None:
        chosen = QColorDialog.getColor(QColor(*self._rgb), self, "Color")
        if chosen.isValid():
            self.set_color((chosen.red(), chosen.green(), chosen.blue()))
            self.picked.emit()


def _wallpaper_library() -> list[tuple[str, str]]:
    """(name, path) for every wallpaper the Background list should offer.

    XP listed the files sitting in its wallpaper directories by bare name,
    alphabetically. Ours are the same two roots theme_file resolves a .theme's
    wallpaper against, which keeps a promise worth keeping: anything a theme can
    name is something this list can offer. Names are deduplicated rather than
    paths, because a name is what the user picks by, and the earlier root wins.
    """
    found: dict[str, str] = {}
    for root in paths.wallpaper_roots():
        for entry in sorted(host.listdir(root)):
            stem, suffix = os.path.splitext(entry)
            if suffix.lower() not in _WALLPAPER_SUFFIXES or stem in found:
                continue
            found[stem] = os.path.join(root, entry)
    return sorted(found.items(), key=lambda pair: pair[0].lower())


def _image_file_icon() -> QIcon:
    """XP's generic image-file icon -- the same one on every Background row.

    ``fromTheme`` first, so the rows wear whatever icon theme the session is
    actually using; once a theme has been applied that is ours anyway. Behind
    it, the generated XP theme's own file, for when that theme is installed but
    is not the active one -- during a fresh session, say. Without that fallback
    the rows come out bare, because Qt answers a theme lookup it cannot resolve
    with a null icon rather than with the hicolor placeholder you would expect.
    """
    icon = QIcon.fromTheme("image-x-generic")
    if not icon.isNull():
        return icon
    for size in ("16x16", "32x32", "48x48"):
        path = os.path.join(xp_icons.THEME_DIR, size, "mimetypes", "image-x-generic.png")
        if os.path.isfile(path):
            return QIcon(path)
    return QIcon()


def _same_file(a: str, b: str) -> bool:
    """normcase is a no-op off Windows, so compare the real files rather than
    trusting the path strings to be spelled the same way."""
    try:
        return os.path.samefile(a, b)
    except OSError:
        return os.path.abspath(a).lower() == os.path.abspath(b).lower()


class ElidedLabel(QLabel):
    """Shortens its text instead of forcing the dialog wider. The full string
    stays in the tooltip (and in fullText())."""

    def __init__(self, text: str = "") -> None:
        super().__init__()
        self._full = text
        self.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)

    def setText(self, text: str) -> None:      # noqa: N802 -- Qt's spelling
        self._full = text
        self.setToolTip(text)
        self._elide()

    def fullText(self) -> str:
        return self._full

    def resizeEvent(self, event) -> None:      # noqa: N802
        super().resizeEvent(event)
        self._elide()

    def _elide(self) -> None:
        fm = self.fontMetrics()
        avail = max(30, self.width() - 2)
        super().setText(fm.elidedText(self._full, Qt.ElideRight, avail))


class PreviewPane(QFrame):
    """A sunken, letterboxed preview pane. `draw(size)` returns a PIL image at
    the requested pixel size, so every pane shares the CANVAS aspect ratio."""

    def __init__(self, draw: Callable[[tuple[int, int]], object], height: int = PANE_HEIGHT,
                 parent: QWidget | None = None, *, width: int | None = None) -> None:
        super().__init__(parent)
        self._draw = draw
        # The edge is painted in paintEvent(), not left to QFrame: Kvantum
        # draws nothing at all for a StyledPanel, and Qt's own Box frame is a
        # flat single-colour rectangle -- neither is the classic sunken edge a
        # real XP dialog has.
        self.setFrameShape(QFrame.NoFrame)
        # Pinned to the CANVAS aspect rather than left to stretch with the
        # window: the interior then *is* the preview, with nothing left over
        # for the centring stretches below to split, which is what used to
        # leave a stray row of background between the edge and the image.
        # An explicit width lets each tab use its own measured sample box;
        # the renderer lays out its contents at that size, without scaling art.
        interior_h = height - 2 * BORDER
        canvas_w, canvas_h = preview_gen.CANVAS
        self._fill_pane = width is not None
        self.setFixedSize(width if width is not None else
                          round(interior_h * canvas_w / canvas_h) + 2 * BORDER, height)

        inner = QVBoxLayout(self)
        inner.setContentsMargins(BORDER, BORDER, BORDER, BORDER)
        inner.addStretch(1)
        row = QHBoxLayout()
        row.addStretch(1)
        self.label = QLabel()
        self.label.setAlignment(Qt.AlignCenter)
        row.addWidget(self.label)
        row.addStretch(1)
        inner.addLayout(row)
        inner.addStretch(1)

        # Debounced: a window drag shouldn't re-render on every pixel.
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.setInterval(60)
        self._timer.timeout.connect(self.render)

    def resizeEvent(self, event) -> None:      # noqa: N802
        super().resizeEvent(event)
        self._timer.start()

    def paintEvent(self, event) -> None:       # noqa: N802
        """The classic sunken client edge, measured off a real XP dialog: a
        btnface ring, then Win32's EDGE_SUNKEN -- btnshadow over 3ddkshadow
        down the top and left, btnhighlight over btnface up the bottom and
        right. Painted here because neither Kvantum nor Qt draws it: Kvantum's
        CE_ShapedFrame is a no-op for a StyledPanel, and Qt's own Box frame is
        one flat colour.
        """
        super().paintEvent(event)
        pal = self.palette()
        face, shadow = pal.midlight().color(), pal.mid().color()
        rings = ((face, face),                        # the btnface ring
                 (shadow, pal.light().color()),       # BDR_SUNKENOUTER
                 (pal.dark().color(), face))          # BDR_SUNKENINNER
        painter = QPainter(self)
        for depth, (top_left, bottom_right) in enumerate(rings):
            rect = self.rect().adjusted(depth, depth, -1 - depth, -1 - depth)
            painter.setPen(top_left)
            painter.drawLine(rect.topLeft(), rect.topRight())
            painter.drawLine(rect.topLeft(), rect.bottomLeft())
            painter.setPen(bottom_right)
            painter.drawLine(rect.bottomLeft(), rect.bottomRight())
            painter.drawLine(rect.topRight(), rect.bottomRight())
        painter.end()

    @staticmethod
    def _fit(avail_w: int, avail_h: int) -> tuple[int, int]:
        """Largest CANVAS-aspect rectangle fitting the pane. The preview is
        letterboxed rather than stretched, so its geometry stays predictable."""
        cw, ch = preview_gen.CANVAS
        w = max(1, avail_w)
        h = round(w * ch / cw)
        if h > avail_h:
            h = max(1, avail_h)
            w = round(h * cw / ch)
        return w, h

    def render(self) -> None:
        w, h = self.width() - 2 * BORDER, self.height() - 2 * BORDER
        if not self._fill_pane:
            w, h = self._fit(w, h)
        ratio = self.devicePixelRatioF()
        pixmap = pil_to_pixmap(self._draw((round(w * ratio), round(h * ratio))))
        pixmap.setDevicePixelRatio(ratio)
        self.label.setFixedSize(w, h)
        self.label.setPixmap(pixmap)


class ItemGrid(QWidget):
    """The Windows 2000 "Advanced Appearance" grid: Item / Size / Color /
    Color2 plus the font fields. Owns no scheme -- it edits the one it is given
    in place and reports every change."""

    changed = Signal()

    def __init__(self, scheme, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.scheme = scheme
        self._loading = False

        grid = QGridLayout(self)
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setHorizontalSpacing(10)
        grid.setVerticalSpacing(3)

        def label(text: str, row: int, col: int) -> None:
            grid.addWidget(QLabel(text), row, col)

        label("Item:", 0, 0)
        label("Size:", 0, 1)
        label("Color:", 0, 2)
        label("Color 2:", 0, 3)

        self.item_combo = QComboBox()
        self.item_combo.addItems(list(ITEMS))
        self.item_combo.currentTextChanged.connect(self._on_item_text)
        grid.addWidget(self.item_combo, 1, 0)

        self.size_spin = QSpinBox()
        self.size_spin.valueChanged.connect(self._on_size_changed)
        grid.addWidget(self.size_spin, 1, 1)

        self.color_btn = ColorButton()
        self.color_btn.picked.connect(self._on_color_changed)
        grid.addWidget(self.color_btn, 1, 2)
        self.color2_btn = ColorButton()
        self.color2_btn.picked.connect(self._on_color_changed)
        grid.addWidget(self.color2_btn, 1, 3)

        grid.setRowMinimumHeight(2, 9)             # breathing room before Font

        label("Font:", 3, 0)
        label("Size:", 3, 1)
        label("Color:", 3, 2)

        self.font_combo = QComboBox()
        self.font_combo.activated.connect(self._on_font_picked)
        grid.addWidget(self.font_combo, 4, 0)

        self.fontsize_combo = QComboBox()
        self.fontsize_combo.addItems(FONT_SIZES)
        self.fontsize_combo.currentTextChanged.connect(self._on_font_size_changed)
        grid.addWidget(self.fontsize_combo, 4, 1)

        self.fontcolor_btn = ColorButton()
        self.fontcolor_btn.picked.connect(self._on_font_color_changed)
        grid.addWidget(self.fontcolor_btn, 4, 2)

        style_row = QHBoxLayout()
        style_row.setSpacing(5)
        self.bold_btn = QPushButton("B")
        self.bold_btn.setCheckable(True)
        self.bold_btn.setFixedSize(30, 22)
        self.bold_btn.setToolTip("Bold")
        self.bold_btn.clicked.connect(self._on_font_style_changed)
        self.italic_btn = QPushButton("I")
        self.italic_btn.setCheckable(True)
        self.italic_btn.setFixedSize(30, 22)
        self.italic_btn.setEnabled(False)          # the scheme model carries no italic
        self.italic_btn.setToolTip("Italic is not carried by the scheme model")
        style_row.addWidget(self.bold_btn)
        style_row.addWidget(self.italic_btn)
        style_row.addStretch(1)
        grid.addLayout(style_row, 4, 3)

        grid.setColumnStretch(0, 4)
        grid.setColumnStretch(1, 1)
        grid.setColumnStretch(2, 1)
        grid.setColumnStretch(3, 1)

        self.note = ElidedLabel("")
        self.note.setMinimumWidth(140)
        grid.addWidget(self.note, 5, 0, 1, 4)

        self._refresh()

    def set_scheme(self, scheme) -> None:
        self.scheme = scheme
        self._refresh()

    # --- item editor -------------------------------------------------------

    def _spec(self) -> ItemSpec:
        return ITEMS[self.item_combo.currentText()]

    def _on_item_text(self, text: str) -> None:
        if not self._loading and text in ITEMS:
            self._refresh()

    def _font(self) -> cg.Font:
        return self.scheme.caption_font if self._spec().font == "caption" else self.scheme.ui_font

    def _set_font(self, spec: cg.Font) -> None:
        if self._spec().font == "caption":
            self.scheme.caption_font = spec
        else:
            self.scheme.ui_font = spec

    def _refresh(self) -> None:
        spec = self._spec()
        self._loading = True

        has_size = spec.size is not None
        self.size_spin.setEnabled(has_size)
        if has_size:
            lo, hi = spec.size_range
            self.size_spin.setRange(lo, hi)
            self.size_spin.setValue(int(getattr(self.scheme, spec.size)))

        for btn, key in ((self.color_btn, spec.color), (self.color2_btn, spec.color2)):
            btn.setEnabled(key is not None)
            if key:
                btn.set_color(self.scheme.c(key))

        has_font = spec.font is not None
        self.font_combo.setEnabled(has_font)
        self.fontsize_combo.setEnabled(has_font)
        self.fontcolor_btn.setEnabled(has_font and spec.font_color is not None)
        self.bold_btn.setEnabled(has_font)
        if has_font:
            self._fill_font_combo()
            family, pt, bold = self._font()
            self.font_combo.setCurrentText(family)
            self.fontsize_combo.setCurrentText(str(pt))
            self.bold_btn.setChecked(bool(bold))
            if spec.font_color:
                self.fontcolor_btn.set_color(self.scheme.c(spec.font_color))

        self.note.setText(spec.note)
        self._loading = False

    def _fill_font_combo(self) -> None:
        if self.font_combo.count() == 0:
            from PySide6.QtGui import QFontDatabase
            self.font_combo.addItems(sorted(QFontDatabase.families()))
        current = self._font()[0]
        if self.font_combo.findText(current) < 0:
            self.font_combo.addItem(current)

    def _on_size_changed(self, value: int) -> None:
        if self._loading:
            return
        spec = self._spec()
        if spec.size is None:
            return          # the spinner is disabled for this item, but a
                            # programmatic setValue still lands here
        setattr(self.scheme, spec.size, int(value))
        if spec.size == "btn_w":
            # Caption buttons are btn_w x btn_h with the XP relation btn_h = btn_w - 2.
            self.scheme.btn_h = max(8, int(value) - 2)
        self.changed.emit()

    def _on_color_changed(self) -> None:
        if self._loading:
            return
        spec = self._spec()
        if spec.color:
            self.scheme.colors[spec.color] = self.color_btn.color()
        if spec.color2:
            self.scheme.colors[spec.color2] = self.color2_btn.color()
        self.changed.emit()

    def _on_font_picked(self, _index: int = 0) -> None:
        if self._loading:
            return
        family = self.font_combo.currentText().strip()
        if not family:
            return
        _fam, pt, bold = self._font()
        self._set_font((family, pt, bold))
        self.changed.emit()

    def _on_font_size_changed(self, text: str) -> None:
        if self._loading or not text.isdigit():
            return
        family, _pt, bold = self._font()
        self._set_font((family, int(text), bold))
        self._refresh()
        self.changed.emit()

    def _on_font_color_changed(self) -> None:
        if self._loading:
            return
        key = self._spec().font_color
        if key:
            self.scheme.colors[key] = self.fontcolor_btn.color()
        self.changed.emit()

    def _on_font_style_changed(self) -> None:
        if self._loading:
            return
        family, pt, _bold = self._font()
        self._set_font((family, pt, self.bold_btn.isChecked()))
        self.changed.emit()


class AdvancedDialog(QDialog):
    """XP's Advanced Appearance: the Win2000 grid over a small preview.

    Edits the editor's scheme in place; Cancel puts back a snapshot, so the
    dialog is a real transaction the way the original was."""

    changed = Signal()

    def __init__(self, scheme, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Advanced Appearance")
        self.scheme = scheme
        self._snapshot = copy.deepcopy(scheme)

        root = QVBoxLayout(self)
        root.setContentsMargins(10, 10, 10, 10)
        root.setSpacing(9)

        self.pane = PreviewPane(lambda size: preview_gen.render(self.scheme, size),
                                ADVANCED_PANE_HEIGHT)
        root.addWidget(self.pane, alignment=Qt.AlignHCenter)

        self.grid = ItemGrid(self.scheme)
        self.grid.changed.connect(self._on_changed)
        root.addWidget(self.grid)
        root.addStretch(1)

        row = QHBoxLayout()
        row.addStretch(1)
        ok = QPushButton("OK")
        ok.clicked.connect(self.accept)
        cancel = QPushButton("Cancel")
        cancel.clicked.connect(self.reject)
        for b in (ok, cancel):
            b.setMinimumWidth(72)
            row.addWidget(b)
        root.addLayout(row)

        self.setFixedWidth(WINDOW_WIDTH)
        self.setFixedSize(WINDOW_WIDTH, self.sizeHint().height())

    def _on_changed(self) -> None:
        self.pane.render()
        self.changed.emit()

    def reject(self) -> None:                  # noqa: N802 -- Qt's spelling
        # Restore field by field: the editor holds this same object, so it has
        # to be mutated rather than rebound.
        for key, value in copy.deepcopy(self._snapshot).__dict__.items():
            setattr(self.scheme, key, value)
        self.changed.emit()
        super().reject()


@dataclass
class Job:
    """One unit of work for ApplyWorker."""
    run: Callable[[Callable[[str], None]], None]
    after: Callable[[], None] | None = None
    label: str = "Working ..."
    sync_palette: bool = True


class ApplyWorker(QThread):
    """Runs the generate+install+apply pipeline off the GUI thread."""

    progress = Signal(str)
    done = Signal(bool, str)

    def __init__(self, job: Job) -> None:
        super().__init__()
        self._job = job

    def run(self) -> None:
        try:
            self._job.run(self.progress.emit)
            if self._job.after:
                self._job.after()
            self.done.emit(True, "")
        except Exception as exc:                       # surfaced in the status area
            self.done.emit(False, f"{type(exc).__name__}: {exc}")


@dataclass(frozen=True)
class ThemeRow:
    """One line of the Themes drop-down."""
    label: str
    kind: str                       # "file" | "current" | "add"
    entry: object | None = None     # theme_index.ThemeEntry
    scheme: object | None = None    # classic_gen.Scheme for a saved scheme


class SetupDialog(QDialog):
    """Fetch the assets a fresh machine needs.

    Nothing Microsoft-copyrighted ships with xp2plasma, so a new install can
    only theme anything once it has the user's XP media or a downloaded theme
    pack. Both routes are offered here, each a job run through a
    setup_ui.JobRunner: a step-by-step check-mark list with the raw output
    behind a Details toggle, real Cancel (the jobs themselves roll back
    whatever they had done), and a byte-progress pop-up for downloads.
    """

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle("Set Up XP Assets")
        self.setMinimumWidth(660)
        self.jobs = setup_ui.JobRunner(self)

        layout = QVBoxLayout(self)
        intro = QLabel(
            "xp2plasma doesn't include the Windows XP artwork, styles or fonts -- they're "
            "Microsoft's. Point it at install media you already have, or fetch the theme "
            "packs from archive.org.")
        intro.setWordWrap(True)
        layout.addWidget(intro)

        self.checklist = QLabel(self._checklist_text())
        self.checklist.setFont(QFont("Monospace", 9))
        layout.addWidget(self.checklist)

        media_row = QHBoxLayout()
        self.iso_btn = QPushButton("Use an XP ISO ...")
        self.iso_btn.clicked.connect(lambda: self._pick_media(iso=True))
        self.dir_btn = QPushButton("Use an extracted folder ...")
        self.dir_btn.clicked.connect(lambda: self._pick_media(iso=False))
        self.icons_btn = QPushButton("Install XP icons")
        self.icons_btn.setToolTip("Extract and apply the original icons from your imported XP media.")
        self.icons_btn.clicked.connect(self._install_icons)
        for button in (self.iso_btn, self.dir_btn, self.icons_btn):
            media_row.addWidget(button)
        media_row.addStretch(1)
        layout.addLayout(media_row)

        pack_row = QHBoxLayout()
        self.download_btn = QPushButton("Download the theme packs")
        self.download_btn.clicked.connect(self._download)
        self.iso_dl_btn = QPushButton("Download XP install media (589 MB)")
        self.iso_dl_btn.setToolTip("From archive.org; only the fonts, the Luna style, the "
                                   "wallpapers, icon resources and the Start flag are kept.")
        self.iso_dl_btn.clicked.connect(self._download_iso)
        self.widgets_check = QCheckBox("Also install the widget style")
        self.widgets_check.setChecked(True)
        for widget in (self.download_btn, self.iso_dl_btn, self.widgets_check):
            pack_row.addWidget(widget)
        pack_row.addStretch(1)
        layout.addLayout(pack_row)

        self.pane = setup_ui.StepsPane()
        layout.addWidget(self.pane, 1)

        footer = QHBoxLayout()
        footer.addStretch(1)
        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.setEnabled(False)
        self.cancel_btn.clicked.connect(self._cancel_clicked)
        self.close_btn = QPushButton("Close")
        self.close_btn.clicked.connect(self.accept)
        footer.addWidget(self.cancel_btn)
        footer.addWidget(self.close_btn)
        layout.addLayout(footer)

    def _checklist_text(self) -> str:
        return "\n".join(f"{'ok  ' if c.ok else 'MISS'}  {c.name:<28} {c.purpose}"
                         for c in setup_assets.preflight())

    def _busy(self, busy: bool) -> None:
        for widget in (self.iso_btn, self.dir_btn, self.download_btn, self.iso_dl_btn,
                       self.icons_btn, self.widgets_check, self.close_btn):
            widget.setEnabled(not busy)
        self.cancel_btn.setEnabled(busy and not self.jobs.cancelling)

    def _cancel_clicked(self) -> None:
        if self.jobs.request_cancel():
            self.cancel_btn.setEnabled(False)

    def reject(self) -> None:
        # Closing mid-job would tear down a still-running worker thread.
        if self.jobs.busy:
            self._cancel_clicked()
        else:
            super().reject()

    def _start(self, fn, *, byte_progress: bool = False, dialog_title: str | None = None) -> None:
        if self.jobs.busy:
            return
        self._busy(True)
        self.jobs.start(fn, self.pane, byte_progress=byte_progress, dialog_title=dialog_title,
                        on_finished=self._finished)

    def _finished(self, _status: str, _message: str) -> None:
        self._busy(False)
        self.checklist.setText(self._checklist_text())

    def _pick_media(self, iso: bool) -> None:
        if iso:
            path, _filt = QFileDialog.getOpenFileName(
                self, "Select your Windows XP ISO", paths.REAL_HOME,
                "Disc images (*.iso *.ISO *.img);;All files (*)")
        else:
            path = QFileDialog.getExistingDirectory(
                self, "Select the extracted I386 folder (or its parent)", paths.REAL_HOME)
        if not path:
            return
        self._start(lambda progress, _on_bytes: setup_assets.import_media(path, progress))

    def _download_iso(self) -> None:
        self._start(lambda progress, on_bytes: setup_assets.download_pack(
            setup_assets.ISO_DOWNLOAD, progress, on_bytes),
            byte_progress=True, dialog_title="Downloading XP install media")

    def _install_icons(self) -> None:
        import xp_icons
        self._start(lambda progress, _on_bytes: xp_icons.setup(progress))

    def _download(self) -> None:
        pack = setup_assets.DOWNLOADS[0]
        build_widgets = self.widgets_check.isChecked()

        def work(progress, on_bytes) -> None:
            setup_assets.download_pack(pack, progress, on_bytes)
            if build_widgets:
                host.check_cancelled()
                setup_assets.install_widget_style(progress)

        self._start(work, byte_progress=True, dialog_title=f"Downloading {pack.label}")


class AppearanceEditor(QDialog):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("Display Properties")
        self.setFixedWidth(WINDOW_WIDTH)
        self.setSizePolicy(QSizePolicy.Fixed, QSizePolicy.Fixed)

        self.scheme = self._initial_scheme()     # the classic scheme Advanced edits
        self._loading = False
        self._edited = False                     # scheme differs from its named source
        self._dirty = False                      # something to apply that isn't applied yet
        self._busy = False
        self._shift_held = False                 # temporary force-Apply override
        self._displayed_theme_row: ThemeRow | None = None
        self._refresh_library_on_activate = False
        self._worker: ApplyWorker | None = None
        self._dialog: setup_ui.ProgressDialog | None = None
        self._cancel_event: threading.Event | None = None
        self._pending_close = False
        self._applied_ui_font: tuple | None = None

        self._applied = applied_state.load()
        try:
            self._desktop = theme_presets.current_desktop()
        except (OSError, ValueError):
            self._desktop = None
        self._styles = tx.list_visual_styles()
        self._rows: list[ThemeRow] = []
        self._theme_info: tx.ThemeInfo | None = None
        self._theme_info_key: str | None = None
        self._style_info: tx.ThemeInfo | None = None
        self._style_info_key: tuple | None = None
        # The Desktop tab's own state. `self._desktop` is what the session is
        # showing; these are what the tab has been told to show instead, which
        # only becomes real on Apply.
        self._wallpaper_rows: list[tuple[str, str]] = []
        self._wallpaper_choice: str | None = None
        self._wallpaper_cache = None
        self._wallpaper_cache_key: str | None = None

        self._build_ui()
        self._init_desktop()
        self._reload_themes()
        self._init_appearance()
        self._init_theme_selection()
        self._sync_palette()
        # Button bounds include Luna's transparent outer rim; combo bounds
        # already coincide with their visible edges.
        for control in self.findChildren(QPushButton):
            control.setFixedHeight(BUTTON_HEIGHT)
        for control in self.findChildren(QComboBox):
            control.setFixedHeight(CONTROL_HEIGHT)
        # Startup shows what's already applied, so there is nothing pending.
        self._set_dirty(False)
        # XP fixes the client area, excluding KWin's title bar and frame.
        self.setFixedSize(WINDOW_WIDTH, WINDOW_HEIGHT)
        # Both panes are drawn up front: switching tabs shouldn't flash an
        # empty frame, and a render is cheap next to the layout pass.
        self.themes_pane.render()
        self.appearance_pane.render()
        # Watch child controls too: Shift should work while a combo or button
        # has keyboard focus, without consuming its normal key handling.
        QApplication.instance().installEventFilter(self)

    def eventFilter(self, watched, event) -> bool:
        kind = event.type()
        if watched is self and kind in (QEvent.WindowDeactivate, QEvent.Hide):
            self._shift_held = False
            self._update_apply_enabled()
        elif watched is self and kind == QEvent.WindowActivate:
            self._shift_held = bool(QApplication.keyboardModifiers() & Qt.ShiftModifier)
            self._update_apply_enabled()
            if self._refresh_library_on_activate and not self._busy:
                self._refresh_library_on_activate = False
                self._reload_themes()
                old_style = self._style()
                self._styles = tx.list_visual_styles()
                self.style_combo.clear()
                self.style_combo.addItems([s.label for s in self._styles])
                self.style_combo.setCurrentIndex(next(
                    (i for i, s in enumerate(self._styles) if s.path == old_style.path), 0))
        elif (kind in (QEvent.KeyPress, QEvent.KeyRelease)
              and isinstance(watched, QWidget) and watched.window() is self
              and event.key() == Qt.Key_Shift and not event.isAutoRepeat()):
            self._shift_held = kind == QEvent.KeyPress
            self._update_apply_enabled()
        return super().eventFilter(watched, event)

    @staticmethod
    def _initial_scheme():
        """Reopen on whatever is actually on the desktop right now."""
        return cg.load_applied() or cg.load_scheme(scheme_name="Windows Standard")

    # --- construction ------------------------------------------------------

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(6, LAYOUT_GAP, LAYOUT_GAP, LAYOUT_GAP)
        root.setSpacing(LAYOUT_GAP)

        self.tabs = QTabWidget()
        # Every tab in TAB_TITLES is live. The two that were not are gone
        # rather than disabled, so there is nothing left to gate here.
        pages = {"Themes": self._build_themes_tab(),
                 "Desktop": self._build_desktop_tab(),
                 "Appearance": self._build_appearance_tab()}
        for title in TAB_TITLES:
            self.tabs.addTab(pages[title], title)
        self.tabs.setCurrentIndex(TAB_TITLES.index("Themes"))
        self.tabs.currentChanged.connect(lambda _i: self._render_current())
        root.addWidget(self.tabs, 1)
        root.addLayout(self._build_button_row())

    def _build_themes_tab(self) -> QWidget:
        """XP's order: the blurb, the Theme row, then "Sample:" over the preview."""
        page = QWidget()
        content = QVBoxLayout(page)
        content.setContentsMargins(LAYOUT_GAP, LAYOUT_GAP, LAYOUT_GAP, LAYOUT_GAP)
        content.setSpacing(LAYOUT_GAP)

        blurb = QLabel("A theme is a background plus a set of sounds, icons, and other elements "
                       "to help you personalize your computer with one click.")
        blurb.setWordWrap(True)
        content.addWidget(blurb)
        theme_group = QVBoxLayout()
        theme_group.setSpacing(4)
        theme_group.addWidget(QLabel("Theme:"))
        row = QHBoxLayout()
        row.setSpacing(8)
        self.theme_combo = QComboBox()
        # A long name from an added theme must not widen XP's fixed dialog.
        self.theme_combo.setSizeAdjustPolicy(QComboBox.AdjustToMinimumContentsLengthWithIcon)
        self.theme_combo.setMinimumContentsLength(1)
        self.theme_combo.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Fixed)
        self.theme_combo.activated.connect(self._on_theme_picked)
        row.addWidget(self.theme_combo, 1)
        self.save_btn = QPushButton("Save As...")
        self.save_btn.clicked.connect(self._save_as)
        row.addWidget(self.save_btn)
        self.delete_btn = QPushButton("Delete")
        self.delete_btn.clicked.connect(self._delete_theme)
        row.addWidget(self.delete_btn)
        theme_group.addLayout(row)
        content.addLayout(theme_group)

        sample_group = QVBoxLayout()
        sample_group.setSpacing(4)
        sample_group.addWidget(QLabel("Sample:"))
        self.themes_pane = PreviewPane(lambda size: preview_gen.render_desktop(self._theme_info, size),
                                       width=PANE_WIDTH)
        sample_group.addWidget(self.themes_pane, alignment=Qt.AlignHCenter)
        content.addLayout(sample_group)

        self.theme_desc = ElidedLabel("")
        # The real dialog has nothing between the Sample box and the buttons,
        # and this line is a good chunk of what pushes the window past its
        # 423px height. Still built and still updated, just not shown --
        # dropping the hide() puts it back.
        self.theme_desc.hide()
        content.addWidget(self.theme_desc)
        content.addStretch(1)
        return page

    def _build_desktop_tab(self) -> QWidget:
        """XP's Desktop tab: the monitor sample, the Background list, and the
        Browse/Position/Color column down its right-hand side.

        This is the one tab that gets XP's monitor -- Themes and Appearance
        show a *window*, and Screen Saver and Settings, the only others that
        use a monitor, are not implemented.
        """
        page = QWidget()
        content = QVBoxLayout(page)
        content.setContentsMargins(LAYOUT_GAP, LAYOUT_GAP, LAYOUT_GAP, LAYOUT_GAP)
        content.setSpacing(LAYOUT_GAP)

        # The preview is the Desktop tab's own; it draws the wallpaper into the
        # monitor's screen at the chosen position, using themeui.dll's bitmap.
        self.desktop_pane = PreviewPane(self._draw_desktop, width=PANE_WIDTH)
        content.addWidget(self.desktop_pane, alignment=Qt.AlignHCenter)

        grid = QGridLayout()
        grid.setHorizontalSpacing(LAYOUT_GAP)
        grid.setVerticalSpacing(4)

        self.wallpaper_list = QListWidget()
        # XP puts the same small image-file icon on every row rather than a
        # thumbnail of the wallpaper itself.
        self.wallpaper_list.setIconSize(QSize(16, 16))
        self.wallpaper_list.setFixedHeight(WALLPAPER_LIST_HEIGHT)
        self.wallpaper_list.currentRowChanged.connect(self._on_wallpaper_picked)
        grid.addWidget(QLabel("Background:"), 0, 0, 1, 2)
        grid.addWidget(self.wallpaper_list, 1, 0)

        column = QVBoxLayout()
        column.setSpacing(4)
        self.browse_btn = QPushButton("Browse...")
        self.browse_btn.clicked.connect(self._browse_wallpaper)
        column.addWidget(self.browse_btn)
        column.addSpacing(8)
        column.addWidget(QLabel("Position:"))
        self.position_combo = QComboBox()
        self.position_combo.addItems([name for name, _fill in POSITIONS])
        self.position_combo.activated.connect(self._on_position_changed)
        column.addWidget(self.position_combo)
        column.addSpacing(8)
        # Color is the desktop colour *behind* the wallpaper, which is what you
        # see around a centred or tiled image. ColorButton is the same swatch
        # widget the Advanced grid uses.
        column.addWidget(QLabel("Color:"))
        self.desktop_color = ColorButton()
        self.desktop_color.picked.connect(self._on_desktop_color)
        column.addWidget(self.desktop_color)
        column.addStretch(1)
        grid.addLayout(column, 1, 1)
        grid.setColumnStretch(0, 1)

        content.addLayout(grid)

        self.customize_btn = QPushButton("Customize Desktop...")
        self.customize_btn.setEnabled(False)
        self.customize_btn.setToolTip(
            "Desktop Items (My Documents, My Computer, My Network Places) are not "
            "part of xp2plasma.")
        content.addWidget(self.customize_btn, alignment=Qt.AlignLeft)
        content.addStretch(1)
        return page

    def _init_desktop(self) -> None:
        """Seed the Desktop tab from what is actually on the desktop now."""
        desktop = self._desktop or {}
        image = desktop.get("image")
        rows = _wallpaper_library()
        if image and not any(path == image for _name, path in rows):
            # A browsed image, or one a .theme points at from outside the usual
            # roots. It is what is on screen, so it gets a row -- otherwise the
            # selection would point at a line that isn't in the list.
            rows.append((os.path.splitext(os.path.basename(image))[0], image))
            rows.sort(key=lambda pair: pair[0].lower())
        self._wallpaper_rows = rows
        self._wallpaper_choice = image
        self._wallpaper_cache = None
        self._wallpaper_cache_key = None

        self._loading = True
        try:
            icon = self._wallpaper_icon = _image_file_icon()
            self.wallpaper_list.clear()
            self.wallpaper_list.addItem(QListWidgetItem("(None)"))
            selected = 0
            for index, (name, path) in enumerate(rows, start=1):
                item = QListWidgetItem(icon, name)
                item.setData(Qt.UserRole, path)
                self.wallpaper_list.addItem(item)
                if image and path == image:
                    selected = index
            self.wallpaper_list.setCurrentRow(selected)

            fill = desktop.get("fill")
            by_fill = {value: name for name, value in POSITIONS}
            self.position_combo.setCurrentText(
                by_fill.get(fill) or _NEAREST_POSITION.get(fill) or "Stretch")
            self.desktop_color.set_color(desktop.get("color")
                                         or cg.scheme_wallpaper_color(self.scheme))
        finally:
            self._loading = False
        self.desktop_pane.render()

    def _wallpaper_path(self) -> str | None:
        """The path of the selected background, or None for "(None)"."""
        row = self.wallpaper_list.currentRow()
        return self.wallpaper_list.item(row).data(Qt.UserRole) if row > 0 else None

    def _fill_mode(self) -> int:
        return dict(POSITIONS).get(self.position_combo.currentText(), tf.FILL_STRETCH)

    def _desktop_changed(self) -> None:
        self._set_dirty(True)
        self.desktop_pane.render()

    def _on_wallpaper_picked(self, row: int) -> None:
        if self._loading or row < 0:
            return
        self._wallpaper_choice = self._wallpaper_path()
        self._desktop_changed()

    def _on_position_changed(self, _index: int) -> None:
        if not self._loading:
            self._desktop_changed()

    def _on_desktop_color(self) -> None:
        if not self._loading:
            self._desktop_changed()

    def _browse_wallpaper(self) -> None:
        path, _chosen_filter = QFileDialog.getOpenFileName(
            self, "Browse", paths.REAL_HOME,
            "Images (*.jpg *.jpeg *.png *.bmp *.gif);;All files (*)")
        if not path:
            return
        for row in range(1, self.wallpaper_list.count()):
            if self.wallpaper_list.item(row).data(Qt.UserRole) == path:
                self.wallpaper_list.setCurrentRow(row)
                return
        item = QListWidgetItem(self._wallpaper_icon,
                               os.path.splitext(os.path.basename(path))[0])
        item.setData(Qt.UserRole, path)
        self.wallpaper_list.addItem(item)
        self.wallpaper_list.setCurrentItem(item)

    def _draw_desktop(self, size):
        """The Desktop sample: the chosen wallpaper, on XP's monitor."""
        image = None
        if self._wallpaper_choice:
            if self._wallpaper_cache_key != self._wallpaper_choice:
                self._wallpaper_cache = tx._load_wallpaper(self._wallpaper_choice)
                self._wallpaper_cache_key = self._wallpaper_choice
            image = self._wallpaper_cache
        # The monitor sits on the dialog's own face, as it does in XP. Read
        # through a local: an inline palette temporary can be collected while
        # the renderer still holds it.
        face = self.palette().button().color()
        return preview_gen.render_wallpaper(
            image, self._fill_mode(), self.desktop_color.color(), size,
            face=(face.red(), face.green(), face.blue()))

    def _build_appearance_tab(self) -> QWidget:
        """XP's Appearance tab: the sample, then three labelled drop-downs with
        Effects... beside "Color scheme" and Advanced beside "Font size"."""
        page = QWidget()
        content = QVBoxLayout(page)
        content.setContentsMargins(LAYOUT_GAP, LAYOUT_GAP, LAYOUT_GAP, LAYOUT_GAP)
        content.setSpacing(LAYOUT_GAP)

        self.appearance_pane = PreviewPane(self._draw_appearance, APPEARANCE_PANE_HEIGHT,
                                           width=PANE_WIDTH)
        content.addWidget(self.appearance_pane, alignment=Qt.AlignHCenter)

        grid = QGridLayout()
        grid.setHorizontalSpacing(LAYOUT_GAP)
        grid.setVerticalSpacing(4)

        self.style_combo = QComboBox()
        self.style_combo.addItems([s.label for s in self._styles])
        self.style_combo.activated.connect(self._on_style_changed)
        grid.addWidget(QLabel("Windows and buttons:"), 0, 0)
        grid.addWidget(self.style_combo, 1, 0)

        self.colour_combo = QComboBox()
        self.colour_combo.activated.connect(self._on_colour_changed)
        grid.addWidget(QLabel("Color scheme:"), 2, 0)
        grid.addWidget(self.colour_combo, 3, 0)
        self.effects_btn = QPushButton("Effects...")
        self.effects_btn.setEnabled(False)
        self.effects_btn.setToolTip("Visual effects are not part of xp2plasma.")
        grid.addWidget(self.effects_btn, 3, 2)

        self.fontsize_combo = QComboBox()
        self.fontsize_combo.activated.connect(self._on_fontsize_changed)
        grid.addWidget(QLabel("Font size:"), 4, 0)
        grid.addWidget(self.fontsize_combo, 5, 0)
        self.advanced_btn = QPushButton("Advanced")
        self.advanced_btn.clicked.connect(self._open_advanced)
        grid.addWidget(self.advanced_btn, 5, 2)

        for b in (self.effects_btn, self.advanced_btn):
            b.setMinimumWidth(90)
        grid.setColumnStretch(0, 5)
        grid.setColumnMinimumWidth(1, 40)          # XP leaves a gap before the buttons
        grid.setColumnStretch(1, 2)
        content.addLayout(grid)
        content.addStretch(1)
        return page

    def _draw_appearance(self, size):
        """The Appearance sample. Windows Classic uses classic_gen's own
        composition (bevels, menu strip, list box), which is what Apply
        installs; a visual style is drawn from its real art."""
        if self._style().is_classic:
            return preview_gen.render(self.scheme, size)
        return preview_gen.render_style(self._appearance_info(), size)

    def _build_button_row(self) -> QHBoxLayout:
        row = QHBoxLayout()
        # Brief one-off notes ("Saved to ...", "Deleted ...") sit here, always
        # present so nothing reflows. Anything long-running gets a
        # ProgressDialog instead -- see _apply().
        self.status = ElidedLabel("")
        self.status.setMinimumWidth(0)
        row.addWidget(self.status, 1)

        # Commented out for now to make room for a narrower, XP-accurate window
        # width (real XP's dialog had neither) -- both still default on below,
        # via self._widgets_enabled/self._taskbar_enabled, so Apply behaves the
        # same as when these were checked; re-add the widgets to bring the
        # toggles back.
        # self.widgets_check = QCheckBox("Widgets")
        # self.widgets_check.setChecked(True)
        # self.widgets_check.setToolTip(
        #     "Also style buttons, scroll bars, tabs and check boxes inside windows:\n"
        #     "a Kvantum theme cut from the visual style, or Qt's Windows style for Classic.\n"
        #     "Applies to Qt/KDE apps started after Apply.")
        # row.addWidget(self.widgets_check)
        #
        # self.taskbar_check = QCheckBox("Taskbar")
        # self.taskbar_check.setChecked(True)
        # self.taskbar_check.setToolTip(
        #     "Also regenerate the taskbar + Start button.\n"
        #     "This restarts plasmashell (~5s), so uncheck it while tuning colours.")
        # row.addWidget(self.taskbar_check)

        self.ok_btn = QPushButton("OK")
        self.ok_btn.setDefault(True)
        self.ok_btn.clicked.connect(self._ok)
        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.clicked.connect(self.close)
        self.apply_btn = QPushButton("Apply")
        self.apply_btn.setToolTip("Hold Shift to apply again even if nothing has changed.")
        self.apply_btn.clicked.connect(lambda: self._apply())
        row.setSpacing(BUTTON_GAP)
        for b in (self.ok_btn, self.cancel_btn, self.apply_btn):
            # Keep the full bitmap rectangle fixed; the elided status label
            # takes whatever space remains in the row.
            b.setFixedWidth(BUTTON_WIDTH)
            row.addWidget(b)
        return row

    # --- Themes tab --------------------------------------------------------

    def _reload_themes(self) -> None:
        """Complete presets only; bare styles and saved colors live in Appearance."""
        entries = tx.list_themes()
        counts: dict[str, int] = {}
        for e in entries:
            counts[e.label.lower()] = counts.get(e.label.lower(), 0) + 1
        self._rows = [ThemeRow("Current Theme (modified)", "current")]
        for e in entries:
            label = e.label
            if counts[e.label.lower()] > 1:
                # Same name twice (two "luna" files): tell them apart by folder.
                label = f"{e.label} ({os.path.basename(os.path.dirname(e.theme_file or e.path))})"
            self._rows.append(ThemeRow(label, "file", entry=e))
        self._rows.append(ThemeRow("Add more...", "add"))
        self._fill_theme_combo()

    def _fill_theme_combo(self) -> None:
        current = self._current_row()
        self._loading = True
        self.theme_combo.clear()
        for row in self._rows:
            self.theme_combo.addItem(row.label, row)
        self._loading = False
        if current is not None:
            self._select_row(current)

    def _select_row(self, row: ThemeRow) -> None:
        # By identity, not text: two files can share a name ("luna" twice).
        for i in range(self.theme_combo.count()):
            candidate = self.theme_combo.itemData(i)
            if candidate is row or (candidate.kind == row.kind and candidate.label == row.label
                    and getattr(candidate.entry, "path", None) == getattr(row.entry, "path", None)
                    and getattr(candidate.entry, "theme_file", None) == getattr(row.entry, "theme_file", None)):
                self.theme_combo.setCurrentIndex(i)
                return

    def _current_row(self) -> ThemeRow | None:
        data = self.theme_combo.itemData(self.theme_combo.currentIndex())
        return data if isinstance(data, ThemeRow) else None

    def _on_theme_picked(self, _index: int = 0) -> None:
        if self._loading:
            return
        row = self._current_row()
        if row is None:
            return
        if row.kind == "add":
            self._add_more_themes()
            return
        if row.kind == "current":
            self._show_current_theme()
            return
        self._set_dirty(True)       # a different theme is now waiting on Apply
        self._show_theme(row)

    def _add_more_themes(self) -> None:
        # Folder action, not a pending theme selection. Keep the current sample
        # and dirty/Apply state, then discover additions when the user returns.
        if self._displayed_theme_row is not None:
            self._select_row(self._displayed_theme_row)
        else:
            self.theme_combo.setCurrentIndex(-1)
        try:
            folder = paths.ensure(paths.THEMES)
            host.run(["xdg-open", folder], check=True, capture=True)
        except (OSError, RuntimeError) as exc:
            self._set_status(f"Couldn't open the themes folder: {exc}")
            return
        self._refresh_library_on_activate = True
        self._set_status("Add extracted .theme/.msstyles files here, then return to this window.")

    def _setup_assets(self) -> None:
        """Open the asset setup dialog, then pick up anything it brought in."""
        SetupDialog(self).exec()
        self._reload_themes()

    def _browse(self) -> None:
        path, _filt = QFileDialog.getOpenFileName(
            self, "Browse Themes", paths.browse_root(),
            "Windows theme presets (*.theme)")
        if not path:
            self._select_row(self._current_row() or self._rows[0])
            return
        entry = tx.entry_from_path(path)
        if not entry.theme_file:
            self._set_status("Visual styles belong under Appearance, not Themes.")
            return
        row = ThemeRow(entry.label, "file", entry=entry)
        if not any(r.entry and r.entry.path == entry.path and r.entry.theme_file == entry.theme_file
                   for r in self._rows):
            self._rows.insert(0, row)
        self._fill_theme_combo()
        self._select_row(row)
        self._show_theme(row)

    def _applied_row(self) -> ThemeRow | None:
        """The row for the theme the last apply recorded (from here or the
        command line), matched by file rather than by name."""
        state = self._applied
        if state.get("theme_file"):
            row = next((r for r in self._rows if r.kind == "file" and r.entry.theme_file
                        and _same_file(r.entry.theme_file, state["theme_file"])), None)
            if row and state.get("kind") == "style" and state.get("ini"):
                style = tx.visual_style_from(row.entry.style_path)
                if tx.bundle_variant(row.entry, style) != state["ini"]:
                    return None
            return row
        return None

    def _init_theme_selection(self) -> None:
        """Point the Themes tab at whatever is applied, the way XP opens on the
        active theme. When nothing on disk matches, say so rather than naming a
        theme that isn't the one on screen."""
        row = self._applied_row() or next(
            (r for r in self._rows if r.kind in ("file", "user") and r.label == self.scheme.name), None)
        if row is not None:
            self._select_row(row)
            # _init_appearance has already restored the *applied* variant; a
            # bundle names its own, which would otherwise overwrite it.
            self._show_theme(row, sync_appearance=not self._applied.get("ini"))
            return
        self._show_current_theme()

    def _show_current_theme(self) -> None:
        row = next(r for r in self._rows if r.kind == "current")
        self._select_row(row)
        self._displayed_theme_row = row
        self._theme_info = copy.copy(self._appearance_info())
        if self._desktop:
            self._theme_info.desktop = self._desktop["color"]
            self._theme_info.wallpaper = tx._load_wallpaper(self._desktop["image"])
        self._theme_info_key = "current"
        self.theme_desc.setText("Current appearance with the existing desktop background; not a saved preset.")
        self._render("themes")

    def _show_theme(self, row: ThemeRow, sync_appearance: bool = True) -> None:
        self._displayed_theme_row = row
        key = f"{row.kind}:{row.label}:{getattr(row.entry, 'path', '')}"
        if self._theme_info_key != key:
            try:
                info = (tx.info_for_entry(row.entry) if row.kind == "file"
                        else tx.info_from_scheme(row.scheme, row.label))
            except Exception as exc:
                self._set_status(f"Can't preview {row.label}: {exc}")
                return
            self._theme_info, self._theme_info_key = info, key
            self.theme_desc.setText(self._describe(row))
            if sync_appearance:
                self._sync_appearance(row)
        self._render("themes")

    def _describe(self, row: ThemeRow) -> str:
        if row.kind == "user":
            return f"{row.label} — a scheme you saved on this machine."
        entry = row.entry
        bits = []
        if entry.theme_file:
            bits.append(os.path.basename(entry.theme_file))
        if entry.path and os.path.exists(entry.path):
            bits.append(os.path.basename(entry.path))
        style = tf.theme_msstyles_path(entry.theme_file) if entry.theme_file else None
        if style and os.path.basename(style) not in bits:
            bits.append(os.path.basename(style))
        parts = [row.label + " — " + " · ".join(dict.fromkeys(bits))] if bits else [row.label]
        if not entry.supported:
            parts.append("NOT SUPPORTED: this .msstyles stores its layout as binary "
                         "VSTYLE records (Vista/Longhorn era), not the plaintext INI "
                         "this converter reads. It is listed so you can tell it apart "
                         "from a broken file, but it can't be applied.")
            return "\n".join(parts)
        wp = tf.theme_wallpaper_path(entry.theme_file) if entry.theme_file else None
        if wp and os.path.exists(wp):
            parts.append(f"Wallpaper: {os.path.basename(wp)}")
        elif wp:
            parts.append("Wallpaper: not on disk (the bundle points at a file this "
                         "collection doesn't include)")
        else:
            parts.append("No wallpaper in the bundle; the desktop colour is used.")
        return " — ".join(parts)

    def _save_as(self) -> None:
        row = self._current_row()
        suggested = row.label if row and row.kind == "file" else self._style().label
        name, ok = QInputDialog.getText(self, "Save Theme", "Save this theme as:", text=suggested)
        if not ok or not name.strip():
            return
        base = row.entry.theme_file if row and row.kind == "file" else None
        try:
            desktop = ({"image": tf.theme_wallpaper_path(base), "color": self._theme_info.desktop,
                        "fill": tf.theme_wallpaper_fill_mode(base) or 0} if base
                       else theme_presets.current_desktop())
            path = theme_presets.save(name.strip(), self._style(), self._colour_key(),
                                      self._size_key(), self.scheme, desktop, base)
        except (OSError, ValueError) as exc:
            self._set_status(f"Couldn't save theme: {exc}")
            QMessageBox.warning(self, "Save Theme", str(exc))
            return
        self._reload_themes()
        row = next(r for r in self._rows if r.kind == "file" and _same_file(r.entry.theme_file, path))
        self._select_row(row)
        self._show_theme(row, sync_appearance=False)
        self._set_status(f"Saved complete preset to {path}")

    def _delete_theme(self) -> None:
        row = self._current_row()
        if row is None or row.kind != "file" or not theme_presets.owned(row.entry.theme_file):
            QMessageBox.information(
                self, "Delete",
                "Only themes saved here can be deleted; stock themes and "
                "files in your theme collection are left alone.")
            return
        if QMessageBox.question(self, "Delete", f"Delete the saved theme '{row.label}'?") != QMessageBox.Yes:
            return
        path = row.entry.theme_file
        backup = path + ".deleted"
        i = 1
        while os.path.exists(backup):
            backup = path + f".deleted-{i}"
            i += 1
        os.rename(path, backup)
        self._reload_themes()
        self._show_current_theme()
        self._set_status(f"Removed '{row.label}' from the list; recoverable at {backup}.")

    # --- Appearance tab ----------------------------------------------------

    def _init_appearance(self) -> None:
        state = self._applied
        if state.get("kind") == "classic" and state.get("scheme") != self.scheme.name:
            try:
                self.scheme = cg.load_scheme(scheme_name=state["scheme"])
            except (KeyError, ValueError):
                pass
        if state.get("kind") == "style" and state.get("msstyles"):
            for i, style in enumerate(self._styles):
                if style.path and _same_file(style.path, state["msstyles"]):
                    self.style_combo.setCurrentIndex(i)
                    colour = size_key = None
                    for size, token in style.variants:
                        if style.resolve(token, size) == state.get("ini"):
                            colour, size_key = token, size
                            break
                    self._fill_colour_combo(colour)
                    self._fill_size_combo(size_key)
                    self.advanced_btn.setEnabled(False)
                    return
        bases = {c.key for c in self._styles[0].colours()}
        base, size_key = tx.classic_split(self.scheme.name)
        if base not in bases:
            # The applied scheme is an edited one with no name of its own, so
            # the drop-downs have nothing to point at. Start from the default
            # rather than leaving them on a scheme the preview isn't drawing.
            base = self._styles[0].colours()[0].key
            size_key = "NORMAL"
            self.scheme = cg.load_scheme(scheme_name=base)
        self.style_combo.setCurrentIndex(0)         # Windows Classic style
        self._fill_colour_combo(base)
        self._fill_size_combo(size_key)

    def _style(self) -> tx.VisualStyle:
        return self._styles[max(0, self.style_combo.currentIndex())]

    def _colour_key(self) -> str:
        return self.colour_combo.currentData() or ""

    def _size_key(self) -> str:
        return self.fontsize_combo.currentData() or "NORMAL"

    def _fill_colour_combo(self, want: str | None = None) -> None:
        self._loading = True
        self.colour_combo.clear()
        for opt in self._style().colours():
            self.colour_combo.addItem(opt.label, opt.key)
        i = self.colour_combo.findData(want) if want else -1
        self.colour_combo.setCurrentIndex(max(0, i))
        self._loading = False

    def _fill_size_combo(self, want: str | None = None) -> None:
        self._loading = True
        self.fontsize_combo.clear()
        for opt in self._style().sizes(self._colour_key()):
            self.fontsize_combo.addItem(opt.label, opt.key)
        i = self.fontsize_combo.findData(want) if want else -1
        if i < 0:
            i = self.fontsize_combo.findData("NORMAL")
        self.fontsize_combo.setCurrentIndex(max(0, i))
        self._loading = False

    def _refresh_appearance(self) -> None:
        self.colour_combo.clear()
        self.fontsize_combo.clear()
        self._init_appearance()

    def _on_style_changed(self, _index: int) -> None:
        if self._loading:
            return
        self._fill_colour_combo()
        self._fill_size_combo()
        self._on_appearance_changed()

    def _on_colour_changed(self, _index: int) -> None:
        if self._loading:
            return
        self._fill_size_combo()
        self._on_appearance_changed()

    def _on_fontsize_changed(self, _index: int) -> None:
        if self._loading:
            return
        self._on_appearance_changed()

    def _on_appearance_changed(self) -> None:
        self._set_dirty(True)
        style = self._style()
        self.advanced_btn.setEnabled(style.is_classic)
        self.advanced_btn.setToolTip(
            "Item, size and font metrics for the classic scheme."
            if style.is_classic else
            "Metrics come from the selected visual style's own .msstyles.")
        if style.is_classic:
            name = style.resolve(self._colour_key(), self._size_key())
            try:
                self.scheme = cg.load_scheme(scheme_name=name)
            except ValueError:
                pass
        self._show_current_theme()
        self._render("appearance")

    def _appearance_info(self) -> tx.ThemeInfo:
        style = self._style()
        if style.is_classic:
            return tx.info_from_scheme(self.scheme, "Windows Classic")
        ini = style.resolve(self._colour_key(), self._size_key())
        key = (style.path, ini)
        if self._style_info_key != key:
            self._style_info = tx.info_from_style(
                style.path, ini, None, style.label)
            self._style_info_key = key
        return self._style_info

    def _sync_appearance(self, row: ThemeRow) -> None:
        """Follow the Themes tab: picking a theme moves the Appearance tab's
        three drop-downs onto the same thing, as it does in XP."""
        if row.kind == "user":
            style_path = None
        else:
            entry = row.entry
            style_path = (tf.theme_msstyles_path(entry.theme_file) if entry.theme_file
                          else entry.path)
        if not style_path:
            # A classic bundle: it has no visual style, but it does name the
            # stock scheme it is built from, so the drop-downs can still follow.
            if row.kind == "file" and row.entry.theme_file:
                self.scheme = cg.load_scheme(row.entry.theme_file)
                base, size_key = tx.classic_split(self.scheme.name)
                self.style_combo.setCurrentIndex(0)
                self._fill_colour_combo(base)
                if base not in {c.key for c in self._styles[0].colours()}:
                    self.colour_combo.addItem(self.scheme.name, self.scheme.name)
                    self.colour_combo.setCurrentIndex(self.colour_combo.count() - 1)
                self._fill_size_combo(size_key)
                self.advanced_btn.setEnabled(True)
            return
        for i, style in enumerate(self._styles):
            if style.path and _same_file(style.path, style_path):
                self.style_combo.setCurrentIndex(i)
                break
        else:
            return
        style = self._style()
        ini = tx.bundle_variant(row.entry, style) if row.kind == "file" else None
        colour, size_key = "", "NORMAL"
        if ini:
            for size, token in style.variants:
                if style.resolve(token, size) == ini:
                    colour, size_key = token, size
                    break
        self._fill_colour_combo(colour or None)
        self._fill_size_combo(size_key)
        self.advanced_btn.setEnabled(style.is_classic)

    def _open_advanced(self) -> None:
        dlg = AdvancedDialog(self.scheme, self)
        dlg.changed.connect(self._on_advanced_changed)
        dlg.exec()

    def _on_advanced_changed(self) -> None:
        self._set_dirty(True)
        self._mark_dirty()
        self._show_current_theme()
        self._render("appearance")

    def _mark_dirty(self) -> None:
        """An edit stops belonging to the stock scheme it started from, so it
        is never written back over it: Apply stashes it in the hidden
        applied-scheme file and Save As... offers it under a new name. The name
        itself is kept, because the Appearance drop-downs need something to
        point at while the metrics are the edited ones."""
        self._edited = True

    # --- rendering ---------------------------------------------------------

    def _render(self, which: str) -> None:
        if which == "themes":
            if self._theme_info is not None:
                self.themes_pane.render()
        elif which == "desktop":
            self.desktop_pane.render()
        elif which == "appearance":
            self.appearance_pane.render()

    def _render_current(self) -> None:
        # One path for both, so a new tab cannot be added and forgotten here --
        # which is exactly what would leave a pane blank on first switch.
        self._render(self.tabs.tabText(self.tabs.currentIndex()).lower())

    def _sync_palette(self) -> None:
        """Paint the editor's own widgets in whatever the *session* is using,
        read back from kdeglobals -- the way the real Display Properties wore
        the active scheme. Reading the live file (not self.scheme) is what
        keeps this right after a visual style is applied: the classic scheme
        being edited has nothing to do with the colours on screen then."""
        app = QApplication.instance()
        if app is None:
            return
        colours, font = setup_ui.live_palette()
        if colours is None:
            return
        pal = QPalette()
        disabled = colours.pop("disabled", None)
        for role, rgb in colours.items():
            pal.setColor(role, QColor(*rgb))
        if disabled is not None:
            for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
                pal.setColor(QPalette.Disabled, role, QColor(*disabled))
        app.setPalette(pal)
        if font is not None and font != self._applied_ui_font:
            app.setFont(font)
            self._applied_ui_font = font

    # --- actions -----------------------------------------------------------

    def _classic_job(self, scheme, theme_file: str | None, label: str, *, preserve_wallpaper=False) -> Job:
        snap = copy.deepcopy(scheme)               # edits may continue meanwhile
        # Widgets/Taskbar checkboxes are commented out in _build_button_row()
        # for now; both defaulted to checked, so this keeps that behaviour.
        panel = True
        widgets = True

        def run(progress):
            from xplasma import apply_scheme_live
            apply_scheme_live(snap, LIVE_NAME, panel=panel, layout=panel,
                              theme_file=theme_file, widgets=widgets, progress=progress,
                              preserve_wallpaper=preserve_wallpaper, apply_icons=not preserve_wallpaper)

        return Job(run, lambda: cg.save_applied(snap), label)

    def _style_job(self, path: str, name: str, theme_file: str | None,
                   ini: str | None, label: str) -> Job:
        # Widgets/Taskbar checkboxes are commented out in _build_button_row()
        # for now; both defaulted to checked, so this keeps that behaviour.
        panel = True
        widgets = True

        def run(progress):
            from xplasma import apply_msstyles_live
            apply_msstyles_live(path, name, theme_file=theme_file, ini_resource_name=ini,
                                panel=panel, layout=panel, widgets=widgets, progress=progress,
                                apply_icons=theme_file is not None)

        # A visual style has no classic scheme to fold into the editor's palette.
        return Job(run, None, label, sync_palette=False)

    def _job_for_themes(self) -> Job | None:
        row = self._current_row()
        if row is not None and row.kind == "current":
            return self._job_for_appearance()
        if row is None or row.kind not in ("file", "user"):
            self._set_status("Pick a theme from the list, or choose Add more... to open the themes folder.")
            return None
        if row.kind == "user":
            return self._classic_job(row.scheme, None, f"Applying {row.label} ...")
        entry = row.entry
        if entry.theme_file and not entry.style_path and tf.load_theme_file(entry.theme_file).get("visualstyles", {}).get("path"):
            self._set_status(f"{row.label}: the preset's visual style could not be found.")
            return None
        if not entry.supported:
            self._set_status(f"{row.label} is a Vista-format .msstyles; xp2plasma "
                                "can't read it.")
            return None
        style = entry.style_path
        if style:
            ini = tx.bundle_variant(entry, tx.visual_style_from(style))
            return self._style_job(style, row.label, entry.theme_file, ini, f"Applying {row.label} ...")
        # A .theme with no visual style: its whole content is the colour scheme.
        return self._classic_job(cg.load_scheme(entry.theme_file), entry.theme_file,
                                 f"Applying {row.label} ...")

    def _job_for_desktop(self) -> Job:
        """Apply the desktop on its own.

        No package regeneration and no plasmashell restart: a wallpaper and a
        background colour are two settings, which makes this the cheapest Apply
        in the dialog. sync_palette is off because nothing here changes the
        colours the dialog itself wears.
        """
        image = self._wallpaper_choice
        fill = self._fill_mode()
        colour = self.desktop_color.color()

        def work(progress) -> None:
            progress("Applying the desktop ...")
            if image:
                apply_mod.apply_wallpaper(image, fill, colour)
            else:
                # XP's "(None)" is a desktop of flat colour.
                apply_mod.apply_wallpaper_color(colour)

        return Job(work, None, "Applying the desktop ...", sync_palette=False)

    def _job_for_appearance(self) -> Job | None:
        style = self._style()
        if style.is_classic:
            # self.scheme is the drop-down's scheme plus whatever Advanced
            # changed -- never a fresh copy of the stock one, or those edits
            # (a title-bar font, say) would silently not be applied.
            label = self.scheme.name or "edited scheme"
            return self._classic_job(self.scheme, None, f"Applying {label} ...", preserve_wallpaper=True)
        ini = style.resolve(self._colour_key(), self._size_key())
        colour = self.colour_combo.currentText()
        size = self.fontsize_combo.currentText()
        label = f"Applying {style.label} ({colour}, {size}) ..."
        return self._style_job(style.path, style.label, None, ini, label)

    def _apply(self) -> None:
        if self._worker is not None:
            return
        title = self.tabs.tabText(self.tabs.currentIndex())
        if title == "Themes":
            job = self._job_for_themes()
        elif title == "Desktop":
            job = self._job_for_desktop()
        elif title == "Appearance":
            job = self._job_for_appearance()
        else:
            job = None
        if job is None:
            return
        self._set_busy(True, job.label)
        # A modal dialog rather than an inline bar: applying takes a while and
        # has a real Cancel, and the window is a fixed size that nothing should
        # reflow underneath.
        self._cancel_event = threading.Event()
        host.set_cancel_event(self._cancel_event)
        self._dialog = setup_ui.ProgressDialog(job.label, self)
        self._dialog.cancelled.connect(self._cancel_event.set)
        self._worker = ApplyWorker(job)
        self._worker.progress.connect(self._set_status)
        self._worker.progress.connect(self._dialog.on_progress)
        self._worker.done.connect(self._apply_finished)
        self._worker.start()
        self._dialog.show()

    def _apply_finished(self, ok: bool, message: str) -> None:
        self._worker = None
        if self._dialog is not None:
            self._dialog.finish()       # close() would mean "cancel" here
            self._dialog = None
        host.set_cancel_event(None)
        cancelled = self._cancel_event is not None and self._cancel_event.is_set()
        self._cancel_event = None
        close_after = self._pending_close
        self._pending_close = False
        if cancelled:
            self._set_busy(False, "Cancelled.")
            return
        self._set_busy(False, "Done." if ok else f"Failed - {message}")
        if ok:
            self._set_dirty(False)
        if not ok:
            QMessageBox.warning(self, "Apply failed", message)
            return
        self._sync_palette()            # whatever was applied, wear it
        self._applied = applied_state.load()
        try:
            self._desktop = theme_presets.current_desktop()
        except (OSError, ValueError):
            self._desktop = None
        self._init_theme_selection()
        if close_after:
            self.close()

    def _set_status(self, message: str) -> None:
        self.status.setText(message)

    def _set_busy(self, busy: bool, status: str) -> None:
        self._busy = busy
        self.status.setText(status)
        for b in (self.ok_btn, self.cancel_btn, self.apply_btn, self.save_btn, self.delete_btn):
            b.setEnabled(not busy)
        self._update_apply_enabled()

    def _update_apply_enabled(self) -> None:
        self.apply_btn.setEnabled(not self._busy and (self._dirty or self._shift_held))

    def _set_dirty(self, dirty: bool) -> None:
        """Apply is only live when there is something to apply, the way XP's
        is. Without it OK would re-run the whole pipeline over a desktop that
        already has exactly that theme on it."""
        self._dirty = dirty
        self._update_apply_enabled()

    def _ok(self) -> None:
        if self._worker is not None:
            return
        if not self._dirty:
            self.close()            # already applied; OK just closes
            return
        self._pending_close = True
        self._apply()


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv if argv is None else argv)

    def opt(flag: str) -> str | None:
        return argv[argv.index(flag) + 1] if flag in argv and argv.index(flag) + 1 < len(argv) else None

    shot = opt("--screenshot")

    # Never hand our own flags to Qt: it would read them as its arguments.
    app = QApplication.instance() or QApplication(["xp2plasma-edit"])
    setup_ui.apply_system_theme(app)
    win = AppearanceEditor()

    if shot:
        if opt("--theme"):
            target = opt("--theme")
            for row in win._rows:
                if row.kind == "file" and row.label == target:
                    win.tabs.setCurrentIndex(TAB_TITLES.index("Themes"))
                    win._fill_theme_combo()
                    win._select_row(row)
                    win._show_theme(row)
                    break
        if opt("--style"):
            want = opt("--style")
            for i, style in enumerate(win._styles):
                if style.label == want:
                    win.tabs.setCurrentIndex(TAB_TITLES.index("Appearance"))
                    win.style_combo.setCurrentIndex(i)
                    win._on_style_changed(i)
                    if opt("--colour"):
                        win.colour_combo.setCurrentIndex(win.colour_combo.findData(opt("--colour")))
                        win._on_colour_changed(0)
                    if opt("--size"):
                        win.fontsize_combo.setCurrentIndex(win.fontsize_combo.findData(opt("--size")))
                        win._on_fontsize_changed(0)
                    break
        if opt("--scheme"):
            win.scheme = cg.load_scheme(scheme_name=opt("--scheme"))
        tab = opt("--tab")
        if tab in TAB_TITLES:
            win.tabs.setCurrentIndex(TAB_TITLES.index(tab))
        win.show()
        app.processEvents()
        win._render_current()       # the resize debounce hasn't fired yet
        app.processEvents()
        win.grab().save(shot)
        print(f"wrote {shot}")
        return 0

    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
