"""Start menu prototype: source-derived geometry, original pixels, QML layout."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "prototypes/start_menu"))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
os.environ.setdefault("QT_QUICK_BACKEND", "software")
os.environ.setdefault("QT_QPA_PLATFORMTHEME", "generic")
from assets import MsStyle, export, numbers
from PySide6.QtCore import QObject, QPoint, Qt
from PySide6.QtGui import QGuiApplication
from PySide6.QtTest import QTest
from PIL import Image
from preview import create_view

app = QGuiApplication.instance() or QGuiApplication([])


class StartMenuTests(unittest.TestCase):
    def test_eight_slots_fit_with_every_pinned_recent_split(self):
        theme = MsStyle.load(str(REPO / "iso/cache/luna.msstyles"), "NORMALBLUE_INI")
        with tempfile.TemporaryDirectory() as out:
            view = create_view(export(theme, out))
            root = view.rootObject()
            view.show()
            for pins in range(9):
                with self.subTest(pins=pins):
                    root.setProperty("pinnedItems", [{"label": f"Pin {i}", "action": "pin"} for i in range(pins)])
                    root.setProperty("recentItems", [{"label": f"Recent {i}", "action": "recent"} for i in range(8 - pins)])
                    QTest.qWait(30)
                    scroll = root.findChild(QObject, "programScroll")
                    self.assertLessEqual(scroll.property("contentHeight"), scroll.property("height"))
                    self.assertEqual((root.width(), root.height()), (380, 478))
            view.close()

    def test_all_programs_hover_delay_click_and_open_highlight(self):
        theme = MsStyle.load(str(REPO / "iso/cache/luna.msstyles"), "NORMALBLUE_INI")
        with tempfile.TemporaryDirectory() as out:
            view = create_view(export(theme, out))
            root = view.rootObject()
            actions = []
            root.requested.connect(actions.append)
            view.show()
            QTest.qWait(40)
            self.assertEqual(root.property("menuOpenDelay"), 400)
            QTest.mouseMove(view, QPoint(90, 415))
            QTest.qWait(150)
            self.assertEqual(actions, [])
            QTest.qWait(300)
            self.assertEqual(actions, ["all-programs"])
            root.setProperty("allProgramsOpen", True)
            QTest.mouseMove(view, QPoint(300, 85))
            QTest.qWait(30)
            highlight = root.findChild(QObject, "allProgramsHighlight")
            self.assertTrue(highlight.property("visible"))
            root.setProperty("allProgramsOpen", False)
            self.assertFalse(highlight.property("visible"))
            actions.clear()
            QTest.mouseMove(view, QPoint(90, 415))
            QTest.qWait(80)
            QTest.mouseMove(view, QPoint(300, 85))
            QTest.qWait(400)
            self.assertEqual(actions, [])
            QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(90, 415))
            self.assertEqual(actions, ["all-programs"])
            QTest.qWait(450)
            self.assertEqual(actions, ["all-programs"])
            view.close()

    def test_all_programs_closes_independently(self):
        theme = MsStyle.load(str(REPO / "iso/cache/luna.msstyles"), "NORMALBLUE_INI")
        with tempfile.TemporaryDirectory() as out:
            view = create_view(export(theme, out))
            root = view.rootObject()
            actions, closes = [], []
            root.requested.connect(actions.append)
            def close_programs():
                closes.append(True)
                root.setProperty("allProgramsOpen", False)
            root.closeProgramsRequested.connect(close_programs)
            view.show()
            QTest.qWait(40)
            root.setProperty("allProgramsOpen", True)
            QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(90, 415))
            self.assertEqual(len(closes), 1)
            self.assertEqual(actions, [])
            # Hovering another main-menu entry closes only the flyout, after
            # the same delay; passing across it briefly leaves the flyout open.
            root.setProperty("allProgramsOpen", True)
            QTest.mouseMove(view, QPoint(300, 85))
            QTest.qWait(100)
            QTest.mouseMove(view, QPoint(90, 415))
            QTest.qWait(350)
            self.assertEqual(len(closes), 1)
            QTest.mouseMove(view, QPoint(300, 85))
            QTest.qWait(450)
            self.assertEqual(len(closes), 2)
            root.setProperty("allProgramsOpen", True)
            root.forceActiveFocus()
            QTest.keyClick(view, Qt.Key_Escape)
            self.assertEqual(len(closes), 3)
            self.assertEqual(actions, [])
            self.assertTrue(view.isVisible())
            view.close()

    def test_recent_files_hover_click_and_highlight(self):
        theme = MsStyle.load(str(REPO / "iso/cache/luna.msstyles"), "NORMALBLUE_INI")
        with tempfile.TemporaryDirectory() as out:
            view = create_view(export(theme, out))
            root = view.rootObject()
            actions, closes = [], []
            root.requested.connect(actions.append)
            def close_recent():
                closes.append(True)
                root.setProperty("recentFilesOpen", False)
                root.forceActiveFocus(Qt.MouseFocusReason)
            root.closeProgramsRequested.connect(close_recent)
            view.show()
            QTest.qWait(40)
            entry = root.property("recentFilesItem")
            self.assertIsNotNone(entry)
            position = entry.mapToScene(entry.boundingRect().center()).toPoint()
            QTest.mouseMove(view, position)
            QTest.qWait(120)
            self.assertEqual(actions, [])
            QTest.qWait(330)
            self.assertEqual(actions, ["recent-documents"])
            root.setProperty("recentFilesOpen", True)
            QTest.mouseMove(view, QPoint(300, 30))
            self.assertFalse(root.property("recentFilesHovered"))
            self.assertTrue(entry.property("highlighted"))
            close_recent()
            self.assertFalse(entry.property("highlighted"))
            actions.clear()
            QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, position)
            self.assertEqual(actions, ["recent-documents"])
            root.setProperty("recentFilesOpen", True)
            QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, position)
            self.assertEqual(len(closes), 2)
            QTest.mouseMove(view, QPoint(300, 30))
            QTest.qWait(450)
            self.assertEqual(actions, ["recent-documents"])
            self.assertFalse(entry.property("highlighted"))
            view.close()

    def test_all_favorites_remain_reachable_in_fixed_size_menu(self):
        theme = MsStyle.load(str(REPO / "iso/cache/luna.msstyles"), "NORMALBLUE_INI")
        with tempfile.TemporaryDirectory() as out:
            view = create_view(export(theme, out))
            root = view.rootObject()
            root.setProperty("pinnedItems", [{"label": f"Favorite {i}", "action": f"favorite-{i}"} for i in range(12)])
            root.setProperty("recentItems", [])
            view.show()
            QTest.qWait(60)
            scroll = root.findChild(QObject, "programScroll")
            self.assertEqual(scroll.property("contentHeight"), 12 * 38)
            self.assertEqual((root.width(), root.height()), (380, 478))
            maximum = scroll.property("contentHeight") - scroll.property("height")
            scroll.setProperty("contentY", maximum)
            QTest.qWait(30)
            actions = []
            root.requested.connect(actions.append)
            last_y = int(64 + 9 + 11 * 38 - maximum + 19)
            QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(40, last_y))
            self.assertEqual(actions, ["favorite-11"])
            root.resetScroll()
            self.assertEqual(scroll.property("contentY"), 0)
            root.setProperty("pinnedItems", [])
            QTest.qWait(30)
            self.assertEqual(scroll.property("contentHeight"), 0)
            view.close()

    def test_ini_supports_space_and_comma_separated_margins(self):
        self.assertEqual(numbers("6 4 9 5"), [6, 4, 9, 5])
        self.assertEqual(numbers("6, 4, 9, 5"), [6, 4, 9, 5])

    def test_export_retains_original_art_and_provenance(self):
        theme = MsStyle.load(str(REPO / "iso/cache/luna.msstyles"), "NORMALBLUE_INI")
        with tempfile.TemporaryDirectory() as out:
            data = export(theme, out)
            self.assertEqual(data["layout"], {"width": 380, "height": 478, "headerHeight": 64,
                                             "footerHeight": 40, "leftWidth": 190, "moreHeight": 44})
            self.assertEqual(data["parts"]["moreprograms"]["pane"][1], 30)
            self.assertEqual(data["provisional"]["pinnedRowHeight"], 38)
            self.assertEqual(data["provisional"]["recentRowHeight"], 38)
            self.assertIsNone(data["parts"]["userpicture"]["pane"])
            self.assertEqual(data["parts"]["proglist"]["content"], [6, 4, 9, 5])
            self.assertEqual(data["parts"]["logoff"]["sizing"][:2], [48, 48])
            self.assertEqual(data["parts"]["logoff"]["properties"]["sizingmargins"], "49, 47, 0, 38")
            self.assertEqual(data["palette"]["placesText"], "#0a246a")
            self.assertEqual(data["userShadowOffset"], [2, 2])
            with Image.open(Path(out) / "proglist.png") as image:
                self.assertEqual(image.tobytes(), theme.get_background_bitmap("startpanel", "proglist").tobytes())
            with Image.open(Path(out) / "logoff.png") as image:
                self.assertEqual(image.size, (97, 39))
            with Image.open(Path(out) / "button-shutdown.png") as normal, Image.open(Path(out) / "button-shutdown-hot.png") as hot:
                self.assertEqual(normal.size, (24, 24))
                self.assertNotEqual(normal.tobytes(), hot.tobytes())
            self.assertEqual(json.loads((Path(out) / "layout.json").read_text()), data)

    def test_qml_renders_all_luna_variants_and_keeps_actions_inert(self):
        for ini in ("NORMALBLUE_INI", "NORMALMETALLIC_INI", "NORMALHOMESTEAD_INI"):
            with self.subTest(ini=ini), tempfile.TemporaryDirectory() as out:
                theme = MsStyle.load(str(REPO / "iso/cache/luna.msstyles"), ini)
                data = export(theme, out)
                view = create_view(data, username="Layout test")
                view.show()
                QTest.qWait(80)
                root = view.rootObject()
                self.assertEqual((root.width(), root.height()), (380, 478))
                self.assertEqual((view.width(), view.height()), (380, 478))
                expected = {"header": (0, 0, 380, 64), "programs": (0, 64, 190, 330),
                            "places": (190, 64, 190, 374), "allPrograms": (0, 394, 190, 44),
                            "footer": (0, 438, 380, 40)}
                for name, rect in expected.items():
                    part = root.findChild(QObject, name)
                    self.assertEqual(tuple(part.property(k) for k in ("x", "y", "width", "height")), rect)
                separator = root.findChild(QObject, "allProgramsSeparator")
                self.assertEqual((separator.property("x"), separator.property("width")), (9, 172))
                self.assertEqual(separator.property("y"), 2)
                screenshot = view.grabWindow()
                self.assertFalse(screenshot.isNull())
                self.assertEqual(screenshot.pixelColor(10, 455).alpha(), 255)
                actions = []
                root.requested.connect(actions.append)
                QTest.mouseClick(view, Qt.LeftButton, Qt.NoModifier, QPoint(300, 458))
                self.assertEqual(actions, ["shutdown"])
                self.assertTrue(view.isVisible())
                view.close()

    def test_other_installed_official_themes(self):
        library = Path.home() / ".local/share/xp2plasma/assets/themes"
        checked = 0
        for relative in ("Royale/Royale.msstyles", "Royale Noir/Royale Noir.msstyles",
                         "Zune/Zune.msstyles", "Embedded/Embedded.msstyles"):
            source = library / relative
            if not source.exists():
                continue
            with self.subTest(theme=relative), tempfile.TemporaryDirectory() as out:
                theme = MsStyle.load(str(source))
                data = export(theme, out)
                self.assertEqual(data["palette"]["submenuText"], "#000000")
                if relative.startswith("Zune/"):
                    self.assertEqual(data["palette"]["programText"], "#ffffff")
                    self.assertEqual(data["palette"]["submenuHighlightText"], "#000000")
                self.assertEqual(data["parts"]["logoff"]["sizing"],
                                 numbers(theme.effective("startpanel", "logoff")["sizingmargins"]))
                view = create_view(data)
                view.show()
                QTest.qWait(50)
                self.assertFalse(view.grabWindow().isNull())
                view.close()
                checked += 1
        if not checked:
            self.skipTest("No optional official theme packs installed")


if __name__ == "__main__":
    unittest.main()
