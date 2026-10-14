import importlib.util,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('decoration',Path(__file__).parents[1]/'xp-decoration.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class CompatibilityTests(unittest.TestCase):
 def test_working_plugin_is_untouched(self):
  with patch.object(m.subprocess,'check_output',return_value=m.LEGACY),patch.object(m.subprocess,'run') as run:
   self.assertFalse(m.repair());run.assert_not_called()
 def test_other_theme_is_untouched(self):
  with patch.object(m.subprocess,'check_output',side_effect=[m.V2,'Breeze']),patch.object(m.subprocess,'run') as run:
   self.assertFalse(m.repair());run.assert_not_called()
 def test_generated_xp_theme_selects_compatible_plugin(self):
  with tempfile.TemporaryDirectory() as d:
   rc=Path(d)/'.local/share/aurorae/themes/xp/xprc';rc.parent.mkdir(parents=True);rc.write_text('[X-XPlasma-Generated]\n')
   with patch.object(m.subprocess,'check_output',side_effect=[m.V2,'__aurorae__svg__xp']),patch.object(m.subprocess,'run') as run:
    self.assertTrue(m.repair(Path(d)));self.assertEqual(run.call_count,2)
    self.assertEqual(run.call_args_list[0].args[0][-2:],['library',m.LEGACY])
if __name__=='__main__':unittest.main()
