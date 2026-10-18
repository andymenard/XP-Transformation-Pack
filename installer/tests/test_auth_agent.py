"""KDE's password prompt is restarted when the XP fonts change under it."""
import importlib.util,sys,tempfile,types,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
spec=importlib.util.spec_from_file_location('agent_backend',ROOT/'backend.py')
b=importlib.util.module_from_spec(spec)
with patch.dict(sys.modules,{'tomlkit':types.SimpleNamespace()}):spec.loader.exec_module(b)
UNIT='plasma-polkit-agent.service'

class AuthAgent(unittest.TestCase):
 def calls(self,state):
  seen=[]
  def run(args,capture=False,check=True,env=None):
   seen.append([str(a) for a in args])
   return state if capture else 0
  return seen,run

 def test_a_running_agent_is_restarted(self):
  seen,run=self.calls('active')
  with patch.object(b,'run',run):b.restart_auth_agent()
  self.assertIn(['systemctl','--user','restart',UNIT],seen)

 def test_one_that_isnt_running_is_left_alone(self):
  for state in ['inactive','failed','']:
   seen,run=self.calls(state)
   with patch.object(b,'run',run):b.restart_auth_agent()
   self.assertNotIn(['systemctl','--user','restart',UNIT],seen)

 def test_remove_and_rollback_restart_it_with_the_shell(self):
  seen,run=self.calls('active')
  with tempfile.TemporaryDirectory() as folder,patch.object(b,'run',run):b.refresh(Path(folder),Path(folder))
  self.assertIn(['systemctl','--user','restart',UNIT],seen)
  self.assertGreater(seen.index(['systemctl','--user','restart',UNIT]),seen.index(['systemctl','--user','restart','plasma-plasmashell.service']))

 def test_each_password_prompt_comes_from_a_restarted_agent(self):
  # (An agent left over from before an earlier Remove draws the first prompt too.)
  seen,run=self.calls('active')
  with patch.object(b,'run',run),patch('time.sleep') as slept:b.root('dependencies',{'taskbar'})
  restart=seen.index(['systemctl','--user','restart',UNIT]);pkexec=next(i for i,c in enumerate(seen) if c[0]=='pkexec')
  self.assertLess(restart,pkexec);slept.assert_called_once_with(1)

 def test_installing_the_xp_fonts_restarts_it(self):
  seen,run=self.calls('active')
  with tempfile.TemporaryDirectory() as folder:
   home=Path(folder)/'home';home.mkdir()
   fonts=Path(folder)/'bundle/payload/assets/fonts';fonts.mkdir(parents=True)
   with patch.object(b,'run',run),patch.object(b,'BASE',Path(folder)/'bundle'),patch.object(b,'config',lambda *a:None),\
        patch.dict(sys.modules,{'tahoma_os':types.SimpleNamespace(generate=lambda *a:None)}),patch('shutil.which',lambda name:None):
    b.basic(home,{'fonts'},native=True)
  self.assertIn(['systemctl','--user','restart',UNIT],seen)
  self.assertGreater(seen.index(['systemctl','--user','restart',UNIT]),seen.index(['fc-cache','-f']))

if __name__=='__main__':unittest.main()
