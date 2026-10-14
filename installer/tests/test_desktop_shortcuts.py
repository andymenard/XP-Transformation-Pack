import ast,json,os,re,shutil,tarfile,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
class ShortcutTests(unittest.TestCase):
 def test_shortcut_icons_exist_and_legacy_launchers_are_repaired(self):
  tree=ast.parse((ROOT/'backend.py').read_text())
  functions=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in {'copy','shortcuts'}]
  with tempfile.TemporaryDirectory() as folder:
   base=Path(folder);home=base/'home';state=home/'.local/share/kde-xp-suite';desktop=home/'Desktop';desktop.mkdir(parents=True)
   payload=base/'payload/assets/desktop-icons';payload.mkdir(parents=True)
   for name in ['computer','documents','network','trash-full','trash-empty','browser']:(payload/(name+'.png')).write_bytes(('XP '+name).encode())
   legacy=desktop/'My Computer.desktop';legacy.write_text('[Desktop Entry]\nType=Link\nName=My Computer\nIcon='+str(state/'desktop-icons/computer.png')+'\nURL=file:///\nX-Custom=keep\n')
   custom=desktop/'My Documents.desktop';custom.write_text('[Desktop Entry]\nIcon=custom-folder\nURL=file:///custom\n');saved=custom.read_bytes()
   ns={'Path':Path,'BASE':base,'shutil':shutil,'tempfile':tempfile,'os':os,'re':re,'tarfile':tarfile,'desktop_path':lambda _:desktop,'log':lambda _:None}
   exec(compile(ast.Module(body=functions,type_ignores=[]),'backend.py','exec'),ns)
   with patch('shutil.which',return_value=None):ns['shortcuts'](home,state,{'firefox'},{})
   self.assertEqual(custom.read_bytes(),saved);self.assertIn('X-Custom=keep',legacy.read_text())
   for name in ['My Computer','My Network Places','Recycle Bin','Internet Explorer']:
    text=(desktop/(name+'.desktop')).read_text()
    for line in text.splitlines():
     if line.startswith(('Icon=','EmptyIcon=')):
      icon=Path(line.split('=',1)[1]);self.assertTrue(icon.is_file(),str(icon));self.assertNotIn(str(state),str(icon))
