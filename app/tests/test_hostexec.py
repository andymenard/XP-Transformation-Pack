"""The AppImage boundary: what hostexec must not leak into host commands.

The AppImage exports its own variables and ships its own python3. Everything
exported at start is inherited by the host commands the wizard then runs, and
the failure that causes is silent rather than loud -- a host tool that loads
the bundle's Qt, or a native venv built by an interpreter that disappears when
the mount does. These tests pin the two mechanisms that prevent it.
"""
from pathlib import Path
import os
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hostexec as host


# What the AppImage runtime and a conventional AppRun actually set. Used as the
# input to every case below, so the tests describe a realistic environment
# rather than an isolated variable.
APPIMAGE_ENV = {
    "APPIMAGE": "/home/deck/xp2plasma-1.0.0-x86_64.AppImage",
    "APPDIR": "/tmp/.mount_xp2pla1Cab2d",
    "OWD": "/home/deck",
    "ARGV0": "/tmp/.mount_xp2pla1Cab2d/AppRun",
    "LD_LIBRARY_PATH": "/tmp/.mount_xp2pla1Cab2d/usr/lib",
    "PYTHONHOME": "/tmp/.mount_xp2pla1Cab2d/usr",
    "PYTHONPATH": "/tmp/.mount_xp2pla1Cab2d/usr/share/xp2plasma",
    "PATH": "/tmp/.mount_xp2pla1Cab2d/usr/bin:/usr/local/bin:/usr/bin:/bin",
    "XDG_DATA_DIRS": "/tmp/.mount_xp2pla1Cab2d/usr/share:/usr/share",
    "HOME": "/home/deck",
    "LANG": "en_GB.UTF-8",
}


class AppImageBoundaryTests(unittest.TestCase):
    def setUp(self):
        patcher = patch.dict(os.environ, APPIMAGE_ENV, clear=True)
        patcher.start()
        self.addCleanup(patcher.stop)
        # IN_APPIMAGE is read once at import; drive it per-test instead.
        self.in_appimage = patch.object(host, "IN_APPIMAGE", True)
        self.in_appimage.start()
        self.addCleanup(self.in_appimage.stop)

    def test_bundle_variables_are_stripped(self):
        env = host._host_env()
        for name in ("APPDIR", "APPIMAGE", "OWD", "ARGV0",
                     "LD_LIBRARY_PATH", "PYTHONHOME", "PYTHONPATH"):
            self.assertNotIn(name, env, f"{name} leaked into a host command")

    def test_session_variables_survive(self):
        """HOME and LANG describe the machine, not the bundle."""
        env = host._host_env()
        self.assertEqual(env["HOME"], "/home/deck")
        self.assertEqual(env["LANG"], "en_GB.UTF-8")

    def test_appdir_is_removed_from_path(self):
        path = host._host_path()
        self.assertNotIn("/tmp/.mount_xp2pla1Cab2d/usr/bin", path.split(os.pathsep))
        self.assertEqual(path.split(os.pathsep), ["/usr/local/bin", "/usr/bin", "/bin"])

    def test_xdg_search_paths_keep_the_host_entries(self):
        """Subtract the mount, keep the rest.

        Dropping XDG_DATA_DIRS wholesale would be wrong -- the session sets it
        and host tools legitimately need it -- so only the mount's own entries
        come out.
        """
        self.assertEqual(host._host_env()["XDG_DATA_DIRS"], "/usr/share")

    def test_bundled_python_is_not_visible_to_which(self):
        """The bundle ships a python3; find_host_python() must never see it."""
        with patch.object(host.shutil, "which") as which, \
                patch.dict(host._which_cache, {}, clear=True):
            which.return_value = "/usr/bin/python3"
            host.which("python3")
            searched = which.call_args.kwargs["path"].split(os.pathsep)
        self.assertNotIn("/tmp/.mount_xp2pla1Cab2d/usr/bin", searched)
        self.assertIn("/usr/bin", searched)

    def test_outside_the_appimage_nothing_is_touched(self):
        with patch.object(host, "IN_APPIMAGE", False):
            env = host._host_env()
        self.assertEqual(env.get("APPDIR"), APPIMAGE_ENV["APPDIR"])
        self.assertEqual(env.get("LD_LIBRARY_PATH"), APPIMAGE_ENV["LD_LIBRARY_PATH"])


class SpawnedEnvironmentTests(unittest.TestCase):
    """_execute() must pass the scrubbed environment, never inherit ours."""

    def test_run_passes_a_scrubbed_environment(self):
        with patch.dict(os.environ, APPIMAGE_ENV, clear=True), \
                patch.object(host, "IN_APPIMAGE", True), \
                patch.object(host, "_subprocess_run") as spawned:
            spawned.return_value = host.subprocess.CompletedProcess([], 0, "", "")
            host._execute(["true"], check=False, capture=False, env=None,
                          cwd=None, timeout=None, on_output=None)
            passed = spawned.call_args.kwargs["env"]
        self.assertNotIn("APPDIR", passed)
        self.assertNotIn("LD_LIBRARY_PATH", passed)
        self.assertNotIn("PYTHONHOME", passed)

    def test_explicit_env_still_wins(self):
        with patch.dict(os.environ, APPIMAGE_ENV, clear=True), \
                patch.object(host, "IN_APPIMAGE", True), \
                patch.object(host, "_subprocess_run") as spawned:
            spawned.return_value = host.subprocess.CompletedProcess([], 0, "", "")
            host._execute(["true"], check=False, capture=False,
                          env={"QT_QPA_PLATFORM": "offscreen"},
                          cwd=None, timeout=None, on_output=None)
            passed = spawned.call_args.kwargs["env"]
        self.assertEqual(passed["QT_QPA_PLATFORM"], "offscreen")


if __name__ == "__main__":
    unittest.main()
