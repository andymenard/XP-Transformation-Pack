"""Run with the host Qt bindings and QT_PLUGIN_PATH pointing at kvantum-xp."""
import sys

from PySide6.QtCore import QRect, Qt
from PySide6.QtGui import QImage, QPainter
from PySide6.QtWidgets import QApplication, QComboBox, QStyle, QStyleFactory, QStyleOptionComboBox

app = QApplication([])
style = QStyleFactory.create("kvantum-xp")
assert style is not None, "The native plugin must load; no fallback"
app.setStyle(style)
combo = QComboBox()
combo.addItem("Windows XP")
combo.resize(180, 21)
combo.show()
combo.activateWindow()
app.processEvents()


def render(focused=False, hovered=False, enabled=True, editable=False, rtl=False,
           selected=False, pressed=False, popup=False):
    combo.setEditable(editable)
    option = QStyleOptionComboBox()
    combo.initStyleOption(option)
    option.rect = QRect(0, 0, 180, 21)
    option.direction = Qt.RightToLeft if rtl else Qt.LeftToRight
    option.state = QStyle.State_Active
    if enabled:
        option.state |= QStyle.State_Enabled
    if focused:
        option.state |= QStyle.State_HasFocus
    if hovered:
        option.state |= QStyle.State_MouseOver
    if selected:
        option.state |= QStyle.State_Selected
    if pressed:
        option.state |= QStyle.State_Sunken
    if popup:
        option.state |= QStyle.State_On
    image = QImage(180, 21, QImage.Format_ARGB32)
    image.fill(option.palette.window().color())
    painter = QPainter(image)
    style.drawComplexControl(QStyle.CC_ComboBox, option, painter, combo)
    style.drawControl(QStyle.CE_ComboBoxLabel, option, painter, combo)
    painter.end()
    return image, option


def count(image, colour, rect, tolerance=0):
    # Small antialiased glyphs need not contain any fully covered white pixel.
    return sum(max(abs(a-b) for a, b in zip(image.pixelColor(x, y).getRgb()[:3], colour.getRgb()[:3])) <= tolerance
               for y in range(rect.top(), rect.bottom() + 1)
               for x in range(rect.left(), rect.right() + 1))


gallery = QImage(188, 91, QImage.Format_ARGB32)
gallery.fill(combo.palette().window().color())
for rtl in (False, True):
    normal, option = render(rtl=rtl)
    focused, _ = render(focused=True, rtl=rtl)
    if len(sys.argv) > 1 and not rtl:
        focused.save(sys.argv[1] + ".focused.png")
    field = style.subControlRect(QStyle.CC_ComboBox, option, QStyle.SC_ComboBoxEditField, combo).adjusted(4, 3, -4, -3)
    blue = option.palette.highlight().color()
    white = option.palette.highlightedText().color()
    assert count(focused, blue, field) > 100, "Focused dropdown is missing the selection background"
    assert count(focused, white, field, tolerance=32) > 5, "Selected text must use HighlightedText"
    assert count(normal, blue, field) == 0, "Unfocused dropdown must not be selected"
    for kwargs in ({"hovered": True}, {"focused": True, "enabled": False},
                   {"focused": True, "editable": True}):
        image, _ = render(rtl=rtl, **kwargs)
        assert count(image, blue, field) == 0, kwargs
    arrow = style.subControlRect(QStyle.CC_ComboBox, option, QStyle.SC_ComboBoxArrow, combo)
    assert normal.copy(arrow) == focused.copy(arrow), "Focus highlight must not repaint the arrow"
    selected, _ = render(focused=True, selected=True, rtl=rtl)
    assert selected.copy(arrow) == normal.copy(arrow), "Qt's focused/selected flag must not leave the arrow pressed"
    pressed, _ = render(focused=True, selected=True, pressed=True, rtl=rtl)
    assert pressed.copy(arrow) != selected.copy(arrow), "A real press must still depress the arrow"
    popup, _ = render(focused=True, selected=True, popup=True, rtl=rtl)
    assert popup.copy(arrow) != selected.copy(arrow), "An open popup must still depress the arrow"
    if not rtl:
        painter = QPainter(gallery)
        painter.drawImage(4, 4, normal)
        painter.drawImage(4, 35, focused)
        painter.end()
        hovered, _ = render(hovered=True)
        painter = QPainter(gallery)
        painter.drawImage(4, 66, hovered)
        painter.end()

if len(sys.argv) > 1:
    assert gallery.save(sys.argv[1])
print("Native combo focus: highlight and text colours pass; hover, disabled, editable, RTL and arrow checks pass")
