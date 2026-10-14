import hashlib,json,os,sys,tempfile,unittest,subprocess
from pathlib import Path
from unittest.mock import patch
BASE=Path(__file__).resolve().parents[1];sys.path.insert(0,str(BASE));import core,publish
class UpdatesTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.keys=tempfile.TemporaryDirectory();cls.key=Path(cls.keys.name)/'key.pem';cls.pub=Path(cls.keys.name)/'pub.pem'
  subprocess.run(['openssl','genpkey','-algorithm','RSA','-pkeyopt','rsa_keygen_bits:2048','-out',str(cls.key)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  subprocess.run(['openssl','pkey','-in',str(cls.key),'-pubout','-out',str(cls.pub)],check=True,stdout=subprocess.DEVNULL)
 @classmethod
 def tearDownClass(cls):cls.keys.cleanup()
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name);self.home=self.root/'home';self.home.mkdir();self.state=self.home/'.local/share/kubuntu-xp-suite';self.state.mkdir(parents=True)
  self.path='.local/share/xp-accessories/notepad.py';dest=self.home/self.path;dest.parent.mkdir(parents=True);dest.write_text('old code')
  core.write_json(self.state/'installation.json',{'version':2,'features':['notepad'],'theme':'Luna Silver'})
  core.write_json(self.state/'before.json',{'original':'untouched'});core.write_json(self.state/'after.json',{self.path:{'kind':'file','value':core.digest(dest)}})
  self.source=self.root/'source.py';self.source.write_text('new code');self.spec=self.root/'spec.json';self.output=self.root/'release'
 def release(self,files=None,version='0.1.0'):
  files=files or [{'source':str(self.source),'target':self.path,'feature':'notepad'}]
  core.write_json(self.spec,{'version':version,'files':files});publish.build(self.spec,self.key,self.output);return core.verify_release(self.output,self.pub)
 def test_signature_and_archive_tampering(self):
  self.release();p=self.output/'release.json';p.write_text(p.read_text().replace('0.1.0','0.2.0'))
  with self.assertRaises(core.UpdateError):core.verify_release(self.output,self.pub)
  self.release();(self.output/'payload.tar.gz').write_bytes(b'damaged')
  with self.assertRaises(core.UpdateError):core.verify_release(self.output,self.pub)
 def test_preserve_choices_settings_and_original_uninstall(self):
  self.release();config=self.home/'.config/kdeglobals';config.parent.mkdir();config.write_text('my theme settings')
  original=(self.state/'before.json').read_bytes();tx=core.apply(self.home,self.output,key=self.pub)
  self.assertEqual((self.home/self.path).read_text(),'new code');self.assertEqual(config.read_text(),'my theme settings');self.assertEqual((self.state/'before.json').read_bytes(),original)
  self.assertEqual(json.loads((self.state/'installation.json').read_text())['theme'],'Luna Silver');core.rollback_locked(self.home,tx);self.assertEqual((self.home/self.path).read_text(),'old code')
 def test_locally_modified_files_block_update(self):
  data=self.release();(self.home/self.path).write_text('my local improvement');self.assertEqual(len(core.plan(self.home,data)['conflicts']),1)
  with self.assertRaises(core.UpdateError):core.apply(self.home,self.output,key=self.pub)
 def test_optional_features_are_explicit(self):
  extra={ 'source':str(self.source),'target':'.local/share/xp-accessories/calculator.py','feature':'calculator','optional':True}
  self.release([{'source':str(self.source),'target':self.path,'feature':'notepad'},extra]);core.apply(self.home,self.output,key=self.pub)
  self.assertFalse((self.home/extra['target']).exists());self.assertEqual(json.loads((self.state/'installation.json').read_text())['features'],['notepad'])
 def test_explicit_optional_feature_is_rollbackable(self):
  extra={ 'source':str(self.source),'target':'.local/share/xp-accessories/calculator.py','feature':'calculator','optional':True};self.release([extra]);tx=core.apply(self.home,self.output,['calculator'],key=self.pub)
  self.assertIn('calculator',json.loads((self.state/'installation.json').read_text())['features']);core.rollback_locked(self.home,tx);self.assertFalse((self.home/extra['target']).exists());self.assertEqual(json.loads((self.state/'installation.json').read_text())['features'],['notepad'])
 def test_symlink_escape_and_settings_targets_rejected(self):
  for path in ['.config/kdeglobals','@state/../../bad','.local/share/xp-accessories/../../bad']:
   with self.assertRaises(core.UpdateError):core.target(self.home,self.state,path)
  (self.home/'.local/share/xp-accessories/link').symlink_to(self.root,target_is_directory=True)
  with self.assertRaises(core.UpdateError):core.target(self.home,self.state,'.local/share/xp-accessories/link/evil')
 def test_wrong_native_arch_and_qt_cannot_install(self):
  self.release([{'source':str(self.source),'target':self.path,'feature':'notepad','arch':'amd64','qt':'6.10'}])
  with self.assertRaises(core.UpdateError):core.apply(self.home,self.output,key=self.pub,arch='arm64',qt='6.11')
 def test_rollback_preserves_later_edits(self):
  self.release();tx=core.apply(self.home,self.output,key=self.pub);(self.home/self.path).write_text('later edit')
  with self.assertRaises(core.UpdateError):core.rollback_locked(self.home,tx)
  self.assertEqual((self.home/self.path).read_text(),'later edit')
 def test_partial_failure_rolls_back(self):
  extra={'source':str(self.source),'target':'.local/share/xp-accessories/extra.py','feature':'notepad'};self.release([{'source':str(self.source),'target':self.path,'feature':'notepad'},extra]);original=core.atomic
  def fail(path,data,mode=0o644):
   if Path(path).name=='extra.py':raise OSError('disk full')
   return original(path,data,mode)
  with patch('core.atomic',side_effect=fail):
   with self.assertRaises(OSError):core.apply(self.home,self.output,key=self.pub)
  self.assertEqual((self.home/self.path).read_text(),'old code');self.assertFalse((self.home/extra['target']).exists())
 def test_interrupted_journal_recovers(self):
  self.release();tx=core.apply(self.home,self.output,key=self.pub);j=json.loads((tx/'journal.json').read_text());j['status']='applying';core.write_json(tx/'journal.json',j);core.recover(self.home);self.assertEqual((self.home/self.path).read_text(),'old code')
 def test_addition_updates_existing_accessory_selection_marker(self):
  marker=self.home/'.local/share/xp-accessories/installed-apps.json';marker.write_text('["notepad"]')
  item={'source':str(self.source),'target':'.local/share/xp-accessories/calculator.py','feature':'calculator','optional':True};self.release([item]);tx=core.apply(self.home,self.output,['calculator'],key=self.pub)
  self.assertEqual(json.loads(marker.read_text()),['calculator','notepad']);core.rollback_locked(self.home,tx);self.assertEqual(json.loads(marker.read_text()),['notepad'])
 def test_shared_code_and_multiple_architecture_variants(self):
  info=json.loads((self.state/'installation.json').read_text());info['features']=['calculator'];core.write_json(self.state/'installation.json',info)
  item={'source':str(self.source),'target':self.path,'feature':'notepad','features_any':['calculator'],'arch':'amd64'}
  other={**item,'arch':'arm64'};data=self.release([item,other]);p=core.plan(self.home,data,arch='amd64');self.assertEqual(len(p['entries']),1);self.assertEqual(p['skipped'],[])
 def test_bootstrapped_client_can_update_itself(self):
  path='.local/share/xplasma-updater/app/core.py';p=self.home/path;p.parent.mkdir(parents=True);p.write_text('old client');core.write_json(self.home/'.local/share/xplasma-updater/client-files.json',{path:core.digest(p)})
  self.release([{'source':str(self.source),'target':path,'feature':'updates'}]);tx=core.apply(self.home,self.output,key=self.pub);self.assertEqual(p.read_text(),'new code');core.rollback_locked(self.home,tx);self.assertEqual(p.read_text(),'old client')
 def test_authenticated_patch_preserves_surrounding_code_and_rolls_back(self):
  path='.local/share/plasma/plasmoids/org.kde.desktopcontainment/contents/ui/FolderView.qml';dest=self.home/path;dest.parent.mkdir(parents=True);dest.write_text('prefix OLD suffix')
  core.write_json(self.state/'installation.json',{'features':['desktop']});core.write_json(self.state/'after.json',{path:{'kind':'file','value':core.digest(dest)}})
  self.source.write_text('[{"find":"OLD","replace":"NEW"}]')
  data=self.release([{'source':str(self.source),'target':path,'feature':'desktop','operation':'text_patch'}]);tx=core.apply(self.home,self.output,key=self.pub)
  self.assertEqual(dest.read_text(),'prefix NEW suffix');core.rollback_locked(self.home,tx);self.assertEqual(dest.read_text(),'prefix OLD suffix')
 def test_unknown_patch_anchor_blocks_without_writes(self):
  path='.local/share/plasma/plasmoids/org.kde.desktopcontainment/contents/ui/FolderView.qml';dest=self.home/path;dest.parent.mkdir(parents=True);dest.write_text('unknown code')
  core.write_json(self.state/'installation.json',{'features':['desktop']});core.write_json(self.state/'after.json',{path:{'kind':'file','value':core.digest(dest)}});self.source.write_text('[{"find":"OLD","replace":"NEW"}]')
  data=self.release([{'source':str(self.source),'target':path,'feature':'desktop','operation':'text_patch'}]);self.assertTrue(core.plan(self.home,data)['conflicts'])
  with self.assertRaises(core.UpdateError):core.apply(self.home,self.output,key=self.pub)
  self.assertEqual(dest.read_text(),'unknown code')
 def test_unit_state_template_is_machine_independent_and_rollbackable(self):
  path='.config/systemd/user/xp-session-theme.service';self.source.write_text('state={{STATE_NAME}}')
  data=self.release([{'source':str(self.source),'target':path,'feature':'notepad','operation':'state_template'}]);tx=core.apply(self.home,self.output,key=self.pub)
  self.assertEqual((self.home/path).read_text(),'state=kubuntu-xp-suite');core.rollback_locked(self.home,tx);self.assertFalse((self.home/path).exists())
 def test_no_arbitrary_patch_template_or_command_targets(self):
  self.source.write_text('[{"find":"OLD","replace":"NEW"}]')
  for operation in ['text_patch','state_template','run_command']:
   with self.assertRaises(core.UpdateError):self.release([{'source':str(self.source),'target':self.path,'feature':'notepad','operation':operation}])
  for path in ['.config/systemd/user/other.service','.local/share/firefox-esr-ie6/firefox/ie6-ui.cfg.evil']:
   with self.assertRaises(core.UpdateError):core.target(self.home,self.state,path)
 def test_latest_stable_selects_feature_release_instead_of_bridge_pin(self):
  assets=[{'name':n} for n in ['release.json','release.sig','payload.tar.gz']]
  releases=[{'tag_name':'v0.1.1','assets':assets},{'tag_name':'v0.2.0','assets':assets},{'tag_name':'v9.0.0','prerelease':True,'assets':assets},{'tag_name':'v8.0.0','assets':[]}]
  with patch('core.github_json',return_value=releases):self.assertEqual(core.latest('owner/repo')['tag_name'],'v0.2.0')
 def test_restored_completed_backup_updates_and_rolls_back(self):
  self.release();core.apply(self.home,self.output,key=self.pub)
  (self.home/self.path).write_text('old code');self.source.write_text('third code');data=self.release(version='0.2.0')
  self.assertFalse(core.plan(self.home,data)['conflicts'])
  tx=core.apply(self.home,self.output,key=self.pub);core.rollback_locked(self.home,tx)
  self.assertEqual((self.home/self.path).read_text(),'old code')
 def test_tampered_backup_cannot_approve_local_edits(self):
  self.release();tx=core.apply(self.home,self.output,key=self.pub)
  (self.home/self.path).write_text('old code');(tx/'backup'/self.path).write_text('tampered')
  self.source.write_text('third code');data=self.release(version='0.2.0')
  self.assertTrue(core.plan(self.home,data)['conflicts'])
 def test_missing_session_bus_defers_install_and_allows_rollback(self):
  path='.config/systemd/user/xp-session-theme.path';self.source.write_text('[Install]\nWantedBy=default.target\n')
  self.release([{'source':str(self.source),'target':path,'feature':'notepad'}])
  response=subprocess.CompletedProcess([],1,'','Failed to connect to user scope bus: No data available')
  run=subprocess.run
  def systemctl(args,**kwargs):
   return response if args[0]=='systemctl' else run(args,**kwargs)
  with patch('core.subprocess.run',side_effect=systemctl):
   tx=core.apply(self.home,self.output,key=self.pub)
   self.assertTrue(json.loads((tx/'journal.json').read_text())['activation_deferred'])
   link=self.home/'.config/systemd/user/default.target.wants/xp-session-theme.path'
   self.assertTrue(link.is_symlink());core.rollback_locked(self.home,tx)
   self.assertFalse(link.is_symlink());self.assertEqual(json.loads((tx/'journal.json').read_text())['status'],'rolled-back')
 def test_systemctl_absolute_enablement_link_is_accepted(self):
  units=self.home/'.config/systemd/user';link=units/'default.target.wants/xp-session-theme.path'
  link.parent.mkdir(parents=True);link.symlink_to(units/'xp-session-theme.path')
  with patch('core.subprocess.run',return_value=subprocess.CompletedProcess([],0,'','')):
   self.assertFalse(core.selection_units(True,self.home))
  self.assertEqual(link.readlink(),units/'xp-session-theme.path')
  link.unlink();link.symlink_to('/elsewhere/xp-session-theme.path')
  with self.assertRaises(core.UpdateError):core.selection_units(True,self.home)
 def test_real_service_failure_is_not_hidden(self):
  response=subprocess.CompletedProcess([],1,'','Unit failed due to invalid configuration')
  with patch('core.subprocess.run',return_value=response):
   with self.assertRaises(core.UpdateError):core.selection_units(True,self.home)
 def test_no_downgrade(self):
  self.release();core.apply(self.home,self.output,key=self.pub)
  with self.assertRaises(core.UpdateError):core.plan(self.home,core.verify_release(self.output,self.pub))
if __name__=='__main__':unittest.main()
