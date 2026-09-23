"""Native Qt click + real proxy-model tests. No live desktop writes."""
import os
from pathlib import Path
import sys
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fixtures  # noqa: E402
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PySide6.QtCore import QAbstractListModel, QModelIndex, QPoint, QUrl, Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickView
from PySide6.QtTest import QTest
import panel_widgets

app = QGuiApplication.instance() or QGuiApplication([])


class TrayModel(QAbstractListModel):
    # Plasma ItemStatus: Passive=1, Active=2, Hidden=5.
    entries = [("always-shown", 2), ("explicit-first", 1), ("auto-active", 2), ("explicit", 1),
               ("auto-inactive", 1), ("disabled", 5)]

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.entries)

    def roleNames(self):
        return {int(Qt.UserRole) + 2: b"itemId", int(Qt.UserRole) + 6: b"effectiveStatus"}

    def data(self, index, role):
        if not index.isValid():
            return None
        if role == int(Qt.UserRole) + 2:
            return self.entries[index.row()][0]
        if role == int(Qt.UserRole) + 6:
            return self.entries[index.row()][1]


class TrayClickTests(unittest.TestCase):
    def test_generated_svg_exposes_xp_content_margin_to_ksvg(self):
        repo = Path(__file__).resolve().parents[1]
        svg = subprocess.check_output([sys.executable, "-c",
            "from msstyles_parse import MsStyle; from plasmatheme_gen import build_tray_svg; "
            f"print(build_tray_svg(MsStyle.load({str(fixtures.LUNA)!r}, 'NORMALBLUE_INI')))"], cwd=repo)
        with tempfile.TemporaryDirectory() as out:
            image = Path(out) / "xp-tray.svg"
            image.write_bytes(svg)
            path = Path(out) / "TrayMargin.qml"
            path.write_text('''import QtQuick
import org.kde.ksvg as KSvg
KSvg.FrameSvgItem {
    width: 120
    height: 28
    prefix: "tray"
    imagePath: "''' + str(image) + '''"
    readonly property real leftInset: margins.left
}
''')
            view = QQuickView()
            view.setSource(QUrl.fromLocalFile(str(path)))
            self.assertEqual(view.status(), QQuickView.Ready, [e.toString() for e in view.errors()])
            self.assertEqual(view.rootObject().property("leftInset"), 19)
            view.close()

    def test_no_expander_reserves_cap_and_regular_icons_stay_right_anchored(self):
        source = Path("/usr/share/plasma/plasmoids/org.kde.plasma.systemtray/contents/ui/main.qml").read_text()
        patched = panel_widgets.patch_main(source, True)
        grid_properties = patched.split("                id: tasksGrid\n", 1)[1].split("Layout.alignment:", 1)[0]
        qml = '''import QtQuick
import QtQuick.Layouts
Item {
    id: root
    width: mainLayout.implicitWidth
    height: 24
    property bool xpTrayAvailable: true
    property bool vertical: false
    property bool hasExpander: false
    property int iconCount: 4
    property int contentInset: 19
    readonly property real firstIconX: tasksGrid.x
    readonly property real regularIconFromRight: width - (tasksGrid.x + (iconCount - 4) * 18)
    QtObject {
        id: xpTrayBackground
        readonly property var margins: ({left: root.contentInset})
    }
    GridLayout {
        id: mainLayout
        anchors.fill: parent
        rowSpacing: 0
        columnSpacing: 0
        Item {
            id: tasksGrid
''' + grid_properties + '''
            implicitWidth: root.iconCount * 18
            implicitHeight: 24
        }
        Item {
            id: expander
            Layout.row: 0
            Layout.column: 0
            implicitWidth: 20
            implicitHeight: 24
            visible: root.hasExpander
        }
    }
}
'''
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "TrayLayout.qml"
            path.write_text(qml)
            view = QQuickView()
            view.setSource(QUrl.fromLocalFile(str(path)))
            self.assertEqual(view.status(), QQuickView.Ready, [e.toString() for e in view.errors()])
            view.show()
            QTest.qWait(30)
            root = view.rootObject()
            self.assertEqual(root.property("firstIconX"), 19)
            self.assertEqual(root.property("regularIconFromRight"), 72)
            for show, count in ((True, 4), (True, 6), (True, 4), (False, 4)):
                root.setProperty("hasExpander", show)
                root.setProperty("iconCount", count)
                QTest.qWait(30)
                self.assertEqual(root.property("firstIconX"), 20 if show else 19)
                self.assertEqual(root.property("regularIconFromRight"), 72)
            root.setProperty("contentInset", 27)
            QTest.qWait(30)
            self.assertEqual(root.property("firstIconX"), 27)
            root.setProperty("xpTrayAvailable", False)
            QTest.qWait(30)
            self.assertEqual(root.property("firstIconX"), 0)
            root.setProperty("xpTrayAvailable", True)
            root.setProperty("vertical", True)
            QTest.qWait(30)
            self.assertEqual(root.property("firstIconX"), 0)
            view.close()

    def test_click_only_reveals_explicitly_hidden_items(self):
        imports, loader = panel_widgets.EXPANDER.split("Loader {", 1)
        imports = imports.replace("import org.kde.plasma.plasmoid\n", "")
        imports += "\nimport org.kde.kitemmodels as KItemModels\n"
        loader = ("Loader {" + loader).replace(
            "PlasmaExpanderArrow { iconSize: expanderLoader.iconSize }", "Item {}")
        source = Path("/usr/share/plasma/plasmoids/org.kde.plasma.systemtray/contents/ui/main.qml").read_text()
        patched = panel_widgets.patch_main(source, True)
        models = patched[patched.index("    KItemModels.KSortFilterProxyModel {\n        id: activeModel"):
                         patched.index("    Instantiator {")]
        # Use production state, filters, arrow and click handler unchanged;
        # replace only the attached model/config with controlled fixtures.
        body = (panel_widgets.TRAY_STATE + models).replace(
            "Plasmoid.systemTrayModel", "fixtureModel").replace(
            "Plasmoid.configuration.hiddenItems", "root.testHiddenItems")
        qml = imports + '''
Item {
    id: root
    width: 20
    height: 24
    property bool xpTrayAvailable: true
    property bool vertical: false
    property var testHiddenItems: ["explicit", "explicit-first", "disabled"]
    readonly property string visibleIds: modelIds(activeModel, activeModel.count)
    readonly property string hiddenIds: modelIds(hiddenModel, hiddenModel.count)
    readonly property int expandableCount: xpExpandableModel.count
    function i18n(s) { return s; }
    function modelIds(model, count) {
        let ids = [];
        for (let i = 0; i < count; i++) ids.push(model.data(model.index(i, 0), Qt.UserRole + 2));
        return ids.join(",");
    }
''' + body + loader + "\n}\n"
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "TrayClick.qml"
            path.write_text(qml)
            view = QQuickView()
            model = TrayModel()
            view.rootContext().setContextProperty("fixtureModel", model)
            view.setSource(QUrl.fromLocalFile(str(path)))
            self.assertEqual(view.status(), QQuickView.Ready, [e.toString() for e in view.errors()])
            view.show()
            QTest.qWait(50)
            root = view.rootObject()
            self.assertEqual(root.property("visibleIds"), "always-shown,auto-active")
            collapsed = root.property("visibleIds").split(",")
            self.assertEqual(root.property("expandableCount"), 2)
            QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(10, 10))
            QTest.qWait(20)
            self.assertTrue(root.property("xpTrayExpanded"))
            self.assertEqual(root.property("visibleIds"), "explicit-first,explicit,always-shown,auto-active")
            expanded = root.property("visibleIds").split(",")
            # Each existing icon keeps its distance from the tray's right edge.
            for item in collapsed:
                self.assertEqual(len(collapsed) - collapsed.index(item),
                                 len(expanded) - expanded.index(item))
            self.assertEqual(root.property("hiddenIds"), "auto-inactive")
            self.assertEqual(root.property("testHiddenItems").toVariant(), ["explicit", "explicit-first", "disabled"])
            QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(10, 10))
            QTest.qWait(20)
            self.assertFalse(root.property("xpTrayExpanded"))
            self.assertEqual(root.property("visibleIds"), "always-shown,auto-active")
            self.assertEqual(root.property("hiddenIds"), "explicit-first,explicit,auto-inactive")
            root.setProperty("testHiddenItems", [])
            QTest.qWait(20)
            self.assertEqual(root.property("expandableCount"), 0)
            view.close()


if __name__ == "__main__":
    unittest.main()
