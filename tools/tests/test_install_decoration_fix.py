import importlib.util,tempfile,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('installer',Path(__file__).parents[1]/'install-decoration-fix.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class InstallerTests(unittest.TestCase):
 def test_repeat_install_preserves_backup_and_single_hook(self):
  with tempfile.TemporaryDirectory() as d:
   h=Path(d);state=h/'.local/share/kubuntu-xp-suite';tools=state/'runtime/tools';tools.mkdir(parents=True)
   (state/'installation.json').write_text('{}');keeper=tools/'keep-xp-session.py';keeper.write_text('# original helper\n')
   m.install(h);first=keeper.read_text();m.install(h)
   self.assertEqual(keeper.read_text(),first)
   backup=h/'.local/share/xplasma/maintenance/decoration-compatibility/keep-xp-session.py'
   self.assertEqual(backup.read_text(),'# original helper\n')
   self.assertTrue((h/'.config/autostart/xp-decoration-compatibility.desktop').is_file())
 def test_missing_installation_is_rejected(self):
  with tempfile.TemporaryDirectory() as d:
   with self.assertRaises(RuntimeError):m.install(Path(d))
if __name__=='__main__':unittest.main()
