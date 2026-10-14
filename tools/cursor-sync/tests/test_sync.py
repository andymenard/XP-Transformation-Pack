import importlib.util,tempfile,unittest
from pathlib import Path
root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('cursor_sync',root/'sync.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class CursorSyncTests(unittest.TestCase):
 def test_default_tracks_theme_changes_without_touching_logical_size(self):
  with tempfile.TemporaryDirectory() as temp:
   home=Path(temp);cfg=home/'.config/kcminputrc';cfg.parent.mkdir();cfg.write_text('[Mouse]\ncursorTheme=xp-first\ncursorSize=32\n')
   module.apply(home);index=home/'.local/share/icons/default/index.theme';stamp=index.stat().st_mtime_ns;module.apply(home);self.assertEqual(index.stat().st_mtime_ns,stamp)
   cfg.write_text('[Mouse]\ncursorTheme=xp-second\ncursorSize=32\n');module.apply(home);self.assertIn('Inherits=xp-second',index.read_text());self.assertIn('cursorSize=32',cfg.read_text());self.assertEqual((home/'.icons/default').resolve(),index.parent)
 def test_existing_unmanaged_fallback_is_preserved(self):
  with tempfile.TemporaryDirectory() as temp:
   home=Path(temp);index=home/'.local/share/icons/default/index.theme';index.parent.mkdir(parents=True);index.write_text('[Icon Theme]\nInherits=user-custom\n')
   with self.assertRaises(RuntimeError):module.apply(home)
   self.assertIn('user-custom',index.read_text())
 def test_unsafe_theme_path_is_rejected(self):
  with tempfile.TemporaryDirectory() as temp:
   home=Path(temp);cfg=home/'.config/kcminputrc';cfg.parent.mkdir();cfg.write_text('[Mouse]\ncursorTheme=../../outside\ncursorSize=32\n')
   with self.assertRaises(ValueError):module.apply(home)
if __name__=='__main__':unittest.main()
