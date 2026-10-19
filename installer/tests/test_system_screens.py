"""The system login and boot screens are managed by one account's installation."""
import importlib.util,json,os,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import system_profile
spec=importlib.util.spec_from_file_location('screens_root_helper',ROOT/'root_helper.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
FEATURES=json.loads((ROOT/'features.json').read_text())
# Everything login and boot need is present, so only the marker decides.
PROFILE={'name':'Kubuntu','family':'apt','arch':'amd64','plasma':(6,6,4),'qt':(6,10,2),'writer':'/usr/bin/kwriteconfig6',
         'dbus':'/usr/bin/qdbus6','kde':True,'boot':'update-initramfs','splash':True,'systemd':True}

class Screens(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
  self.lib=Path(self.temp.name)/'kde-xp-suite';self.lib.mkdir()
  self.marker=self.lib/'screens.json'
  patcher=patch.object(system_profile,'SCREENS_MARKER',self.marker);patcher.start();self.addCleanup(patcher.stop)
  patcher=patch.object(r,'account_name',lambda uid:{1000:'owner',1001:'kat'}.get(uid,'user '+str(uid)));patcher.start();self.addCleanup(patcher.stop)
 def store(self,uid,features=None):
  store=self.lib/str(uid);store.mkdir(exist_ok=True)
  if features is not None:(store/'system.json').write_text(json.dumps({'features':features,'paths':[]}))
  return store
 def support(self):
  return system_profile.capabilities(PROFILE,FEATURES)

 def test_without_a_manager_the_screens_are_available(self):
  self.assertEqual((self.support()['login'],self.support()['boot']),('',''))

 def test_another_accounts_screens_are_unavailable_here_with_its_name(self):
  self.marker.write_text(json.dumps({'uid':os.getuid()+1,'user':'owner','features':['boot','login']}))
  support=self.support()
  for name in ['login','boot']:self.assertIn('owner account',support[name])
  self.assertEqual(support['lock'],'',"only the system screens")

 def test_the_managing_account_keeps_them_for_an_upgrade(self):
  self.marker.write_text(json.dumps({'uid':os.getuid(),'user':'me','features':['login']}))
  self.assertEqual(self.support()['login'],'')

 def test_an_unreadable_marker_is_ignored(self):
  for text in ['','{','[]','{"user":"owner"}','{"uid":"x"}']:
   self.marker.write_text(text)
   self.assertEqual(self.support()['login'],'',text)

 def test_installing_the_screens_publishes_and_removal_withdraws(self):
  owner=self.store(1000)
  r.publish(owner,['login','boot'])
  self.assertEqual(json.loads(self.marker.read_text()),{'uid':1000,'user':'owner','features':['boot','login']})
  self.assertEqual(self.marker.stat().st_mode&0o777,0o644)
  r.withdraw(self.store(1001));self.assertTrue(self.marker.exists(),"another account's removal leaves it")
  r.withdraw(owner);self.assertFalse(self.marker.exists())

 def test_a_second_account_stops_before_anything_is_installed(self):
  self.store(1000,['boot','login'])
  with self.assertRaisesRegex(RuntimeError,'owner account.*Clear XP Welcome login screen and XP boot splash'):
   r.check_screens(self.store(1001),['desktop','login'])
  # An installation from before screens.json existed now has one.
  self.assertEqual(json.loads(self.marker.read_text())['uid'],1000)

 def test_other_features_and_the_managing_account_are_not_stopped(self):
  self.store(1000,['boot','login'])
  r.check_screens(self.store(1001),['desktop','icons'])
  r.check_screens(self.store(1000),['login','boot'])
  self.assertFalse(self.marker.exists())

 def test_a_removed_installation_no_longer_manages_them(self):
  owner=self.store(1000,['login']);(owner/'system.json').rename(owner/'system-restored.json')
  r.check_screens(self.store(1001),['login'])

if __name__=='__main__':unittest.main()
