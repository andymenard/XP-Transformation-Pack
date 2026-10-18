"""Windows XP media is marked non-free: in the installer, and in the public repository's non-free/ folder."""
import importlib.util,json,subprocess,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
BASE=Path(__file__).resolve().parents[1]
SCRIPT=BASE.parent/'public-alpha/non-free.py'
sys.path.insert(0,str(BASE))

class Installer(unittest.TestCase):
 def test_features_using_xp_media_say_so(self):
  spec=importlib.util.spec_from_file_location('non_free_gui',BASE/'gui.py');g=importlib.util.module_from_spec(spec);spec.loader.exec_module(g)
  features={f['id']:f for f in json.loads((BASE/'features.json').read_text())}
  self.assertTrue(features['desktop']['non_free']);self.assertTrue(features['firefox']['non_free'])
  self.assertNotIn('non_free',features['behavior'])
  self.assertIn("Microsoft's copyright",g.feature_description(features['sounds']))
  self.assertEqual(g.feature_description(features['behavior']),features['behavior']['description'])

@unittest.skipUnless(SCRIPT.is_file(),'the public export tools are not part of an installer bundle')
class Folder(unittest.TestCase):
 def setUp(self):
  spec=importlib.util.spec_from_file_location('non_free',SCRIPT);self.n=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.n)
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);root=Path(self.temp.name)
  self.payload=root/'suite/payload';self.public=root/'public'
  subprocess.run(['git','init','-q',str(self.public)],check=True)
  (self.public/'ours.qml').write_text('Item {}\n')
  subprocess.run(['git','-C',str(self.public),'add','.'],check=True)
  subprocess.run(['git','-C',str(self.public),'-c','user.name=t','-c','user.email=t@t','commit','-qm','x'],check=True)
 def put(self,relative,data=b'x'):
  path=self.payload/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
 def pe(self,entry):
  # The smallest PE header the check reads: MZ, e_lfanew, PE signature, entry point.
  data=bytearray(0x100);data[:2]=b'MZ';data[0x3c:0x40]=(0x40).to_bytes(4,'little');data[0x40:0x44]=b'PE\0\0'
  data[0x40+0x28:0x40+0x2c]=entry.to_bytes(4,'little');return bytes(data)

 def test_media_only_marked_and_listed(self):
  for name in ['basic/sounds/stereo/start.wav','assets/fonts/tahoma.ttf','assets/xplasma/assets/themes/Luna/luna.msstyles',
               'basic/pointers/cursors/left_ptr','assets/user-pictures/PROVENANCE.json']:self.put(name)
  self.put('assets/xplasma/assets/themes/Royale/Shell/NormalColor/ShellStyle.dll',self.pe(0))
  # Not media, not Microsoft's, or already the project's source:
  self.put('assets/xplasma/extracted/explorer.exe');self.put('assets/xplasma/extracted/shell32.dll',self.pe(0x1000))
  self.put('assets/xplasma/assets/themes/Winter Fun Packs 2003/Games/Half-Life_2.wmz')
  self.put('assets/hires-wallpapers/Bliss.jpg');self.put('firefox/firefox-linux-amd64.tar.xz')
  self.put('templates/sddm/Main.qml');self.put('templates/lockscreen/ours.qml',b'Item {}\n')
  with patch('builtins.print'):chosen=self.n.refresh(self.payload.parent,self.public)
  self.assertEqual(chosen,['assets/fonts/tahoma.ttf','assets/user-pictures/PROVENANCE.json','assets/xplasma/assets/themes/Luna/luna.msstyles',
                           'assets/xplasma/assets/themes/Royale/Shell/NormalColor/ShellStyle.dll','basic/pointers/cursors/left_ptr','basic/sounds/stereo/start.wav'])
  folder=self.public/'non-free'
  self.assertIn('not** covered by the project licence',(folder/'NOTICE.md').read_text())
  manifest=json.loads((folder/'MANIFEST.json').read_text())
  self.assertEqual(sorted(manifest),['payload/'+c for c in chosen]);self.assertTrue(all(v['license']=='non-free' for v in manifest.values()))
  self.assertTrue((folder/'payload/basic/sounds/stereo/start.wav').is_file())

if __name__=='__main__':unittest.main()
