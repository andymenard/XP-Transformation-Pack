"""XP interface choices must not replace generic fonts used by web content."""
import ast, tempfile, unittest, sys, types
from pathlib import Path
from unittest.mock import Mock
from xml.etree import ElementTree
ROOT=Path(__file__).resolve().parents[1]
class FontDefaults(unittest.TestCase):
 def test_fonts_keep_explicit_desktop_choices_without_generic_redirects(self):
  tree=ast.parse((ROOT/'backend.py').read_text())
  node=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='basic')
  config=Mock();copy=Mock()
  namespace=dict(Path=Path,BASE=ROOT,copy=copy,config=config,run=Mock(),shutil=Mock(which=Mock(return_value=None)))
  exec(compile(ast.Module(body=[node],type_ignores=[]),'backend.py','exec'),namespace)
  from unittest.mock import patch
  with patch.dict(sys.modules,{'tahoma_os':types.SimpleNamespace(generate=Mock())}), tempfile.TemporaryDirectory() as directory:
   home=Path(directory)
   namespace['basic'](home,{'fonts'},native=True)
   path=home/'.config/fontconfig/conf.d/99-xp-fonts.conf'
   document=ElementTree.parse(path).getroot()
   self.assertEqual(document.findall('alias'),[])
   self.assertEqual(document.findall('match'),[])
   self.assertTrue(any(call.args[2]=='menuFont' and call.args[3].startswith('Tahoma OS,') for call in config.call_args_list))
   self.assertFalse(any('xp-fonts.conf' in str(call.args[0]) for call in copy.call_args_list))
if __name__=='__main__':unittest.main()
