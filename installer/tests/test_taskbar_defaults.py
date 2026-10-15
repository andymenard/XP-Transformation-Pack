import ast,json,re,tempfile,unittest
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]
class TaskbarDefaultsTests(unittest.TestCase):
 def test_exactly_three_launchers_with_optional_ie6_browser(self):
  tree=ast.parse((BASE/'backend.py').read_text());function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='taskbar_defaults')
  commands=[];ns={'Path':Path,'json':json,'kdbus':lambda:'qdbus','run':lambda args:commands.append(args)}
  exec(compile(ast.Module(body=[function],type_ignores=[]),'backend.py','exec'),ns)
  with tempfile.TemporaryDirectory() as tmp:
   home=Path(tmp);entries=home/'.local/share/applications';entries.mkdir(parents=True)
   for name in ['systemsettings.desktop','org.kde.dolphin.desktop','firefox.desktop','firefox-esr-ie6.desktop']:(entries/name).write_text('[Desktop Entry]\n')
   for selected,browser in [({'firefox'},'firefox-esr-ie6.desktop'),(set(),'firefox.desktop')]:
    ns['taskbar_defaults'](home,selected)
    script=commands[-1][-1];urls=json.loads(re.search(r'writeConfig\("launcherUrls",(\[.*?\])\)',script).group(1))
    self.assertEqual(urls,[(entries/name).as_uri() for name in ['systemsettings.desktop','org.kde.dolphin.desktop',browser]])
    self.assertIn('quick.index=start.index+1',script)
    self.assertIn('writeConfig("launchers",[])',script)
if __name__=='__main__':unittest.main()
