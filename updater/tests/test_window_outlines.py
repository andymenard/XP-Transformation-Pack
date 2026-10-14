import importlib.util,subprocess,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('xp_window_outlines',Path(__file__).resolve().parents[2]/'releases/0.2.1/xp-window-outlines.py')
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
class WindowOutlines(unittest.TestCase):
 def test_theme_changes_are_repaired_idempotently_with_original_backup(self):
  with tempfile.TemporaryDirectory() as folder:
   home=Path(folder);outline=home/'.local/share/kwin/xplasma/outline.qml';outline.parent.mkdir(parents=True);outline.write_text('outline')
   config=home/'.config/kwinrc';config.parent.mkdir();original='[Windows]\nElectricBorderMaximize=true\n[Outline]\nQmlPath=other\n';config.write_text(original)
   value='other';writes=[]
   def run(args,**kwargs):
    nonlocal value
    if args[0]=='kreadconfig6':return subprocess.CompletedProcess(args,0,value+'\n','')
    if args[0]=='kwriteconfig6':
     value=args[-1];writes.append(args);config.write_text(config.read_text().replace('QmlPath=other','QmlPath='+value))
    return subprocess.CompletedProcess(args,0,'','')
   with patch.object(helper.Path,'home',return_value=home),patch.object(helper.shutil,'which',side_effect=lambda name:name),patch.object(helper.subprocess,'run',side_effect=run):
    helper.apply();helper.apply();self.assertEqual(len(writes),1)
    self.assertIn('ElectricBorderMaximize=true',config.read_text())
    value='other';helper.apply();self.assertEqual(len(writes),2)
    self.assertEqual((home/'.local/share/xplasma/maintenance/snapping-outline/kwinrc.before').read_text(),original)
 def test_absent_outline_does_not_change_configuration(self):
  with tempfile.TemporaryDirectory() as folder,patch.object(helper.Path,'home',return_value=Path(folder)),patch.object(helper.subprocess,'run') as run:
   helper.apply();run.assert_not_called()
if __name__=='__main__':unittest.main()
