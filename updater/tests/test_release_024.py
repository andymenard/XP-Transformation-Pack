import hashlib,importlib.util,json,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import core
REPO=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('desktop024',REPO/'releases/0.2.4/setup-desktop.py');setup=importlib.util.module_from_spec(spec);spec.loader.exec_module(setup)
class Release024Tests(unittest.TestCase):
 def test_desktop_record_wins_over_accessory_record(self):
  with tempfile.TemporaryDirectory() as temp:
   home=Path(temp)
   for name in ['xp-accessory-suite','fedora-xp-installer']:
    state=home/'.local/share'/name;state.mkdir(parents=True);(state/'installation.json').write_text(json.dumps({'features':['icons']}))
   state=home/'.local/share/fedora-xp-installer';(state/'runtime/tools').mkdir(parents=True);(state/'runtime/tools/keep-xp-session.py').touch()
   self.assertEqual(core.find_installation(home)[0],state);self.assertEqual(setup.installation(home)[0],state)
 def test_setup_rejects_changed_or_linked_resources(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);p=root/'asset';p.write_text('original');(root/'files.json').write_text(json.dumps({'asset':hashlib.sha256(p.read_bytes()).hexdigest()}))
   with patch.object(setup,'ROOT',root):
    setup.verify();p.write_text('modified')
    with self.assertRaises(RuntimeError):setup.verify()
    p.unlink();(root/'other').write_text('original');p.symlink_to('other')
    with self.assertRaises(RuntimeError):setup.verify()
