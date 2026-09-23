"""Render the production tray effect, checking pixels and input passthrough."""
import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QPA_PLATFORMTHEME", "generic")
# Window grabs from a Python test need the GUI-thread renderer on Wayland.
os.environ.setdefault("QSG_RENDER_LOOP", "basic")
from PySide6.QtCore import QPoint, QUrl, Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickView
from PySide6.QtTest import QTest
import panel_widgets

app = QGuiApplication.instance() or QGuiApplication([])


class TrayShadowTests(unittest.TestCase):
    def test_shadow_preserves_pixels_geometry_and_clicks(self):
        qml = '''import QtQuick
import QtQuick.Effects
Rectangle {
    id: root
    width: 64; height: 64; color: "#80b8ea"
    property bool xpTrayAvailable: true
    property bool vertical: false
    property bool inTray: true
    property int clicks: 0
    readonly property bool softwareRenderer: GraphicsInfo.api === GraphicsInfo.Software
    Item {
        id: abstractItem
        x: 16; y: 16; width: 16; height: 16
        readonly property bool inVisibleLayout: root.inTray
''' + panel_widgets.TRAY_ICON_SHADOW + '''
        Rectangle { x: 4; y: 4; width: 8; height: 8; color: "white" }
        MouseArea { anchors.fill: parent; onClicked: root.clicks++ }
    }
}
'''
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "TrayShadow.qml"
            path.write_text(qml)
            view = QQuickView()
            view.setSource(QUrl.fromLocalFile(str(path)))
            self.assertEqual(view.status(), QQuickView.Ready, [e.toString() for e in view.errors()])
            view.show()
            QTest.qWait(100)
            root = view.rootObject()
            shaded = view.grabWindow()
            self.assertFalse(shaded.isNull(), "A GPU/RHI-capable Qt backend is required")
            self.assertEqual(shaded.pixelColor(24, 24).name(), "#ffffff")
            background = shaded.pixelColor(0, 0)
            if root.property("softwareRenderer"):
                self.assertEqual(shaded.pixelColor(24, 28), background)
                view.close()
                self.skipTest("Software rendering fallback verified; GPU required for shadow assertions")
            self.assertLess(shaded.pixelColor(24, 28).red(), background.red())
            # A 35%-opaque black shadow leaves at least 65% of the background;
            # allow one level for 8-bit channel rounding.
            self.assertGreaterEqual(shaded.pixelColor(24, 28).red(), background.red() * 0.65 - 1)
            QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(24, 24))
            self.assertEqual(root.property("clicks"), 1)
            baseline = None
            for prop, disabled, enabled in (("xpTrayAvailable", False, True),
                                            ("vertical", True, False), ("inTray", False, True)):
                root.setProperty(prop, disabled)
                QTest.qWait(50)
                plain = view.grabWindow()
                self.assertEqual(plain.pixelColor(24, 28), background)
                for y in range(20, 28):
                    for x in range(20, 28):
                        self.assertEqual(plain.pixelColor(x, y), shaded.pixelColor(x, y))
                if baseline is not None:
                    self.assertEqual(plain, baseline)
                baseline = plain
                root.setProperty(prop, enabled)
            view.close()


if __name__ == "__main__":
    unittest.main()
