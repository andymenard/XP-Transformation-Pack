"""Exercise the real kvantum-xp plugin (use the host Qt Python bindings).

QT_QPA_PLATFORM=offscreen QT_PLUGIN_PATH=<plugin root> python3 tests/test_native_buttons.py
Optionally pass an output PNG path: empty, labelled and defaulted rows;
normal/hot/pressed columns.
"""
import sys

from PySide6.QtCore import QRect
from PySide6.QtGui import QColor, QFont, QImage, QPainter
from PySide6.QtWidgets import QApplication, QPushButton, QStyle, QStyleFactory, QStyleOptionButton

app = QApplication([])
style = QStyleFactory.create("kvantum-xp")
assert style is not None, "Native kvantum-xp plugin must load; no fallback allowed"
app.setStyle(style)
font = QFont("Tahoma", 8)
app.setFont(font)
states = (QStyle.State_Raised,
          QStyle.State_Raised | QStyle.State_MouseOver,
          QStyle.State_Sunken)
gallery = QImage(243, 87, QImage.Format_ARGB32)
gallery.fill(QColor(236, 233, 216))

def render(text, state, default=False, enabled=True):
    image = QImage(73, 21, QImage.Format_ARGB32)
    image.fill(QColor(236, 233, 216))
    option = QStyleOptionButton()
    option.rect = QRect(0, 0, 73, 21)
    option.text = text
    option.palette = style.standardPalette()
    option.state = QStyle.State_Active | state
    if enabled:
        option.state |= QStyle.State_Enabled
    button = QPushButton(text)
    button.setDefault(default)
    if default:
        option.features |= QStyleOptionButton.DefaultButton
    painter = QPainter(image)
    painter.setFont(font)
    style.drawControl(QStyle.CE_PushButton, option, painter, button)
    painter.end()
    return image

for i, state in enumerate(states):
    empty, labelled = render("", state), render("Cancel", state)
    # Text must never cause the artwork's corners/outer rim to be rescaled.
    for y in range(21):
        for x in range(73):
            if x < 5 or x >= 68 or y < 4 or y >= 17:
                assert empty.pixel(x, y) == labelled.pixel(x, y), (i, x, y)
    painter = QPainter(gallery)
    painter.drawImage(4 + i * 81, 4, empty)
    painter.drawImage(4 + i * 81, 33, labelled)
    painter.drawImage(4 + i * 81, 62, render("OK", state, default=True))
    painter.end()
if len(sys.argv) > 1:
    assert gallery.save(sys.argv[1])
print("Native normal/hot/pressed button borders remain intact with labels")
assert render("OK", states[0]) != render("OK", states[0], default=True), "Missing DEFAULTED art"
for state in (*states[1:], QStyle.State_On):
    assert render("OK", state) == render("OK", state, default=True), "Default art masked an interaction state"
assert render("OK", states[0], enabled=False) == render("OK", states[0], default=True, enabled=False)
print("Native DEFAULTED art is distinct; hover, pressed, toggled and disabled take precedence")
