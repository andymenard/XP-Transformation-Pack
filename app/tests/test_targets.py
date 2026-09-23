"""The toolkit-target registry: Apply and Reset walk every target in order;
GTK says "not yet"; one target's failure doesn't stop the rest."""
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import targets  # noqa: E402


class TargetsTests(unittest.TestCase):
    def test_registry_is_qt_then_the_gtk_stubs(self):
        self.assertEqual([t.name for t in targets.TARGETS], ["qt", "gtk3", "gtk4"])

    def test_visual_style_goes_to_qt_and_gtk_reports_not_yet(self):
        lines = []
        with patch.object(targets.widget_style, "apply_visual_style") as qt:
            targets.apply_visual_style("/x/luna.msstyles", "NORMALBLUE_INI", lines.append)
        qt.assert_called_once_with("/x/luna.msstyles", "NORMALBLUE_INI", lines.append)
        self.assertEqual([line for line in lines if "not yet" in line],
                         ["  GTK 3: not yet (its apps keep their current theme)",
                          "  GTK 4: not yet (its apps keep their current theme)"])

    def test_classic_scheme_goes_to_qt(self):
        with patch.object(targets.widget_style, "apply_classic_style") as qt:
            targets.apply_classic_style("scheme", lambda _line: None)
        qt.assert_called_once()

    def test_a_failing_target_is_reported_and_the_rest_still_run(self):
        lines = []
        with patch.object(targets.widget_style, "apply_visual_style", side_effect=RuntimeError("no plugin built")):
            targets.apply_visual_style("/x/luna.msstyles", None, lines.append)
        self.assertIn("  Qt skipped: no plugin built", lines)
        self.assertEqual(sum("not yet" in line for line in lines), 2)

    def test_revert_undoes_qt_and_leaves_gtk_alone(self):
        lines = []
        with patch.object(targets.widget_style, "remove", return_value="Breeze") as qt:
            targets.revert(lines.append)
        qt.assert_called_once()
        self.assertEqual(lines, ["  Qt: widget style restored to Breeze"])


if __name__ == "__main__":
    unittest.main()
