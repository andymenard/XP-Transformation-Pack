# SPDX-License-Identifier: GPL-3.0-or-later
import hashlib,json,sys,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import core,publish
import test_updates as fixtures

class NativeSetupTests(unittest.TestCase):
 setUpClass=classmethod(fixtures.UpdatesTests.setUpClass.__func__)
 tearDownClass=classmethod(fixtures.UpdatesTests.tearDownClass.__func__)
 setUp=fixtures.UpdatesTests.setUp
 script='.local/share/xp-accessories/desktop-update/setup-desktop.py'
 backup='.local/share/xplasma/maintenance/desktop-0.3.1'
 binary='.local/lib/qt6/plugins/styles/libxplasma.so'
 def native_release(self,fail=False,metadata=None,version='0.3.1'):
  core.write_json(self.state/'installation.json',{'features':['desktop'],'theme':'Luna Silver'})
  old=self.home/self.binary;old.parent.mkdir(parents=True);old.write_text('old native')
  script=self.root/'setup-desktop.py'
  script.write_text('''from pathlib import Path
import hashlib,json,shutil,sys
home=Path.cwd();name='''+repr(self.binary)+''';path=home/name
backup=home/'''+repr(self.backup)+'''
backup.mkdir(parents=True)
def digest(p):return {'kind':'file','value':hashlib.sha256(p.read_bytes()).hexdigest()}
before={name:digest(path)}
saved=backup/'backup'/name;saved.parent.mkdir(parents=True);shutil.copy2(path,saved)
(backup/'before.json').write_text(json.dumps(before))
print('Building matching native components',flush=True)
path.write_text('new native')
(backup/'after.json').write_text(json.dumps({name:digest(path)}))
print('Native installation finished',flush=True)
sys.exit('''+('7' if fail else '0')+''')
''')
  inventory=self.root/'files.json';inventory.write_text(json.dumps({'setup-desktop.py':core.digest(script)}))
  spec={'version':version,'files':[{'source':str(script),'target':self.script,'feature':'desktop'},{'source':str(inventory),'target':str(Path(self.script).parent/'files.json'),'feature':'desktop'},{'source':str(self.source),'target':self.path,'feature':'desktop'}]}
  if metadata:spec['native_setup']=metadata
  core.write_json(self.spec,spec);publish.build(self.spec,self.key,self.output)
 def apply(self,**kwargs):
  with patch.object(core,'properties_running',return_value=False):return core.apply(self.home,self.output,key=self.pub,**kwargs)
 def test_single_install_runs_setup_streams_output_and_rolls_back_both(self):
  self.native_release();messages=[];tx=self.apply(progress=messages.append)
  self.assertEqual((self.home/self.binary).read_text(),'new native');self.assertEqual((self.home/self.path).read_text(),'new code')
  self.assertTrue(any('Building matching' in line for line in messages))
  self.assertEqual(json.loads((tx/'journal.json').read_text())['status'],'complete')
  core.rollback_locked(self.home,tx)
  self.assertEqual((self.home/self.binary).read_text(),'old native');self.assertEqual((self.home/self.path).read_text(),'old code')
  self.assertFalse((self.home/self.backup).exists())
 def test_failed_native_step_undoes_both_stages_and_can_be_retried(self):
  self.native_release(fail=True)
  with self.assertRaisesRegex(core.UpdateError,'Native setup failed'):self.apply()
  self.assertEqual((self.home/self.binary).read_text(),'old native');self.assertEqual((self.home/self.path).read_text(),'old code')
  self.assertFalse((self.home/'.local/share/xplasma-updater/current.json').exists())
  self.assertFalse((self.home/self.backup).exists())
 def test_open_properties_blocks_before_any_update_writes(self):
  self.native_release()
  with patch.object(core,'properties_running',return_value=True):
   with self.assertRaisesRegex(core.UpdateError,'Close XPlasma Properties'):core.apply(self.home,self.output,key=self.pub)
  self.assertEqual((self.home/self.path).read_text(),'old code')
 def test_rollback_preserves_later_native_edits_and_leaves_ota_intact(self):
  self.native_release();tx=self.apply();(self.home/self.binary).write_text('user modification')
  with self.assertRaisesRegex(core.UpdateError,'later edits'):core.rollback_locked(self.home,tx)
  self.assertEqual((self.home/self.binary).read_text(),'user modification');self.assertEqual((self.home/self.path).read_text(),'new code')
 def test_interrupted_combined_transaction_recovers_both_stages(self):
  self.native_release();tx=self.apply();journal=json.loads((tx/'journal.json').read_text());journal['status']='native-applying';core.write_json(tx/'journal.json',journal)
  core.recover(self.home)
  self.assertEqual((self.home/self.binary).read_text(),'old native');self.assertEqual((self.home/self.path).read_text(),'old code')
 def test_older_ota_can_finish_setup_inside_new_client(self):
  self.native_release()
  with patch.object(core,'run_native_setup'):tx=self.apply()
  self.assertIsNotNone(core.pending_setup(self.home))
  with patch.object(core,'properties_running',return_value=False):self.assertEqual(core.finish_pending(self.home),tx)
  self.assertEqual((self.home/self.binary).read_text(),'new native');self.assertIsNone(core.pending_setup(self.home))
 def test_pending_setup_failure_preserves_previously_installed_ota(self):
  self.native_release(fail=True)
  with patch.object(core,'run_native_setup'):self.apply()
  with patch.object(core,'properties_running',return_value=False):
   with self.assertRaisesRegex(core.UpdateError,'Native setup failed'):core.finish_pending(self.home)
  self.assertEqual((self.home/self.binary).read_text(),'old native');self.assertEqual((self.home/self.path).read_text(),'new code')
  self.assertIsNotNone(core.pending_setup(self.home))
 def test_signed_setup_metadata_is_preserved_and_unsafe_paths_rejected(self):
  descriptor={'script':self.script,'backup':self.backup};self.native_release(metadata=descriptor,version='0.3.2')
  self.assertEqual(core.verify_release(self.output,self.pub)['native_setup'],descriptor)
  bad={'script':'.config/evil.py','backup':self.backup}
  spec=json.loads(self.spec.read_text());spec['native_setup']=bad;core.write_json(self.spec,spec)
  with self.assertRaises(core.UpdateError):publish.build(self.spec,self.key,self.output)
 def test_pending_setup_rejects_changed_script_before_execution(self):
  self.native_release()
  with patch.object(core,'run_native_setup'):self.apply()
  (self.home/self.script).write_text('untrusted replacement')
  with patch.object(core,'properties_running',return_value=False),patch.object(core.subprocess,'Popen') as spawn:
   with self.assertRaisesRegex(core.UpdateError,'missing or modified'):core.finish_pending(self.home)
   spawn.assert_not_called()
  self.assertEqual((self.home/self.binary).read_text(),'old native')
 def test_setup_is_not_selected_for_an_uninstalled_feature(self):
  self.native_release();core.write_json(self.state/'installation.json',{'features':['notepad']})
  data=core.verify_release(self.output,self.pub)
  self.assertIsNone(core.plan(self.home,data)['setup'])
