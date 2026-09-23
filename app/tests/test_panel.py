"""XP panel sizing, artwork and managed native-widget presentation."""
from pathlib import Path
import json
import sys
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fixtures  # noqa: E402
from msstyles_parse import MsStyle
import panel_layout
import panel_widgets as widgets
import plasmatheme_gen as gen
from PIL import Image


class PanelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = str(fixtures.LUNA)
        if not fixtures.LUNA.exists():
            raise unittest.SkipTest("Luna is not in the corpus")
        cls.theme = MsStyle.load(cls.path, "NORMALBLUE_INI")

    def test_panel_height_is_not_bitmap_height(self):
        self.assertEqual(gen.build_panel_background(self.theme)[1], 28)
        with tempfile.TemporaryDirectory() as out:
            pkg, height = gen.generate_plasma_theme(self.theme, out, "Windows XP")
            self.assertEqual(height, 30)
            self.assertTrue((Path(pkg) / "widgets/xp-tray.svg").exists())
            self.assertEqual(gen.generate_plasma_theme(self.theme, out, "Windows XP", 40)[1], 40)

    def test_start_font_and_all_state_sizes(self):
        font = gen.start_font(self.theme)
        self.assertEqual(font.size, 19)
        self.assertEqual(font.getname(), ("Franklin Gothic Medium", "Italic"))
        with tempfile.TemporaryDirectory() as out:
            normal = gen.build_start_button_pngs(self.theme, out, "start", 30)
            sizes = []
            for state in ("normal", "hover", "pressed"):
                with Image.open(normal.replace("-normal.png", f"-{state}.png")) as im:
                    sizes.append(im.size)
            self.assertEqual(len(set(sizes)), 1)
            self.assertEqual(sizes[0][1], 30)

    def test_tray_has_background_and_both_arrow_states(self):
        svg = gen.build_tray_svg(self.theme)
        ids = {node.get("id") for node in ET.fromstring(svg).iter()}
        for name in ("tray-center", "tray-left", "clock-center"):
            self.assertIn(name, ids)
        for direction in ("expand", "collapse"):
            for state in ("normal", "hover", "pressed"):
                self.assertIn(f"{direction}-{state}", ids)
        silver = MsStyle.load(self.path, "NORMALMETALLIC_INI")
        self.assertNotEqual(svg, gen.build_tray_svg(silver))

    def test_tray_preserves_theme_content_inset_without_padding_clock(self):
        for ini in ("NORMALBLUE_INI", "NORMALMETALLIC_INI", "NORMALHOMESTEAD_INI"):
            theme = MsStyle.load(self.path, ini)
            elements = {node.get("id"): node for node in ET.fromstring(gen.build_tray_svg(theme)).iter()}
            expected = int(theme.effective("traynotifyhoriz::traynotify", "background")["contentmargins"].split(",")[0])
            self.assertEqual(float(elements["tray-hint-left-margin"].get("width")), expected)
            # Existing zero-size hints use a 1px transparent image.
            self.assertEqual(float(elements["clock-hint-left-margin"].get("width")), 1)

    def test_clock_uses_fixed_font_without_touching_timezones(self):
        with patch.object(panel_layout, "_script") as script:
            panel_layout.apply_clock_font()
        js = script.call_args.args[0]
        self.assertIn("'autoFontAndSize', false", js)
        self.assertIn("'fontSize', 8", js)
        self.assertIn("'fontWeight', 400", js)
        self.assertNotIn("selectedTimeZones", js)

    def test_clock_does_not_scale_with_physical_monitor_density(self):
        source = "const pixelsPerInch = Screen.pixelDensity * 25.4"
        self.assertIn("root.xpTrayAvailable ? 96 :", widgets.patch_clock(source))

    def test_host_widgets_patch_and_unknown_layout_fails_closed(self):
        for plugin in widgets.IDS:
            source = Path("/usr/share/plasma/plasmoids") / plugin / "contents/ui/main.qml"
            if not source.exists():
                self.skipTest("Native Plasma widgets not installed")
            result = widgets.patch_main(source.read_text(), plugin == widgets.IDS[0])
            self.assertEqual(result.count("id: xpTraySvg"), 1)
            self.assertIn("Plasmoid.formFactor === PlasmaCore.Types.Horizontal", result)
            if plugin == widgets.IDS[0]:
                self.assertIn("model: activeModel", result)
                self.assertIn("xpExpandableModel.count > 0", result)
                self.assertIn("Layout.leftMargin: root.xpTrayAvailable && !root.vertical && !expander.visible", result)
                self.assertIn("? xpTrayBackground.margins.left : 0", result)
                self.assertNotIn("Plasmoid.configuration.showAllItems", result)
        with self.assertRaises(RuntimeError):
            widgets.patch_main("unknown upstream version", True)

    def test_removal_preserves_foreign_overrides(self):
        with tempfile.TemporaryDirectory() as out:
            root = Path(out)
            ours, foreign = [root / plugin for plugin in widgets.IDS]
            ours.mkdir()
            foreign.mkdir()
            (ours / "metadata.json").write_text(json.dumps({widgets.MARKER: True}))
            (foreign / "metadata.json").write_text('{"user": true}')
            with patch.object(widgets, "ROOT", root):
                widgets.remove()
            self.assertFalse(ours.exists())
            self.assertTrue(foreign.exists())

    def test_tray_shadow_is_scoped_to_visible_xp_icons(self):
        path = Path("/usr/share/plasma/plasmoids") / widgets.IDS[0] / "contents/ui/items/AbstractItem.qml"
        if not path.exists():
            self.skipTest("Native Plasma widgets not installed")
        result = widgets.patch_tray_item(path.read_text())
        self.assertIn("import QtQuick.Effects", result)
        self.assertEqual(result.count("layer.effect: MultiEffect"), 1)
        self.assertIn("layer.enabled: root.xpTrayAvailable && !root.vertical && abstractItem.inVisibleLayout", result)
        self.assertIn("layer.smooth: false", result)
        self.assertIn("shadowVerticalOffset: 1", result)
        self.assertIn("shadowOpacity: 0.35", result)
        self.assertIn("root.xpRevealHidden(itemId)", result)
        with self.assertRaises(RuntimeError):
            widgets.patch_tray_item("unknown upstream version")

    def test_install_is_repeatable_and_keeps_stock_fallback(self):
        if not Path("/usr/share/plasma/plasmoids", widgets.IDS[0]).exists():
            self.skipTest("Native Plasma widgets not installed")
        with tempfile.TemporaryDirectory() as out:
            root = Path(out)
            with patch.object(widgets, "ROOT", root):
                widgets.install()
                first = (root / widgets.IDS[0] / "contents/ui/main.qml").read_text()
                widgets.install()
                self.assertEqual(first, (root / widgets.IDS[0] / "contents/ui/main.qml").read_text())
                self.assertTrue((root / widgets.IDS[0] / "contents/ui/PlasmaExpanderArrow.qml").exists())
                self.assertIn(widgets.TRAY_ICON_SHADOW,
                              (root / widgets.IDS[0] / "contents/ui/items/AbstractItem.qml").read_text())
                self.assertIn("root.xpTrayAvailable ? 96", (root / widgets.IDS[1] / "contents/ui/DigitalClock.qml").read_text())
                widgets.remove()
                self.assertEqual(list(root.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
