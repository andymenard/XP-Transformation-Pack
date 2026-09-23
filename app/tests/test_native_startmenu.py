"""Native KDE QML integration. All action tests are dry-run; no desktop writes."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")
os.environ.setdefault("QT_QPA_PLATFORMTHEME", "generic")
os.environ.setdefault("XDG_MENU_PREFIX", "plasma-")
from PySide6.QtCore import QUrl, QObject, QPoint, Qt, QAbstractListModel, QModelIndex, Property, Slot, qInstallMessageHandler
from PySide6.QtGui import QGuiApplication
from PySide6.QtQuick import QQuickView
from PySide6.QtTest import QTest

REPO = Path(__file__).resolve().parents[1]
app = QGuiApplication.instance() or QGuiApplication([])
qInstallMessageHandler(lambda kind, context, message: print(message, file=sys.stderr))


class CatalogFixture(QAbstractListModel):
    """Deterministic category tree; never launches applications."""
    def __init__(self, rows, description=""):
        super().__init__()
        self.rows = rows
        self._description = description

    @Property(int, constant=True)
    def count(self):
        return len(self.rows)

    @Property(str, constant=True)
    def description(self):
        return self._description

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.rows)

    def roleNames(self):
        return {0: b"display", 1: b"decoration", 257: b"favoriteId", 258: b"isSeparator"}

    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid():
            return None
        label, identity, child = self.rows[index.row()]
        return {0: label, 1: "folder" if child else "application-x-executable",
                257: identity, 258: False}.get(role)

    @Slot(int, result=QObject)
    def modelForRow(self, row):
        return self.rows[row][2] if 0 <= row < len(self.rows) else None


class NativeStartTests(unittest.TestCase):
    def test_installed_package_loads_native_models_and_dry_run_actions(self):
        with tempfile.TemporaryDirectory() as out:
            code = '''import start_applet
from msstyles_parse import MsStyle
start_applet.USER_PLASMOIDS = %r
start_applet.install_start_applet(3, 4, MsStyle.load('iso/cache/luna.msstyles', 'NORMALBLUE_INI'))
''' % out
            subprocess.run([str(REPO / ".venv/bin/python3"), "-c", code], cwd=REPO, check=True)
            ui = Path(out) / "org.xp2plasma.start/contents/ui"
            source = (ui / "main.qml").read_text()
            self.assertIn("fullRepresentation: XpFullRepresentation", source)
            self.assertIn("appNameFormat: 0 // NameOnly for the XP menu", source)
            self.assertNotIn(".new/", (ui / "XpTheme.js").read_text())
            # The native Kickoff singleton needs a real PlasmoidItem host. This
            # harness exercises only our independent components and models.
            (ui / "qmldir").unlink()
            # RecentUsageModel expects Kickoff's activity-aware favorites, not
            # this harness's SimpleFavoritesModel. Use deterministic recents
            # here: querying real usage with the stub can crash inside Kicker.
            backend_path = ui / "XpBackend.qml"
            backend_source = backend_path.read_text()
            start = backend_source.index("    Kicker.RecentUsageModel {\n        id: usage")
            end = backend_source.index("    Kicker.RecentUsageModel { id: documents", start)
            backend_path.write_text(backend_source[:start] + "    property var usage: fixtureUsage\n" + backend_source[end:])
            usage = CatalogFixture([(f"Recent {i}", f"recent-{i}.desktop", None) for i in range(10)])
            harness = ui / "Harness.qml"
            harness.write_text('''import QtQuick
import org.kde.plasma.private.kicker as Kicker
Item {
    width: 380; height: 478
    Kicker.SimpleFavoritesModel { id: favorites; enabled: true; maxFavorites: 8; favorites: ["applications:systemsettings.desktop", "applications:org.kde.dolphin.desktop", "applications:org.kde.konsole.desktop"] }
    readonly property int favoriteCount: favorites.count
    readonly property string firstFavorite: favorites.count ? String(favorites.data(favorites.index(0, 0), Qt.DisplayRole)) : "empty"
    Kicker.RootModel {
        id: appCatalog
        autoPopulate: true
        flat: true
        showTopLevelItems: true
        showAllApps: true
        showFavoritesPlaceholder: true
    }
    function catalogInfo() {
        let result = [];
        for (let i = 0; i < appCatalog.count; i++) {
            let child = appCatalog.modelForRow(i);
            result.push([i, appCatalog.labelForRow(i), child ? child.description : "null", child ? child.count : -1]);
        }
        return JSON.stringify(result);
    }
    QtObject {
        id: model
        property var favoritesModel: favorites
        property int count: appCatalog.count
        signal refreshed()
        function refresh() { appCatalog.refresh(); refreshed(); }
        function modelForRow(row) { return appCatalog.modelForRow(row); }
    }
    QtObject {
        id: fixtureLauncher
        property var rootModel: model
        property bool expanded: true
        property bool hideOnWindowDeactivate: true
    }
    XpFullRepresentation { launcher: fixtureLauncher }
    XpBackend {
        id: backend
        objectName: "testBackend"
        launcher: fixtureLauncher
        dryRun: true
        property var recorded: []
        property int pinnedCount: pinned.length
        property string pinnedIds: JSON.stringify(pinned.map(entry => entry.identity))
        property string recentIds: JSON.stringify(recent.map(entry => entry.identity))
        property int allAppsCount: allApps ? allApps.count : -1
        property string placeLabels: JSON.stringify(places.map(group => group.map(entry => entry.label)))
        property string recentFilesIcon: places[0][1].icon
        onActionDispatched: action => recorded = recorded.concat([action])
        function exerciseActions() {
            activate("shutdown"); activate("logoff"); activate("run"); activate("all-programs");
            activate(places[0][0].action);
        }
        function moveFavorite() { favorites.moveRow(2, 0); }
        function removeFavorite() { favorites.removeFavorite("applications:org.kde.dolphin.desktop"); }
        function addFavorite() { favorites.addFavorite("applications:org.kde.dolphin.desktop"); }
        function clearFavorites() { favorites.favorites = []; }
    }
}
''')
            view = QQuickView()
            view.rootContext().setContextProperty("fixtureUsage", usage)
            view.setSource(QUrl.fromLocalFile(str(harness)))
            self.assertEqual(view.status(), QQuickView.Ready, [e.toString() for e in view.errors()])
            view.show()
            QTest.qWait(250)
            backend = view.rootObject().findChild(QObject, "testBackend")
            backend.refresh()
            self.assertEqual(backend.property("pinnedCount"), 3,
                (view.rootObject().property("favoriteCount"), view.rootObject().property("firstFavorite")))
            import json
            self.assertEqual(json.loads(backend.property("placeLabels")), [
                ["Documents", "Recent Files", "Pictures", "Music", "File System"],
                ["System Settings", "Default Applications", "Printers"],
                ["Help Center", "Search", "Run Command..."]])
            self.assertEqual(backend.property("recentFilesIcon"), "folder-recent-files")
            original = json.loads(backend.property("pinnedIds"))
            self.assertEqual(backend.property("recentLimit"), 5)
            self.assertEqual(len(json.loads(backend.property("recentIds"))), 5)
            self.assertFalse(set(original) & set(json.loads(backend.property("recentIds"))))
            backend.moveFavorite()
            QTest.qWait(50)
            self.assertEqual(json.loads(backend.property("pinnedIds")), [original[2], original[0], original[1]])
            backend.removeFavorite()
            QTest.qWait(50)
            self.assertEqual(backend.property("pinnedCount"), 2)
            self.assertEqual(backend.property("recentLimit"), 6)
            self.assertEqual(len(json.loads(backend.property("recentIds"))), 6)
            backend.addFavorite()
            QTest.qWait(50)
            self.assertEqual(backend.property("pinnedCount"), 3)
            backend.clearFavorites()
            QTest.qWait(50)
            self.assertEqual(backend.property("pinnedCount"), 0)
            self.assertEqual(backend.property("recentLimit"), 8)
            self.assertEqual(len(json.loads(backend.property("recentIds"))), 8)
            # Without a PlasmoidItem appletInterface KDE exposes the catalog
            # model but can leave it empty. Verify selection here; populated
            # applet catalog/interaction is checked in the live launcher.
            self.assertGreaterEqual(backend.property("allAppsCount"), 0, view.rootObject().catalogInfo())
            backend.exerciseActions()
            recorded = backend.property("recorded").toVariant()
            self.assertEqual(recorded[:4], ["shutdown", "logoff", "run", "all-programs"])
            self.assertEqual(len(recorded), 5, recorded)
            self.assertTrue(recorded[4]["url"].startswith("file:///"))
            self.assertFalse(view.grabWindow().isNull())
            view.close()

            internet = CatalogFixture([("Discord", "discord.desktop", None)])
            utilities = CatalogFixture([("Terminal", "terminal.desktop", None)])
            synthetic = CatalogFixture([], "KICKER_ALL_MODEL")
            catalog = CatalogFixture([("All Applications", "all", synthetic),
                                      ("Internet", "internet", internet),
                                      ("Utilities", "utilities", utilities)])
            harness = ui / "PopupHarness.qml"
            harness.write_text('''import QtQuick
import "XpTheme.js" as Theme
Item {
    width: 380; height: 478
    property string selectedIdentity: ""
    property int dismissCount: 0
    property var popupWindow: popup
    Rectangle { id: anchor; x: 0; y: 400; width: 190; height: 30 }
    XpProgramsPopup {
        id: popup
        // Regression: a white main-panel label must not leak into flyouts.
        themeData: Object.assign({}, Theme.data, {
            palette: Object.assign({}, Theme.data.palette, {programText: "#ffffff"})
        })
        visualParent: anchor
        sourceModel: fixtureCatalog
        categoriesOnly: true
        onActivated: action => selectedIdentity = action.identity
        onDismissRequested: dismissCount++
    }
    function openPopup() { popup.visible = true; }
    function closePopup() { popup.visible = false; }
    function labels() { return JSON.stringify(popup.rows.map(row => row.label)); }
    function openFolder(index) { popup.activateRow(index, true); }
    function chooseChild() { popup.childPopup.activateRow(0, false); }
    function backChild() { popup.childPopup.back(); }
    function isFolderSelected(index) {
        return popup.mainItem.children.find(item => item.objectName === "programEntries")
            .itemAtIndex(index).selected;
    }
    function labelColor(index) {
        const list = popup.mainItem.children.find(item => item.objectName === "programEntries");
        return String(list.itemAtIndex(index).children.find(item => item.objectName === "programLabel").color);
    }
}
''')
            flyout_view = QQuickView()
            flyout_view.rootContext().setContextProperty("fixtureCatalog", catalog)
            flyout_view.setSource(QUrl.fromLocalFile(str(harness)))
            self.assertEqual(flyout_view.status(), QQuickView.Ready,
                             [e.toString() for e in flyout_view.errors()])
            flyout_view.show()
            root = flyout_view.rootObject()
            root.openPopup()
            QTest.qWait(80)
            popup = root.property("popupWindow")
            self.assertEqual(json.loads(root.labels()), ["Internet", "Utilities"])
            self.assertEqual(root.labelColor(1), "#000000")
            QTest.mouseMove(popup, QPoint(50, 38))
            QTest.qWait(120)
            self.assertIsNone(popup.property("childPopup"))
            QTest.qWait(350)
            child = popup.property("childPopup")
            self.assertIsNotNone(child)
            self.assertTrue(child.isVisible())
            self.assertEqual(child.property("title"), "Internet")
            self.assertEqual(root.labelColor(0), "#ffffff")
            QTest.mouseMove(child, QPoint(50, 38))
            self.assertTrue(root.isFolderSelected(0))
            root.chooseChild()
            self.assertEqual(root.property("selectedIdentity"), "discord.desktop")
            root.backChild()
            self.assertFalse(child.isVisible())
            root.openFolder(0)
            QTest.qWait(20)
            self.assertTrue(popup.property("childPopup").isVisible())
            # Click switches folders immediately, without the hover delay.
            QTest.mouseClick(popup, Qt.LeftButton, Qt.NoModifier, QPoint(50, 62))
            self.assertEqual(popup.property("childPopup").property("title"), "Utilities")
            root.chooseChild()
            self.assertEqual(root.property("selectedIdentity"), "terminal.desktop")
            root.closePopup()
            self.assertIsNone(popup.property("childPopup"))
            root.openPopup()
            QTest.qWait(30)
            popup.back(True)
            QTest.qWait(30)
            self.assertFalse(popup.isVisible())
            self.assertTrue(flyout_view.isVisible())
            # Returning focus to the main window must not dismiss Start.
            # Qt may report transient ancestors active as well, so hovering
            # or clicking a main-menu entry handles the final branch closure.
            root.openPopup()
            QTest.qWait(30)
            root.openFolder(0)
            self.assertIsNotNone(popup.property("childPopup"))
            flyout_view.requestActivate()
            QTest.qWait(30)
            child = popup.property("childPopup")
            if child:  # offscreen backends do not always emit WindowDeactivate
                child.handleDeactivation()
            self.assertEqual(root.property("dismissCount"), 0)
            self.assertTrue(flyout_view.isVisible())
            root.closePopup()
            # Pointer leaving the entire button/flyout family closes it, but
            # entering a descendant or returning during the grace period does not.
            root.openPopup()
            QTest.qWait(30)
            QTest.mouseMove(popup, QPoint(50, 38))
            QTest.qWait(450)
            child = popup.property("childPopup")
            self.assertIsNotNone(child)
            QTest.mouseMove(popup, QPoint(-20, -20))
            QTest.mouseMove(child, QPoint(50, 38))
            QTest.qWait(450)
            self.assertTrue(popup.isVisible())
            self.assertTrue(popup.property("containsPointer"))
            QTest.mouseMove(child, QPoint(-20, -20))
            QTest.qWait(100)
            self.assertTrue(popup.isVisible())
            QTest.mouseMove(child, QPoint(50, 38))
            QTest.qWait(450)
            self.assertTrue(popup.isVisible())
            QTest.mouseMove(child, QPoint(-20, -20))
            QTest.qWait(450)
            self.assertFalse(popup.isVisible())
            self.assertIsNone(popup.property("childPopup"))
            self.assertEqual(root.property("dismissCount"), 0)
            self.assertTrue(flyout_view.isVisible())
            self.assertFalse(popup.property("visualParent").hasActiveFocus())
            # Recent Files is not a category tree, but needs the same close
            # behavior. Staying over its anchor must keep even an empty list open.
            popup.setProperty("categoriesOnly", False)
            popup.setProperty("title", "Recent Files")
            popup.setProperty("sourceModel", internet)
            popup.setProperty("anchorHovered", True)
            root.openPopup()
            QTest.qWait(450)
            self.assertTrue(popup.isVisible())
            popup.setProperty("anchorHovered", False)
            QTest.mouseMove(popup, QPoint(50, 38))
            QTest.qWait(450)
            self.assertTrue(popup.isVisible())
            QTest.mouseMove(popup, QPoint(-20, -20))
            QTest.qWait(450)
            self.assertFalse(popup.isVisible())
            self.assertEqual(root.property("dismissCount"), 0)
            # Opening by keyboard alone must not start a pointer-leave timeout.
            root.openPopup()
            QTest.qWait(450)
            self.assertTrue(popup.isVisible())
            root.closePopup()
            flyout_view.close()


if __name__ == "__main__":
    unittest.main()
