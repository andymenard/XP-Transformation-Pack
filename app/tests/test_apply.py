"""What the wallpaper setters actually write into Plasma's config.

Both go out through Plasma's scripting interface, so without a live session the
only thing worth testing is the script itself -- and that is worth testing,
because a malformed one fails at runtime with the desktop simply unchanged and
nothing to explain why.
"""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import apply


class WallpaperTests(unittest.TestCase):
    def test_position_and_colour_go_out_in_one_script(self):
        with patch.object(apply.host, "run") as run:
            apply.apply_wallpaper("/tmp/x.jpg", 3, (58, 110, 165))
        # The image itself still goes through plasma-apply-wallpaperimage.
        self.assertEqual(run.call_args_list[0].args[0][0], "plasma-apply-wallpaperimage")
        script = run.call_args_list[1].args[0][-1]
        self.assertIn('d.writeConfig("FillMode", 3);', script)
        self.assertIn('d.writeConfig("Color", "#3a6ea5");', script)
        self.assertEqual(len(run.call_args_list), 2)

    def test_colour_only_leaves_the_position_alone(self):
        with patch.object(apply.host, "run") as run:
            apply.apply_wallpaper("/tmp/x.jpg", None, (1, 2, 3))
        script = run.call_args_list[-1].args[0][-1]
        self.assertNotIn("FillMode", script)
        self.assertIn('"#010203"', script)

    def test_position_only_leaves_the_colour_alone(self):
        with patch.object(apply.host, "run") as run:
            apply.apply_wallpaper("/tmp/x.jpg", 0)
        script = run.call_args_list[-1].args[0][-1]
        self.assertIn('d.writeConfig("FillMode", 0);', script)
        self.assertNotIn("Color", script)

    def test_neither_given_touches_nothing_but_the_image(self):
        """The default must stay "keep whatever the user already has"."""
        with patch.object(apply.host, "run") as run:
            apply.apply_wallpaper("/tmp/x.jpg")
        self.assertEqual(len(run.call_args_list), 1)

    def test_solid_colour_switches_to_the_colour_plugin(self):
        with patch.object(apply.host, "run") as run:
            apply.apply_wallpaper_color((58, 110, 165))
        script = run.call_args.args[0][-1]
        self.assertIn("org.kde.color", script)
        self.assertIn('d.writeConfig("Color", "#3a6ea5");', script)

    def test_colour_is_hex_not_a_triple(self):
        # Plasma reads a "0,0,0" triple as unset and paints white, so black has
        # to go out as #000000 or it silently becomes the wrong colour.
        with patch.object(apply.host, "run") as run:
            apply.apply_wallpaper_color((0, 0, 0))
        self.assertIn('"#000000"', run.call_args.args[0][-1])

    def test_every_desktop_is_set(self):
        """Plasma is per-desktop; setting one and leaving the rest is the kind
        of bug that only shows up on a second monitor."""
        with patch.object(apply.host, "run") as run:
            apply.apply_wallpaper_color((1, 2, 3))
        self.assertIn("for (var i = 0; i < ds.length; i++)",
                      run.call_args.args[0][-1])


if __name__ == "__main__":
    unittest.main()
