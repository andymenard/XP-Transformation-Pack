"""Bundled utility lookup, native installation, and real no-PATH extraction."""
from contextlib import ExitStack
import binascii
import hashlib
from io import BytesIO
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import bundled_tools as bundle
import hostexec as host
import paths
import setup_assets


class LookupTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        directory = self.root / platform.machine()
        directory.mkdir()
        self.binary = directory / "cabextract"
        self.binary.write_bytes(b"#!/bin/sh\nexit 0\n")
        self.binary.chmod(0o755)
        self.digest = hashlib.sha256(self.binary.read_bytes()).hexdigest()
        (directory / "manifest.json").write_text(json.dumps({
            "architecture": platform.machine(), "tools": {"cabextract": {"sha256": self.digest}}}))
        self.context = ExitStack()
        self.addCleanup(self.context.close)
        self.context.enter_context(patch.object(bundle, "root", return_value=self.root))

    def test_private_tool_wins_over_host_and_cached_miss(self):
        with patch.dict(host._which_cache, {"cabextract": None}), \
                patch.object(host.shutil, "which") as which:
            self.assertEqual(host.which("cabextract"), str(self.binary))
            which.assert_not_called()

    def test_other_commands_keep_host_lookup(self):
        self.assertIsNone(bundle.executable("kwriteconfig6"))
        with patch.dict(host._which_cache, {}, clear=True), \
                patch.object(host.shutil, "which", return_value="/usr/bin/kwriteconfig6"):
            self.assertEqual(host.which("kwriteconfig6"), "/usr/bin/kwriteconfig6")

    def test_verified_executable_is_used_in_place(self):
        """No staging: the tool runs from wherever the app tree lives.

        The old Flatpak had to copy it into a host-visible cache first because
        /app was invisible to flatpak-spawn --host. An AppImage mount is an
        ordinary path, so a copy would be pure overhead -- and worse, one that
        could go stale against the manifest it was verified under.
        """
        self.assertEqual(bundle.executable("cabextract"), str(self.binary))
        self.assertFalse((self.root / "cache").exists())

    def test_damaged_source_fails_instead_of_silently_using_host(self):
        self.binary.write_bytes(b"damaged")
        with self.assertRaisesRegex(RuntimeError, "damaged"):
            host.which("cabextract")

    def test_unbuilt_checkout_can_fall_back(self):
        with patch.object(bundle.platform, "machine", return_value="unbuilt-cpu"):
            self.assertIsNone(bundle.executable("cabextract"))


class RealToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tools = {name: bundle.executable(name) for name in bundle.TOOLS}
        if not all(cls.tools.values()):
            raise unittest.SkipTest("Build bundled-tools first")

    def run_tool(self, name, *args):
        # Empty PATH also proves gzip/xz/zstd aren't being shelled out to.
        return subprocess.run([self.tools[name], *map(str, args)], check=True,
                              env={"PATH": "", "LANG": "C"}, capture_output=True)

    def test_binaries_are_static_and_run_on_glibc_host_without_path(self):
        for name, file in self.tools.items():
            data = Path(file).read_bytes()
            self.assertEqual(data[:4], b"\x7fELF")
            # Static PIE may have PT_DYNAMIC relocations, but no interpreter
            # or DT_NEEDED entries for external shared libraries.
            import struct
            self.assertEqual(data[4:6], bytes((2, 1)))
            offset = struct.unpack_from("<Q", data, 32)[0]
            size, count = struct.unpack_from("<HH", data, 54)
            types = [struct.unpack_from("<I", data, offset + i * size)[0] for i in range(count)]
            self.assertNotIn(3, types)
            for i, kind in enumerate(types):
                if kind != 2:
                    continue
                header = offset + i * size
                start = struct.unpack_from("<Q", data, header + 8)[0]
                length = struct.unpack_from("<Q", data, header + 32)[0]
                tags = [struct.unpack_from("<q", data, pos)[0]
                        for pos in range(start, start + length, 16)]
                self.assertNotIn(1, tags)
            self.assertIn(name.encode(), self.run_tool(name, "--version").stdout)

    def test_compressed_tar_and_zip_without_external_decompressors(self):
        payload = b"bundled utilities\n"
        with tempfile.TemporaryDirectory() as out:
            root = Path(out)
            for kind in ("gz", "bz2", "xz"):
                archive = root / ("test.tar." + kind)
                with tarfile.open(archive, "w:" + kind) as tf:
                    entry = tarfile.TarInfo("hello.txt")
                    entry.size = len(payload)
                    tf.addfile(entry, BytesIO(payload))
                self.assertEqual(self.run_tool("bsdtar", "-xOf", archive, "hello.txt").stdout, payload)
            archive = root / "test.zip"
            with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as zf:
                zf.writestr("hello.txt", payload)
            self.assertEqual(self.run_tool("bsdtar", "-xOf", archive, "hello.txt").stdout, payload)
            (root / "hello.txt").write_bytes(payload)
            archive = root / "test.tar.zst"
            self.run_tool("bsdtar", "--zstd", "-cf", archive, "-C", root, "hello.txt")
            self.assertEqual(self.run_tool("bsdtar", "-xOf", archive, "hello.txt").stdout, payload)

    def test_rar5_from_upstream_fixture(self):
        archive = bundle.root() / "sources/libarchive-3.8.9.tar.xz"
        fixture = "libarchive-3.8.9/libarchive/test/test_read_format_rar5_compressed.rar.uu"
        with tarfile.open(archive) as tf:
            lines = tf.extractfile(fixture).read().splitlines()
        raw = b"".join(binascii.a2b_uu(line) for line in lines[1:] if line not in (b"end", b""))
        with tempfile.TemporaryDirectory() as out:
            path = Path(out) / "compressed.rar"
            path.write_bytes(raw)
            self.assertIn(b"test.bin", self.run_tool("bsdtar", "-tf", path).stdout)
            self.assertTrue(self.run_tool("bsdtar", "-xOf", path, "test.bin").stdout)

    def test_iso_reader_without_pycdlib_or_host_tools(self):
        import pycdlib
        with tempfile.TemporaryDirectory() as out:
            iso = Path(out) / "test.iso"
            cd = pycdlib.PyCdlib()
            cd.new()
            stream = BytesIO(b"ISO contents")
            cd.add_fp(stream, 12, iso_path="/HELLO.TXT;1")
            cd.write(str(iso))
            cd.close()
            self.assertEqual(self.run_tool("bsdtar", "-xOf", iso, "HELLO.TXT").stdout, b"ISO contents")

    def test_real_xp_cab_member(self):
        i386 = paths.iso_i386()
        source = Path(i386 or "") / "CLASSIC.TH_"
        if not i386 or not source.is_file():
            self.skipTest("Local XP media unavailable")
        with tempfile.TemporaryDirectory() as out:
            self.run_tool("cabextract", "-q", "-d", out, source)
            files = list(Path(out).iterdir())
            self.assertEqual(len(files), 1)
            self.assertIn(b"[Theme]", files[0].read_bytes())


if __name__ == "__main__":
    unittest.main()
