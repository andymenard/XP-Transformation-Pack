"""Installer asset success/failure contracts; no live desktop changes."""
from contextlib import ExitStack
from io import BytesIO
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch, Mock
from urllib.error import URLError

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import install
import setup_assets as assets
import xp_icons
import theme_file
import theme_index


class AssetTests(unittest.TestCase):
    def test_all_loose_presets_import_without_a_name_allowlist(self):
        with tempfile.TemporaryDirectory() as out:
            root = Path(out)
            media = root / "media"
            for relative, content in (
                ("I386/CLASSIC.THE", "[Theme]\nDisplayName=@themeui.dll,-2016\n"),
                ("I386/LUNA.THEME", "[Theme]\nDisplayName=@themeui.dll,-2017\n"),
                ("OEM/Custom.theme", "[Theme]\nDisplayName=Custom preset\n"),
                ("OEM/Other/Custom.Theme", "[Theme]\nDisplayName=Another preset\n"),
                ("OEM/NotATheme.the", "[Something else]\nValue=1\n"),
            ):
                file = media / relative
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text(content)
            with patch.object(assets.paths, "THEMES", str(root / "themes")), \
                    patch.object(assets.paths, "CACHE", str(root / "cache")):
                for _ in range(2):
                    self.assertEqual(assets._import_theme_presets(str(media), lambda _: None), 4)
                presets = list((root / "themes").rglob("*.theme"))
                self.assertEqual(len(presets), 4)
                self.assertEqual({theme_index.theme_label(str(p)) for p in presets},
                                 {"Windows XP", "Windows Classic", "Custom preset", "Another preset"})

    def test_iso_discovers_oem_presets_outside_i386(self):
        import pycdlib
        with tempfile.TemporaryDirectory() as out:
            iso = Path(out) / "custom.iso"
            cd = pycdlib.PyCdlib()
            cd.new()
            cd.add_directory(iso_path="/I386")
            cd.add_directory(iso_path="/OEM")
            content = b"[Theme]\nDisplayName=Custom ISO preset\n"
            stream = BytesIO(content)
            cd.add_fp(stream, len(content), iso_path="/OEM/CUSTOM.THE;1")
            cd.write(str(iso))
            cd.close()
            dest = str(Path(out) / "unpacked")
            self.assertTrue(assets._extract_iso_pycdlib(str(iso), dest, lambda _: None))
            self.assertEqual((Path(dest) / "OEM/CUSTOM.THE").read_bytes(), content)

    def test_fallback_extractors_include_all_presets(self):
        with tempfile.TemporaryDirectory() as out:
            for tool in ("7z", "bsdtar"):
                with patch.object(assets, "_extract_iso_pycdlib", return_value=False), \
                        patch.object(assets.host, "have", side_effect=lambda name: name == tool), \
                        patch.object(assets.host, "run", return_value=Mock(returncode=0)) as run:
                    self.assertTrue(assets._extract_iso("custom.iso", out, lambda _: None))
                    args = run.call_args.args[0]
                    self.assertIn("*.TH_", args)
                    self.assertIn("*.theme", args)

    def test_metadata_failure_is_not_success_or_file_not_found(self):
        with patch.object(assets, "_cached_archive", return_value=None), \
                patch.object(assets, "_http_json", side_effect=URLError("offline")):
            with self.assertRaisesRegex(assets.AssetError, "offline.*retry"):
                assets.download_pack(assets.ISO_DOWNLOAD, lambda _: None)

    def test_missing_remote_iso_fails(self):
        with patch.object(assets, "_cached_archive", return_value=None), \
                patch.object(assets, "_http_json", return_value={"files": []}):
            with self.assertRaisesRegex(assets.AssetError, "not found"):
                assets.download_pack(assets.ISO_DOWNLOAD, lambda _: None)

    def test_short_download_does_not_become_cached_iso(self):
        response = BytesIO(b"short")
        response.headers = {"Content-Length": "100"}
        with tempfile.TemporaryDirectory() as out, \
                patch.object(assets.urllib.request, "urlopen", return_value=response):
            target = Path(out) / "xp.iso"
            with self.assertRaisesRegex(assets.AssetError, "Incomplete download"):
                assets._fetch(assets.ISO_DOWNLOAD, "xp.iso", str(target), lambda _: None)
            self.assertFalse(target.exists())
            self.assertFalse(Path(str(target) + ".part").exists())

    def test_cached_iso_imports_without_network_and_requires_all_assets(self):
        with patch.object(assets, "_cached_archive", return_value="cached.iso"), \
                patch.object(assets, "remote_name") as remote, \
                patch.object(assets, "_fetch") as fetch, \
                patch.object(assets, "import_media") as unpack:
            unpack.return_value = {"fonts": 1, "styles": 0, "wallpapers": 1}
            with self.assertRaises(assets.AssetError):
                assets.download_pack(assets.ISO_DOWNLOAD, lambda _: None)
            unpack.return_value = {"fonts": 1, "styles": 1, "wallpapers": 1}
            self.assertTrue(assets.download_pack(assets.ISO_DOWNLOAD, lambda _: None))
            self.assertEqual(unpack.call_count, 2)
            remote.assert_not_called()
            fetch.assert_not_called()

    def test_cabextract_failure_propagates(self):
        with tempfile.TemporaryDirectory() as out, \
                patch.object(assets.host, "run") as run:
            run.side_effect = assets.host.HostCommandError(["cabextract"], 1, "bad cabinet")
            with self.assertRaisesRegex(assets.host.HostCommandError, "bad cabinet"):
                assets._cabextract(["broken.cab"], out, lambda _: None)
            self.assertTrue(run.call_args.kwargs["check"])

    def test_only_official_packs_icons_and_widgets_in_one_pass(self):
        with patch.object(assets, "download_pack", return_value=True) as download, \
                patch.object(assets.widget_style, "install_kvantum") as widget, \
                patch.object(xp_icons, "setup") as icons:
            progress = Mock()
            assets.prepare_install_assets(progress)
            self.assertEqual([c.args[0] for c in download.call_args_list],
                             [assets.ISO_DOWNLOAD, *assets.DOWNLOADS[:2]])
            self.assertNotIn(assets.DOWNLOADS[2], assets.INSTALL_DOWNLOADS)
            widget.assert_called_once_with(progress)
            icons.assert_called_once_with(progress, activate=False)

    def test_failed_pack_stops_before_widgets_and_icons(self):
        with patch.object(assets, "download_pack", side_effect=[True, False]), \
                patch.object(assets.widget_style, "install_kvantum") as widget, \
                patch.object(xp_icons, "setup") as icons:
            with self.assertRaises(assets.AssetError):
                assets.prepare_install_assets(lambda _: None)
            widget.assert_not_called()
            icons.assert_not_called()

    def test_failed_media_does_not_modify_native_app(self):
        with patch.object(assets, "missing_for_install", return_value=[]), \
                patch.object(assets, "prepare_install_assets", side_effect=assets.AssetError("offline")), \
                patch.object(install, "_copy_app") as copy, \
                patch.object(install, "_write_launchers") as launchers, \
                patch.object(install, "is_installed") as state:
            with self.assertRaisesRegex(install.InstallError, "application has not been changed"):
                install.install(progress=lambda _: None)
            copy.assert_not_called()
            launchers.assert_not_called()
            state.assert_not_called()

    def test_real_iso_extracts_and_can_be_reimported(self):
        iso = Path(__file__).resolve().parents[1] / "iso/WinXPProSP3x86.iso"
        if not iso.exists():
            self.skipTest("Local XP ISO unavailable")
        with tempfile.TemporaryDirectory() as out, ExitStack() as stack:
            for attr, rel in (("CACHE", "cache"), ("FONTS_XP", "fonts"),
                              ("THEMES", "themes"), ("WALLPAPERS", "wallpapers"), ("CONFIG", "config.json")):
                stack.enter_context(patch.object(assets.paths, attr, str(Path(out) / rel)))
            stack.enter_context(patch.object(assets.paths, "DATA", out))
            stack.enter_context(patch.object(assets.host, "have", side_effect=lambda name: name == "cabextract"))
            for _ in range(2):
                counts = assets.import_media(str(iso), lambda _: None)
                self.assertTrue(all(counts.values()), counts)
                self.assertTrue((Path(out) / "themes/Luna/luna.msstyles").exists())
                self.assertEqual(counts["themes"], 2)
                classic = Path(out) / "themes/Windows Classic.theme"
                self.assertEqual(theme_index.theme_label(str(classic)), "Windows Classic")
                self.assertIn("control panel\\colors", theme_file.load_theme_file(str(classic)))
                self.assertEqual(len(list((Path(out) / "themes").rglob("*.theme"))), 2)
            self.assertTrue((Path(out) / "cache/media-unpack/I386/XPSP2RES.DL_").exists())


if __name__ == "__main__":
    unittest.main()
