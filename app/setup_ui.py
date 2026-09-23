"""Shared UI helpers: an Ubiquity-style job pane (a check-mark steps table,
a progress bar, and the raw log behind a "Details" toggle), a pop-up progress
dialog for downloads, and matching the live KDE session's look.

Jobs report through a plain ``progress(line)`` callable, so the same functions
work from the command line. The pane turns that stream into steps with one
rule: a line ending in "..." starts a step; an indented line is detail. That is
the convention every job in setup_assets, install and revert already follows.
"""
from __future__ import annotations

import configparser
import os
import threading

from PySide6.QtCore import Qt, QThread, Signal
from PySide6.QtGui import QBrush, QColor, QFont, QPalette
from PySide6.QtWidgets import (QAbstractItemView, QApplication, QDialog, QHBoxLayout,
                               QHeaderView, QLabel, QMessageBox, QPlainTextEdit, QProgressBar,
                               QPushButton, QTableWidget, QTableWidgetItem, QToolButton,
                               QVBoxLayout, QWidget)

import hostexec as host
import xp_style


def live_palette():
    """(palette roles -> rgb, ui QFont) from ~/.config/kdeglobals -- the
    colours and font the session is showing right now. (None, None) if the
    file can't be read, in which case the caller keeps Qt's defaults.

    Read directly rather than via kreadconfig6: this is pure Python with no
    subprocess at all, and the file is an ordinary home path, so there is
    nothing a helper process would see that this doesn't.
    """
    path = os.path.expanduser("~/.config/kdeglobals")
    cp = configparser.ConfigParser(interpolation=None, strict=False)
    cp.optionxform = str
    try:
        with open(path, encoding="utf-8") as fh:
            cp.read_file(fh)
    except (OSError, configparser.Error):
        return None, None

    def rgb(section: str, key: str):
        parts = cp.get(section, key, fallback="").split(",")
        try:
            return tuple(int(p) for p in parts[:3]) if len(parts) >= 3 else None
        except ValueError:
            return None

    wanted = {
        QPalette.Window: ("Colors:Window", "BackgroundNormal"),
        QPalette.WindowText: ("Colors:Window", "ForegroundNormal"),
        QPalette.Base: ("Colors:View", "BackgroundNormal"),
        QPalette.Text: ("Colors:View", "ForegroundNormal"),
        QPalette.Button: ("Colors:Button", "BackgroundNormal"),
        QPalette.ButtonText: ("Colors:Button", "ForegroundNormal"),
        QPalette.Highlight: ("Colors:Selection", "BackgroundNormal"),
        QPalette.HighlightedText: ("Colors:Selection", "ForegroundNormal"),
        QPalette.ToolTipBase: ("Colors:Tooltip", "BackgroundNormal"),
        QPalette.ToolTipText: ("Colors:Tooltip", "ForegroundNormal"),
    }
    colours = {}
    for role, (section, key) in wanted.items():
        value = rgb(section, key)
        if value is not None:
            colours[role] = value
    if not colours:
        return None, None
    colours[QPalette.AlternateBase] = colours.get(QPalette.Button, colours.get(QPalette.Window))
    disabled = rgb("Colors:Window", "ForegroundInactive")
    if disabled is not None:
        colours["disabled"] = disabled
    font = None
    spec = cp.get("General", "font", fallback="")
    if spec:
        font = QFont()
        if not font.fromString(spec):
            font = None
    return colours, font


def system_widget_style_name() -> str | None:
    """The Qt widget style the live KDE session says it's using
    ([KDE] widgetStyle in kdeglobals), or None if it can't be read."""
    path = os.path.expanduser("~/.config/kdeglobals")
    cp = configparser.ConfigParser(interpolation=None, strict=False)
    cp.optionxform = str
    try:
        with open(path, encoding="utf-8") as fh:
            cp.read_file(fh)
    except (OSError, configparser.Error):
        return None
    return cp.get("KDE", "widgetStyle", fallback="") or None


def apply_system_theme(app: QApplication) -> None:
    """Make `app` look like the live KDE session: its real widget style where
    that can actually load, and always its colours and font -- so a light
    desktop gets a light window and a dark one gets a dark window, instead of
    a hardcoded look picked without knowing what session it will run in.

    Trying the real style is opportunistic, not assumed to work: under a
    system-Qt-linked interpreter (see system_qt.py) ``QStyleFactory.create()``
    genuinely loads whatever the session has configured -- kvantum-xp, Breeze,
    anything -- exactly like any other native KDE app, and that is used as-is.
    A pip-installed PySide6 bundles its own Qt with no ``styles/`` plugin
    directory at all, so there ``QStyleFactory.create()`` cleanly returns None;
    only *then* does this fall back to Fusion plus, if xp2plasma's own theme is
    the one active, xp_style's SVG-based approximation of it -- a fallback for
    our own output specifically, never a substitute for loading a real,
    possibly different, user-configured style.
    """
    name = system_widget_style_name()
    style, loaded = xp_style.base_style(name)
    if not loaded:
        style = xp_style.wrap(style)
    app.setStyle(style)

    colours, font = live_palette()
    if colours is not None:
        palette = QPalette()
        disabled = colours.pop("disabled", None)
        for role, rgb in colours.items():
            palette.setColor(role, QColor(*rgb))
        if disabled is not None:
            for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
                palette.setColor(QPalette.Disabled, role, QColor(*disabled))
        app.setPalette(palette)
    if font is not None:
        app.setFont(font)


class JobWorker(QThread):
    """Runs a setup job off the GUI thread.

    `fn` is called as ``fn(progress, on_bytes)``: `progress(line)` is the same
    convention every job in setup_assets/install/revert already follows
    (a line ending in "..." starts a step; an indented line is detail), and
    `on_bytes(done, total)` is for the one thing that has real byte-level
    progress -- a download. Both are Qt signal emits, which is the
    thread-safe way to get a worker thread's calls onto the GUI thread; jobs
    that do not report bytes just never call it.

    Cancellation is cooperative: the job must be one that raises
    hostexec.Cancelled, which every job in this app does once
    ``hostexec.set_cancel_event()`` names it as cancellable (the caller does
    that before starting the worker, not this class -- it has to be in place
    before the very first host command the job runs).
    """

    progress = Signal(str)
    bytes_progress = Signal(int, int)
    done = Signal(str, str)          # status: ok | failed | cancelled, message

    def __init__(self, fn) -> None:
        super().__init__()
        self._fn = fn

    def run(self) -> None:
        try:
            self._fn(self.progress.emit, self.bytes_progress.emit)
            self.done.emit("ok", "")
        except host.Cancelled:
            self.done.emit("cancelled", "")
        except Exception as exc:                       # surfaced in the log
            self.done.emit("failed", f"{type(exc).__name__}: {exc}")


STATUS = {
    "ok": ("✓", QColor(0x1E, 0x7B, 0x2E)),
    "miss": ("✗", QColor(0xB0, 0x1C, 0x1C)),
    "failed": ("✗", QColor(0xB0, 0x1C, 0x1C)),
    "cancelled": ("–", QColor(0x80, 0x80, 0x80)),
    "skip": ("–", QColor(0x80, 0x80, 0x80)),
    "running": ("…", QColor(0x0A, 0x24, 0x6A)),
}


def mono_log() -> QPlainTextEdit:
    log = QPlainTextEdit()
    log.setReadOnly(True)
    log.setFont(QFont("Monospace", 9))
    return log


def status_table(columns: list[str]) -> QTableWidget:
    """A read-only glyph | ... table in the style of the system check."""
    table = QTableWidget(0, len(columns))
    table.setHorizontalHeaderLabels(columns)
    table.verticalHeader().hide()
    table.setSelectionMode(QAbstractItemView.NoSelection)
    table.setEditTriggers(QAbstractItemView.NoEditTriggers)
    table.setFocusPolicy(Qt.NoFocus)
    table.setShowGrid(False)
    header = table.horizontalHeader()
    for col in range(len(columns) - 1):
        header.setSectionResizeMode(col, QHeaderView.ResizeToContents)
    header.setSectionResizeMode(len(columns) - 1, QHeaderView.Stretch)
    header.setHighlightSections(False)
    return table


def add_status_row(table: QTableWidget, status: str, *cells: str) -> int:
    glyph, colour = STATUS.get(status, STATUS["skip"])
    row = table.rowCount()
    table.insertRow(row)
    mark = QTableWidgetItem(glyph)
    mark.setForeground(QBrush(colour))
    mark.setTextAlignment(Qt.AlignCenter)
    bold = QFont(mark.font())
    bold.setBold(True)
    mark.setFont(bold)
    table.setItem(row, 0, mark)
    for col, text in enumerate(cells, start=1):
        table.setItem(row, col, QTableWidgetItem(text))
    return row


def set_status(table: QTableWidget, row: int, status: str) -> None:
    glyph, colour = STATUS.get(status, STATUS["skip"])
    item = table.item(row, 0)
    if item is not None:
        item.setText(glyph)
        item.setForeground(QBrush(colour))


def _mb(n: int) -> str:
    return f"{n / 1048576:.0f} MB"


class StepsPane(QWidget):
    """Steps table + progress bar + a Details toggle over the raw log."""

    def __init__(self) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(8)
        self.table = status_table(["", "Step"])
        layout.addWidget(self.table, 1)
        self.bar = QProgressBar()
        self.bar.setRange(0, 0)
        self.bar.setTextVisible(False)
        self.bar.hide()
        layout.addWidget(self.bar)
        self.toggle = QToolButton()
        self.toggle.setText("Details")
        self.toggle.setCheckable(True)
        self.toggle.setArrowType(Qt.RightArrow)
        self.toggle.setToolButtonStyle(Qt.ToolButtonTextBesideIcon)
        self.toggle.setAutoRaise(True)
        self.toggle.toggled.connect(self._toggled)
        layout.addWidget(self.toggle, 0, Qt.AlignLeft)
        self.log = mono_log()
        self.log.setMinimumHeight(110)
        self.log.hide()
        layout.addWidget(self.log, 1)
        self._current: int | None = None

    def _toggled(self, shown: bool) -> None:
        self.toggle.setArrowType(Qt.DownArrow if shown else Qt.RightArrow)
        self.log.setVisible(shown)

    def reset(self) -> None:
        self.table.setRowCount(0)
        self.log.clear()
        self._current = None
        self.bar.setRange(0, 0)
        self.bar.setTextVisible(False)
        self.bar.hide()

    def text(self) -> str:
        return self.log.toPlainText()

    def begin(self) -> None:
        self.bar.show()

    def on_progress(self, line: str) -> None:
        self.log.appendPlainText(line)
        if line[:1].isspace():
            return                                   # detail: log only
        if line.rstrip().endswith("..."):
            if self._current is not None:
                set_status(self.table, self._current, "ok")
            self._current = add_status_row(self.table, "running", line.rstrip().rstrip(".").rstrip())
            self.table.scrollToBottom()

    def set_bytes(self, done: int, total: int) -> None:
        self.bar.show()
        if total > 0:
            self.bar.setRange(0, 1000)
            self.bar.setValue(int(done * 1000 / total))
            self.bar.setFormat(f"{_mb(done)} of {_mb(total)}")
            self.bar.setTextVisible(True)
        else:
            self.bar.setRange(0, 0)
            self.bar.setTextVisible(False)

    def finish(self, status: str, message: str = "") -> None:
        if self._current is not None:
            set_status(self.table, self._current, "ok" if status == "ok" else status)
        self._current = None
        self.bar.hide()
        self.log.appendPlainText({"ok": "Done.", "cancelled": "Cancelled."}.get(
            status, f"Failed: {message}"))
        if status != "ok":
            self.toggle.setChecked(True)             # something went wrong: show why


class CheckPane(QWidget):
    """Like StepsPane, but each progress line is a finished check result
    ("status\tname\tpurpose"), not a step in progress -- used for the system
    check page, which reports pass/fail rather than running steps."""

    def __init__(self, columns: list[str]) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(8)
        self.table = status_table(columns)
        layout.addWidget(self.table, 1)
        self.note = QLabel()
        self.note.setWordWrap(True)
        layout.addWidget(self.note)

    def reset(self) -> None:
        self.table.setRowCount(0)
        self.note.setText("")

    def begin(self) -> None:
        pass

    def on_progress(self, line: str) -> None:
        status, *cells = line.split("\t")
        add_status_row(self.table, status, *cells)

    def set_bytes(self, done: int, total: int) -> None:
        pass

    def finish(self, status: str, message: str = "") -> None:
        if status == "failed":
            self.note.setText(message.split(": ", 1)[-1])


class ProgressDialog(QDialog):
    """A pop-up for one download: what's happening, how far along, Cancel."""

    cancelled = Signal()

    def __init__(self, title: str, parent=None) -> None:
        super().__init__(parent)
        self.setWindowTitle(title)
        self.setMinimumWidth(420)
        self.setModal(True)
        self._running = True
        layout = QVBoxLayout(self)
        self.label = QLabel(title)
        self.label.setWordWrap(True)
        layout.addWidget(self.label)
        self.bar = QProgressBar()
        self.bar.setRange(0, 0)
        self.bar.setTextVisible(False)
        layout.addWidget(self.bar)
        row = QHBoxLayout()
        row.addStretch(1)
        self.cancel_btn = QPushButton("Cancel")
        self.cancel_btn.clicked.connect(self._cancel)
        row.addWidget(self.cancel_btn)
        layout.addLayout(row)

    def on_progress(self, line: str) -> None:
        if not line[:1].isspace():
            self.label.setText(line.strip())

    def set_bytes(self, done: int, total: int) -> None:
        if total > 0:
            self.bar.setRange(0, 1000)
            self.bar.setValue(int(done * 1000 / total))
            self.bar.setFormat(f"{_mb(done)} of {_mb(total)}")
            self.bar.setTextVisible(True)
        else:
            self.bar.setRange(0, 0)
            self.bar.setTextVisible(False)

    def _cancel(self) -> None:
        self.cancel_btn.setEnabled(False)
        self.label.setText("Cancelling ...")
        self.cancelled.emit()

    def reject(self) -> None:
        # Closing the pop-up means "cancel", not "hide the job".
        if self._running:
            self._cancel()
        else:
            super().reject()

    def finish(self) -> None:
        self._running = False
        self.accept()


class JobRunner:
    """Owns the worker thread and cancel event for a dialog's job pages.

    One instance per dialog, reused across however many jobs it runs one at a
    time. Connects a job's progress to a pane (StepsPane or CheckPane) and,
    for byte-reporting jobs, a pop-up ProgressDialog -- so a page's own code
    only has to say what to run and where its output goes, not how threading,
    cancellation or the pop-up work.
    """

    def __init__(self, parent: QWidget) -> None:
        self._parent = parent
        self.worker: JobWorker | None = None
        self._cancel: threading.Event | None = None
        self._pane = None
        self._dialog: ProgressDialog | None = None
        self._on_finished = None

    @property
    def busy(self) -> bool:
        return self.worker is not None

    @property
    def cancelling(self) -> bool:
        return self._cancel is not None and self._cancel.is_set()

    def start(self, fn, pane, *, byte_progress: bool = False,
             dialog_title: str | None = None, on_finished=None) -> None:
        """Run `fn(progress, on_bytes)` on a worker thread. `pane` gets the
        step-by-step output; `on_finished(status, message)` is called back on
        the GUI thread once the job ends (status: ok | failed | cancelled)."""
        if self.worker is not None:
            return                                  # one job at a time
        pane.reset()
        pane.begin()
        self._pane = pane
        self._on_finished = on_finished
        self._cancel = threading.Event()
        host.set_cancel_event(self._cancel)
        self.worker = JobWorker(fn)
        self.worker.progress.connect(self._on_progress)
        self.worker.bytes_progress.connect(self._on_bytes)
        self.worker.done.connect(self._on_done)
        if byte_progress:
            self._dialog = ProgressDialog(dialog_title or "Working ...", self._parent)
            self._dialog.cancelled.connect(lambda: self.request_cancel(ask=False))
            self._dialog.show()
        self.worker.start()

    def request_cancel(self, ask: bool = True,
                       question: str = "Stop the current task?\n\n"
                                       "Anything it has done so far will be rolled back.") -> bool:
        """True if a cancellation was (or is already) in flight."""
        if self.worker is None:
            return False
        if self.cancelling:
            return True
        if ask and QMessageBox.question(
                self._parent, "xp2plasma Setup", question,
                QMessageBox.Yes | QMessageBox.No, QMessageBox.No) != QMessageBox.Yes:
            return False
        self._cancel.set()
        if self._pane is not None:
            self._pane.on_progress("Cancelling ...")
        if self._dialog is not None:
            self._dialog.label.setText("Cancelling ...")
            self._dialog.cancel_btn.setEnabled(False)
        return True

    def _on_progress(self, line: str) -> None:
        if self._pane is not None:
            self._pane.on_progress(line)
        if self._dialog is not None:
            self._dialog.on_progress(line)

    def _on_bytes(self, done: int, total: int) -> None:
        if self._pane is not None:
            self._pane.set_bytes(done, total)
        if self._dialog is not None:
            self._dialog.set_bytes(done, total)

    def _on_done(self, status: str, message: str) -> None:
        worker, self.worker = self.worker, None
        if worker is not None:
            worker.wait()
        host.set_cancel_event(None)
        self._cancel = None
        if self._pane is not None:
            self._pane.finish(status, message)
        if self._dialog is not None:
            self._dialog.finish()
        self._dialog = None
        self._pane = None
        callback, self._on_finished = self._on_finished, None
        if callback:
            callback(status, message)
