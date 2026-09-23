"""Resource fidelity, mapping provenance, and safe icon-theme installation."""
from pathlib import Path
import json
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import xp_icons as icons
from PIL import Image


class ResourceTests(unittest.TestCase):
    def test_group_resource_ids_become_file_offsets(self):
        group = struct.pack("<HHH", 0, 1, 2)
        group += struct.pack("<BBBBHHIH", 16, 16, 0, 0, 1, 32, 3, 99)
        group += struct.pack("<BBBBHHIH", 32, 32, 0, 0, 1, 32, 4, 7)
        result = icons.group_to_ico(group, {99: b"abc", 7: b"defg"})
        self.assertEqual(struct.unpack_from("<I", result, 18)[0], 38)
        self.assertEqual(struct.unpack_from("<I", result, 34)[0], 41)
        self.assertEqual(result[38:], b"abcdefg")
        with self.assertRaises(ValueError):
            icons.group_to_ico(group, {99: b"abc"})

    def test_malformed_groups_fail_cleanly(self):
        for data in (b"", b"12345", struct.pack("<HHH", 0, 1, 0), struct.pack("<HHH", 0, 1, 999)):
            with self.subTest(data=data), self.assertRaises(ValueError):
                icons.group_to_ico(data, {})

    def test_wifi_and_battery_state_mappings(self):
        mapping = icons.icon_mappings()
        for level in range(0, 101, 20):
            self.assertEqual(mapping[f"network-wireless-{level}-symbolic"][:2],
                             ("xpsp2res.dll", str(2010 + level // 20)))
            self.assertEqual(mapping[f"network-wireless-{level}-limited-symbolic"][:2],
                             ("netshell.dll", "196"))
        for level, frame in ((0, 3), (10, 2), (30, 2), (40, 1), (60, 1), (70, 0), (100, 0)):
            self.assertEqual(mapping[f"battery-{level:03d}"][1], f"bitmap-frame-{frame}")
            self.assertEqual(mapping[f"battery-{level:03d}-charging"][1],
                             f"bitmap-frame-{frame}-charging")
        self.assertNotEqual(mapping["battery-missing"], mapping["battery-000"])

    def test_unexpected_battery_strip_fails_cleanly(self):
        with tempfile.TemporaryDirectory() as out, \
                patch("xp_assets.pe_bitmap", return_value=Image.new("RGBA", (16, 16))):
            with self.assertRaises(ValueError):
                icons.battery_frames(Path("unused.dll"), Path(out))


class IconThemeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        media = Path(icons.paths.HERE) / "iso/extracted/I386"
        if not (media / "SHELL32.DL_").exists():
            raise unittest.SkipTest("User's XP media is not available")
        cls.tmp = tempfile.TemporaryDirectory()
        cls.catalog = Path(cls.tmp.name) / "catalog"
        cls.manifest = icons.build_catalog(cls.catalog, str(media))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_highest_bit_depth_and_native_sizes(self):
        groups = icons.read_icons(self.catalog / "binaries/shell32.dll")
        folder = next(g for g in groups if g.resource_id == "4")
        self.assertEqual(folder.depths[16], 32)
        self.assertEqual(folder.depths[48], 32)
        self.assertGreater(len(set(folder.images[48].getdata())), 256)
        self.assertEqual(folder.images[16].size, (16, 16))
        self.assertIn(0, folder.images[48].getchannel("A").getdata())
        self.assertTrue(any(0 < a < 255 for a in folder.images[48].getchannel("A").getdata()))

    def test_native_images_and_provenance(self):
        with tempfile.TemporaryDirectory() as out:
            dest = Path(out) / "theme"
            report = icons.build_theme(self.catalog, dest, "breeze")
            self.assertEqual((dest / "16x16/places/folder.png").read_bytes(),
                             (self.catalog / "shell32.dll/4/16.png").read_bytes())
            self.assertEqual(report["folder-programs"]["resource_id"], "37")
            self.assertEqual(report["folder-programs"]["binary"], "shell32.dll")
            self.assertEqual(report["folder-recent-files"]["resource_id"], "21")
            self.assertEqual(report["folder-recent-files"]["binary"], "shell32.dll")
            self.assertEqual(report["system-run"]["resource_id"], "160")
            self.assertEqual(report["system-run"]["binary"], "shell32.dll")
            for size in (16, 24, 32, 48):
                self.assertEqual((dest / f"{size}x{size}/actions/system-run.png").read_bytes(),
                                 (self.catalog / f"shell32.dll/160/{size}.png").read_bytes())
            for size in (16, 24, 32, 48):
                self.assertEqual((dest / f"{size}x{size}/places/folder-recent-files.png").read_bytes(),
                                 (self.catalog / f"shell32.dll/21/{size}.png").read_bytes())
            for size in (16, 32, 48):
                self.assertEqual((dest / f"{size}x{size}/places/folder-programs.png").read_bytes(),
                                 (self.catalog / f"shell32.dll/37/{size}.png").read_bytes())
            self.assertEqual(report["user-trash-full"]["resource_id"], "33")
            self.assertEqual(report["audio-volume-muted"]["resource_id"], "303")
            self.assertEqual(report["network-bluetooth-symbolic"]["binary"], "xpsp2res.dll")
            self.assertEqual(report["network-bluetooth-symbolic"]["resource_id"], "300")
            self.assertEqual(report["preferences-system-bluetooth"]["resource_id"], "300")
            self.assertEqual(report["klipper-symbolic"]["resource_id"], "133")
            self.assertEqual(report["emblem-symbolic-link"]["resource_id"], "30")
            self.assertEqual(report["emblem-symbolic-link"]["binary"], "shell32.dll")
            self.assertEqual(report["text-x-generic"]["resource_id"], "152")
            self.assertEqual(report["text-x-generic"]["binary"], "shell32.dll")
            self.assertEqual(report["battery-050-charging"]["resource_type"], "RT_BITMAP")
            self.assertEqual(report["battery-050-charging"]["frames"], [1, 7])
            self.assertEqual(report["battery-050-charging"]["resources"]["16"],
                             {"resource_id": 301, "rectangles": [[16, 0, 32, 16], [112, 0, 128, 16]]})
            self.assertEqual((dest / "16x16/status/battery-050.png").read_bytes(),
                             (self.catalog / "batmeter.dll/bitmap-frame-1/16.png").read_bytes())
            for level in range(0, 101, 20):
                self.assertEqual((dest / f"32x32/status/network-wireless-{level}-symbolic.png").read_bytes(),
                                 (self.catalog / f"xpsp2res.dll/{2010 + level // 20}/32.png").read_bytes())
            self.assertNotIn("network-bluetooth-inactive-symbolic", report)
            native_off = Path("/usr/share/icons/breeze/status/22/network-bluetooth-inactive-symbolic.svg")
            if native_off.exists():
                self.assertTrue((dest / "16x16/status/network-bluetooth-inactive-symbolic.svg").exists())
            native_profile = Path("/usr/share/icons/breeze/status/16/battery-050-charging-profile-powersave.svg")
            if native_profile.exists():
                self.assertTrue((dest / "16x16/status/battery-050-charging-profile-powersave.svg").exists())
            self.assertNotIn(96, report["folder"]["sizes"])
            self.assertNotIn("discord", report)
            self.assertNotIn("steam", report)
            self.assertIn("Inherits=breeze,hicolor", (dest / "index.theme").read_text())
            self.assertTrue(icons.owned_theme(dest))

    def test_battery_native_frames_mask_and_charging_overlay(self):
        for size, rid in ((16, 301), (32, 300)):
            strip = Image.open(self.catalog / f"batmeter.dll/bitmap-{rid}.png").convert("RGBA")
            overlay = Image.open(self.catalog / f"batmeter.dll/bitmap-frame-7/{size}.png").convert("RGBA")
            for frame in range(4):
                base = Image.open(self.catalog / f"batmeter.dll/bitmap-frame-{frame}/{size}.png").convert("RGBA")
                self.assertEqual(base.size, (size, size))
                for y in range(size):
                    for x in range(size):
                        rgb = strip.getpixel((frame * size + x, y))[:3]
                        self.assertEqual(base.getpixel((x, y)), (*rgb, 0 if rgb == (255, 0, 255) else 255))
                charged = Image.open(self.catalog / f"batmeter.dll/bitmap-frame-{frame}-charging/{size}.png")
                self.assertEqual(charged.tobytes(), Image.alpha_composite(base, overlay).tobytes())
                self.assertNotEqual(charged.tobytes(), base.tobytes())

    def test_entire_theme_rebuilds_directly_from_iso(self):
        iso = Path(icons.paths.HERE) / "iso/WinXPProSP3x86.iso"
        if not iso.exists():
            self.skipTest("User's XP ISO is not available")
        with tempfile.TemporaryDirectory() as out, \
                patch.object(icons.paths, "iso_i386", return_value=None), \
                patch.object(icons.paths, "HERE", out), \
                patch.object(icons.paths, "CACHE", out):
            catalog = Path(out) / "catalog"
            manifest = icons.build_catalog(catalog, str(iso))
            self.assertEqual(set(manifest), set(icons.BINARY_NAMES))
            self.assertEqual(manifest["xpsp2res.dll"]["300"]["sizes"], [16, 32, 48])
            self.assertIn("200", manifest["xpsp2res.dll"])
            self.assertIn("201", manifest["bthci.dll"])
            dest = Path(out) / "theme"
            report = icons.build_theme(catalog, dest)
            self.assertEqual(set(report), set(icons.icon_mappings()))
            self.assertEqual((dest / "16x16/places/folder.png").read_bytes(),
                             (self.catalog / "shell32.dll/4/16.png").read_bytes())

    def test_install_updates_only_owned_theme(self):
        with tempfile.TemporaryDirectory() as out:
            dest = Path(out) / "theme"
            icons.install_theme(self.catalog, dest)
            original = (dest / "16x16/places/folder.png").read_bytes()
            with patch.object(icons, "icon_mappings", return_value={"oops": ("missing.dll", "1", "Places")}):
                with self.assertRaises(ValueError):
                    icons.install_theme(self.catalog, dest)
            self.assertEqual((dest / "16x16/places/folder.png").read_bytes(), original)
            icons.install_theme(self.catalog, dest)
            foreign = Path(out) / "foreign"
            foreign.mkdir()
            (foreign / "index.theme").write_text("user-owned")
            with self.assertRaises(RuntimeError):
                icons.install_theme(self.catalog, foreign)
            self.assertEqual((foreign / "index.theme").read_text(), "user-owned")

    def test_missing_theme_removal_does_not_touch_other_themes(self):
        with tempfile.TemporaryDirectory() as out:
            with patch.object(icons, "THEME_DIR", Path(out)):
                self.assertFalse(icons.remove_theme())
                self.assertTrue(Path(out).exists())


if __name__ == "__main__":
    unittest.main()
