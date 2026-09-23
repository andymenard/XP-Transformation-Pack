"""Themes/Appearance boundaries and saved preset round trips; no live applies."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import fixtures  # noqa: E402
from PIL import Image
from PySide6.QtWidgets import QApplication
import appearance_editor as ae
import classic_gen as cg
import theme_file as tf
import theme_index as tx
import theme_presets as presets
import xplasma as xp

app = QApplication.instance() or QApplication([])
STYLE = fixtures.LUNA


class ThemeSplitTests(unittest.TestCase):
    def test_index_keeps_two_presets_for_one_style_and_no_bare_styles(self):
        with tempfile.TemporaryDirectory() as out:
            first, second = Path(out) / "One.theme", Path(out) / "Two.theme"
            for file in (first, second):
                file.write_text(f"[Theme]\nDisplayName={file.stem}\n[VisualStyles]\nPath={STYLE}\n")
            with patch.object(tx, "_iter_files", return_value=[str(STYLE), str(first), str(second), str(first)]):
                entries = tx.list_themes()
            self.assertEqual([e.label for e in entries], ["One", "Two"])
            self.assertTrue(all(e.style_path == str(STYLE) for e in entries))
            with patch.object(tx, "find_theme_file_for", return_value=str(first)):
                self.assertIsNone(tx.entry_from_path(str(STYLE)).theme_file)

    def test_saved_visual_style_round_trips_variant_wallpaper_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as out, patch.object(presets.paths, "THEMES", out):
            image = Path(out) / "background.png"
            Image.new("RGB", (4, 4), "red").save(image)
            style = tx.visual_style_from(str(STYLE))
            desktop = {"image": str(image), "fill": 1, "color": (1, 2, 3)}
            path = presets.save("My Theme", style, "METALLIC", "LARGEFONTS", cg.Scheme(), desktop)
            entry = tx.entry_from_path(path)
            self.assertEqual(entry.label, "My Theme")
            self.assertEqual(entry.style_path, str(STYLE))
            self.assertEqual(tx.bundle_variant(entry, style), "LARGEFONTSMETALLIC_INI")
            self.assertEqual(tf.theme_wallpaper_path(path), str(image))
            self.assertEqual(tf.theme_wallpaper_fill_mode(path), 1)
            self.assertTrue(presets.owned(path))
            before = Path(path).read_bytes()
            with self.assertRaises(FileExistsError):
                presets.save("My Theme", style, "BLUE", "NORMAL", cg.Scheme(), desktop)
            self.assertEqual(Path(path).read_bytes(), before)

    def test_classic_preset_keeps_colors_fonts_metrics_and_solid_background(self):
        with tempfile.TemporaryDirectory() as out, patch.object(presets.paths, "THEMES", out):
            scheme = cg.load_scheme(scheme_name="Rainy Day")
            scheme.menu, scheme.scroll, scheme.icon_h, scheme.icon_v = 23, 19, 80, 90
            desktop = {"image": None, "fill": 6, "color": scheme.c("background")}
            path = presets.save("Rainy Custom", tx.VisualStyle("Classic", None), "", "NORMAL", scheme, desktop)
            loaded = cg.load_scheme(path)
            self.assertEqual(cg.scheme_to_entry(loaded), cg.scheme_to_entry(scheme))
            self.assertIsNone(tf.theme_msstyles_path(path))
            self.assertEqual(tf.load_theme_file(path)["control panel\\desktop"]["wallpaper"], "")

    def test_desktop_capture_reads_image_and_rejects_unrepresentable_plugin(self):
        with tempfile.TemporaryDirectory() as out:
            image = Path(out) / "wall paper.png"
            image.touch()
            config = ("[Containments][2]\nplugin=org.kde.desktop\nwallpaperplugin=org.kde.image\n"
                      "[Containments][2][Wallpaper][org.kde.image][General]\n"
                      f"Image={image.as_uri()}\nFillMode=1\nColor=#123456\n")
            with patch.object(presets.host, "read_text", return_value=config):
                self.assertEqual(presets.current_desktop(), {"image": str(image), "fill": 1, "color": (18, 52, 86)})
            with patch.object(presets.host, "read_text", return_value=config.replace("org.kde.image", "custom.wallpaper")):
                with self.assertRaises(ValueError):
                    presets.current_desktop()

    def test_appearance_jobs_do_not_attach_a_bundle(self):
        with patch.object(presets, "current_desktop", return_value={"image": None, "fill": 6, "color": (1, 2, 3)}):
            window = ae.AppearanceEditor()
        try:
            index = next(i for i, s in enumerate(window._styles) if s.path)
            window.style_combo.setCurrentIndex(index)
            window._on_style_changed(index)
            self.assertEqual(window._current_row().kind, "current")
            self.assertEqual(window._theme_info.desktop, (1, 2, 3))
            with patch.object(xp, "apply_msstyles_live") as apply_style:
                window._job_for_appearance().run(lambda _: None)
                self.assertIsNone(apply_style.call_args.kwargs["theme_file"])
                self.assertFalse(apply_style.call_args.kwargs["apply_icons"])
            window.style_combo.setCurrentIndex(0)
            window._on_style_changed(0)
            with patch.object(xp, "apply_scheme_live") as apply_classic:
                window._job_for_appearance().run(lambda _: None)
                self.assertTrue(apply_classic.call_args.kwargs["preserve_wallpaper"])
                self.assertFalse(apply_classic.call_args.kwargs["apply_icons"])
        finally:
            window.close()

    def test_classic_pipeline_preserves_wallpaper_when_requested(self):
        with tempfile.TemporaryDirectory() as out, \
                patch.object(cg, "write_aurorae_package", return_value=out), \
                patch.object(cg, "build_colorscheme", return_value=""), \
                patch.object(xp, "apply_font_specs"), patch.object(xp, "xp_icons"), \
                patch.object(xp, "applied_state"), \
                patch.multiple(xp.apply_mod, install_aurorae_theme=Mock(), install_colorscheme=Mock(),
                               apply_aurorae_theme=Mock(), apply_colorscheme=Mock(),
                               apply_wallpaper=Mock(), apply_wallpaper_color=Mock()):
            xp.apply_scheme_live(cg.Scheme(), "Test", out_dir=out, panel=False, widgets=False,
                                 preserve_wallpaper=True, progress=lambda _: None)
            xp.apply_mod.apply_wallpaper.assert_not_called()
            xp.apply_mod.apply_wallpaper_color.assert_not_called()

    def test_editor_save_as_creates_preset_and_delete_is_recoverable(self):
        desktop = {"image": None, "fill": 6, "color": (15, 25, 35)}
        with tempfile.TemporaryDirectory() as out, patch.object(presets.paths, "THEMES", out), \
                patch.object(presets, "current_desktop", return_value=desktop):
            window = ae.AppearanceEditor()
            try:
                index = next(i for i, s in enumerate(window._styles) if s.path)
                window.style_combo.setCurrentIndex(index)
                window._on_style_changed(index)
                with patch.object(ae.QInputDialog, "getText", return_value=("Saved UI Theme", True)), \
                        patch.object(ae.QMessageBox, "warning") as warning:
                    window._save_as()
                    warning.assert_not_called()
                row = window._current_row()
                self.assertEqual(row.kind, "file")
                self.assertEqual(row.label, "Saved UI Theme")
                saved = Path(row.entry.theme_file)
                self.assertTrue(saved.exists())
                self.assertEqual(tf.load_theme_file(str(saved))["control panel\\colors"]["background"], "15 25 35")
                with patch.object(xp, "apply_msstyles_live") as apply_style:
                    window._job_for_themes().run(lambda _: None)
                    self.assertEqual(apply_style.call_args.kwargs["theme_file"], str(saved))
                with patch.object(ae.QMessageBox, "question", return_value=ae.QMessageBox.Yes):
                    window._delete_theme()
                self.assertFalse(saved.exists())
                self.assertTrue(Path(str(saved) + ".deleted").exists())
                self.assertEqual(window._current_row().kind, "current")
            finally:
                window.close()


if __name__ == "__main__":
    unittest.main()
