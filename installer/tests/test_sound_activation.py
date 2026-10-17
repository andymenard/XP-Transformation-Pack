import ast,tempfile,unittest,json
from pathlib import Path
from unittest.mock import Mock
ROOT=Path(__file__).resolve().parents[1]
class SoundActivation(unittest.TestCase):
 def prepare(self,selected,fail=False):
  tree=ast.parse((ROOT/'backend.py').read_text());node=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='prepare_native')
  commands=[]
  def run(args,**kwargs):
   commands.append(args)
   if fail and 'sounds' in args:raise RuntimeError('Unable to generate sound theme')
  with tempfile.TemporaryDirectory() as tmp:
   home=Path(tmp);(home/'.local/share/xplasma').mkdir(parents=True)
   ns=dict(Path=Path,json=json,BASE=home,FEATURES=[],LOOK={'desktop','sounds'},copy=Mock(),prepare_wallpapers=Mock(),phase=Mock(),run=run,presets=lambda h:home,look_args=lambda *a:['apply-desktop'],shutil=Mock())
   exec(compile(ast.Module(body=[node],type_ignores=[]),'backend.py','exec'),ns)
   ns['prepare_native'](home,home/'state',selected,'Luna')
  return commands
 def test_explicit_activation_follows_desktop_apply(self):
  commands=self.prepare({'desktop','sounds'});self.assertEqual(commands[-1][-2:],['sounds','Windows Default']);self.assertLess(commands.index(['apply-desktop']),len(commands)-1)
 def test_unselected_sounds_remain_untouched(self):
  self.assertFalse(any('sounds' in c for c in self.prepare({'desktop'})))
 def test_sound_failure_is_not_reported_as_success(self):
  with self.assertRaisesRegex(RuntimeError,'Unable to generate'):self.prepare({'desktop','sounds'},fail=True)
if __name__=='__main__':unittest.main()
