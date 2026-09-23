"""Title shadow extraction, scoped renderer override, and preview regressions."""
import dataclasses
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import aurorae_gen as ag
import apply as apply_mod
import preview_gen
import theme_index
import title_shadow as ts
from msstyles_parse import MsStyle


class TitleShadowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.path = str(Path(__file__).resolve().parents[1] / "iso/cache/luna.msstyles")
        cls.theme = MsStyle.load(cls.path, "NORMALBLUE_INI")

    def test_luna_values(self):
        self.assertEqual(ag.caption_shadow(self.theme), ((10, 24, 131), (1, 1)))
        self.assertIsNone(ag.caption_shadow(self.theme, False))
        _, rc = ag.build_decoration_svg(self.theme)
        self.assertEqual(rc["General"]["ActiveTextShadowColor"], "10,24,131,255")
        self.assertEqual(rc["General"]["InactiveTextShadowColor"], "0,0,0,0")

    def test_explicit_none_and_state_colour(self):
        class Theme:
            def effective(self, *args):
                return {"textshadowtype": "None" if args[-1] == "inactive" else "Single",
                        "textshadowcolor": "1 2 3", "textshadowoffset": "-1, 2"}
        self.assertEqual(ag.caption_shadow(Theme()), ((1, 2, 3), (-1, 2)))
        self.assertIsNone(ag.caption_shadow(Theme(), False))

    def test_luna_variant_shadow_colours(self):
        for ini, colour in (("NORMALBLUE_INI", (10, 24, 131)),
                            ("NORMALMETALLIC_INI", (199, 194, 209)),
                            ("NORMALHOMESTEAD_INI", (65, 64, 10))):
            with self.subTest(ini=ini):
                theme = MsStyle.load(self.path, ini)
                self.assertEqual(ag.caption_shadow(theme), (colour, (1, 1)))
                self.assertIsNone(ag.caption_shadow(theme, False))

    def test_different_svg_theme_reloads_shared_shadow_renderer(self):
        with patch.object(ts, "install"), patch.object(apply_mod.time, "sleep"), \
                patch.object(apply_mod.host, "resolve", return_value="qdbus6"), \
                patch.object(apply_mod.host, "run", return_value=SimpleNamespace(
                    stdout="__aurorae__svg__Luna\n")) as run:
            apply_mod.apply_aurorae_theme("WindowsXPstyle")
            libraries = [call.args[0][-1] for call in run.call_args_list
                         if "library" in call.args[0]]
            self.assertEqual(libraries, ["org.kde.breeze", "org.kde.kwin.aurorae"])

    def test_preview(self):
        info = theme_index.info_from_style(self.path, "NORMALBLUE_INI")
        without = dataclasses.replace(info, caption_shadow=None)
        self.assertNotEqual(preview_gen.render_style(info, (340, 240)).tobytes(),
                            preview_gen.render_style(without, (340, 240)).tobytes())
        self.assertIsNone(info.caption_shadow_inactive)

    def test_scoped_install_and_removal(self):
        with tempfile.TemporaryDirectory() as tmp:
            ag.generate_theme_package(self.path, tmp, "Luna", "NORMALBLUE_INI")
            foreign = Path(tmp) / "OtherTheme"
            foreign.mkdir()
            (foreign / "metadata.desktop").write_text("A user theme")
            shadows = ts.theme_shadows(tmp)
            self.assertEqual(list(shadows), ["Luna"])
            self.assertEqual(shadows["Luna"], {"active": "#ff0a1883", "inactive": "#00000000", "x": 1, "y": 1})
            target = Path(tmp) / "override/aurorae.qml"
            source = 'import QtQuick\nDecoration {\n    id: root\n    Text {\n        id: caption\n    }\n}\n'
            with patch.object(ts, "OVERRIDE", target), patch.object(ts.host, "read_text", return_value=source):
                ts.install(tmp)
                result = target.read_text()
                self.assertIn("Upstream.Decoration", result)
                self.assertIn('import "file:///usr/local/share/kwin/aurorae" as Upstream', result)
                self.assertIn("x: caption.x + (spec.x || 0)", result)
                self.assertIn('"Luna":', result)
                ts.install(tmp)  # idempotent; no duplicate glyphs
                self.assertEqual(result, target.read_text())
                ts.remove()
                self.assertFalse(target.exists())
                target.write_text("User-owned override")
                with self.assertRaises(RuntimeError):
                    ts.install(tmp)
                ts.remove()
                self.assertEqual(target.read_text(), "User-owned override")

    def test_unknown_upstream_fails_safely(self):
        with self.assertRaises(RuntimeError):
            ts.patch_qml("unrecognised", "/usr/share/kwin/aurorae", {})


if __name__ == "__main__":
    unittest.main()
