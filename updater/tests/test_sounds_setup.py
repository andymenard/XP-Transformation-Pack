import importlib.util,json,hashlib,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
spec=importlib.util.spec_from_file_location('sounds_setup',Path(__file__).resolve().parents[2]/'releases/0.2.3/setup-sounds.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class SoundsSetup(unittest.TestCase):
 def fixture(self,root):
  source=root/'source';p=source/'theme/stereo/desktop-login.wav';p.parent.mkdir(parents=True);p.write_bytes(b'test WAV payload')
  (source/'files.json').write_text(json.dumps({'theme/stereo/desktop-login.wav':hashlib.sha256(p.read_bytes()).hexdigest()}));return source
 def test_install_and_undo_preserve_original_files(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);source=self.fixture(root);home=root/'home';dest=home/'.local/share/sounds'/m.THEME/'stereo/desktop-login.wav';dest.parent.mkdir(parents=True);dest.write_bytes(b'previous sound')
   with patch.object(m,'SOURCE',source):
    m.install(home,configure=False);self.assertEqual(dest.read_bytes(),b'test WAV payload');m.undo(home);self.assertEqual(dest.read_bytes(),b'previous sound')
 def test_tampered_payload_rejected_before_any_write(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);source=self.fixture(root);(source/'theme/stereo/desktop-login.wav').write_bytes(b'changed');home=root/'home'
   with patch.object(m,'SOURCE',source):
    with self.assertRaises(RuntimeError):m.install(home,configure=False)
   self.assertFalse(home.exists())
 def test_later_sound_edits_are_preserved(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);source=self.fixture(root);home=root/'home'
   with patch.object(m,'SOURCE',source):m.install(home,configure=False)
   dest=home/'.local/share/sounds'/m.THEME/'stereo/desktop-login.wav';dest.write_bytes(b'user edit')
   with self.assertRaises(RuntimeError):m.undo(home)
   self.assertEqual(dest.read_bytes(),b'user edit')
 def test_only_sound_keys_are_written_and_undo_restores_them(self):
  with tempfile.TemporaryDirectory() as folder:
   root=Path(folder);source=self.fixture(root);home=root/'home';values={};original={}
   for f,g,k,v in m.SETTINGS:values[(f,g,k)]='before '+k
   original=values.copy()
   def get(args,**kwargs):return values[(args[args.index('--file')+1],args[args.index('--group')+1],args[args.index('--key')+1])]
   def run(args,**kwargs):values[(args[args.index('--file')+1],args[args.index('--group')+1],args[args.index('--key')+1])]=args[-1]
   with patch.object(m,'SOURCE',source),patch.object(m.subprocess,'check_output',side_effect=get),patch.object(m.subprocess,'run',side_effect=run):
    m.install(home);self.assertEqual(values[('kdeglobals','Sounds','Theme')],m.THEME);m.undo(home);self.assertEqual(values,original)
if __name__=='__main__':unittest.main()
