"""Bounds and safety checks for disposable caches; all fixtures live in a temp home."""
import importlib.util,json,os,tempfile,time,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('retention',ROOT/'tools/cache-maintenance/cleanup.py')
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
class RetentionTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.home=Path(self.temp.name);self.now=time.time()
 def tearDown(self):self.temp.cleanup()
 def file(self,relative,data=b'x',age=10000):
  p=self.home/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data);os.utime(p,(self.now-age,)*2);return p
 def folder(self,relative,age=10000):
  p=self.file(relative+'/data',b'x'*10,age).parent;os.utime(p,(self.now-age,)*2);return p
 def state(self,ids,active=''):
  self.file('.local/share/xplasma/state.json',json.dumps({'settings':{'generatedLooks':{i:'source' for i in ids},'appliedPackageId':active}}).encode())
 def backup(self,index,age=10000,size=10,complete=True):
  rel=f'.local/share/xplasma/maintenance/test-20260101-{index:06d}'
  p=self.file(rel+'/data',b'x'*size,age).parent
  (p/r.MARKER).write_text(json.dumps({'kind':'maintenance','complete':complete}));os.utime(p,(self.now-age,)*2);return p
 def test_defaults_match_all_packaged_copies(self):
  expected=(ROOT/'tools/cache-maintenance/cleanup.py').read_bytes()
  for p in [ROOT/'installer/cache_maintenance.py',ROOT/'native/src/app/data/cache_maintenance.py']:self.assertEqual(expected,p.read_bytes())
 def test_count_limit_and_dry_run_leave_sources_and_active_look(self):
  ids=['xplasma-v2-'+str(i) for i in range(22)];self.state(ids,ids[0])
  for i,name in enumerate(ids):self.folder('.local/share/plasma/desktoptheme/'+name+'-taskbar',age=10000-i)
  source=self.file('.local/share/xplasma/custom-luna/user.msstyles',b'KEEP')
  dry=r.cleanup(self.home);self.assertEqual(len(dry['looks']['removed']),5);self.assertEqual(len(list((self.home/'.local/share/plasma/desktoptheme').iterdir())),22)
  real=r.cleanup(self.home,apply=True);self.assertEqual(real['looks']['after_count'],16)
  self.assertTrue((self.home/'.local/share/plasma/desktoptheme'/ (ids[0]+'-taskbar')).exists());self.assertEqual(source.read_bytes(),b'KEEP')
 def test_actual_kde_references_protect_components(self):
  ids=['xplasma-v2-a','xplasma-v2-b'];self.state(ids)
  for name in ids:self.folder('.local/share/themes/XPlasma-'+name)
  self.file('.config/gtk-3.0/settings.ini',b'[Settings]\ngtk-theme-name=XPlasma-xplasma-v2-a\n')
  report=r.cleanup(self.home,True,limits={**r.LIMITS,'looks':(0,0)})
  self.assertIn(ids[0],report['looks']['protected']);self.assertTrue((self.home/'.local/share/themes/XPlasma-xplasma-v2-a').exists());self.assertFalse((self.home/'.local/share/themes/XPlasma-xplasma-v2-b').exists())
 def test_dark_size_limit_preserves_live_build(self):
  root='.cache/xplasma/dark-mode/';names=[str(i)*64 for i in range(3)]
  for i,name in enumerate(names):
   p=self.folder(root+name,10000-i);(p/'source.json').write_text('{"source":"saved.msstyles"}')
  self.file('.config/xplasmarc',('[Theme]\nSource='+str(self.home/root/names[0]/'applications.msstyles')+'\n').encode())
  report=r.cleanup(self.home,True,limits={**r.LIMITS,'dark':(8,1)})
  self.assertEqual(len(report['dark']['removed']),2);self.assertTrue((self.home/root/names[0]).exists())
 def test_backup_count_keeps_newest_and_never_installer_recovery(self):
  backups=[self.backup(i,age=10000-i) for i in range(8)]
  recovery=self.file('.local/share/kde-xp-suite/backup/original',b'INSTALLER')
  baseline=self.folder('.local/share/xplasma/maintenance/icon-coverage')
  report=r.cleanup(self.home,True);self.assertEqual(report['backups']['after_count'],5)
  self.assertTrue(backups[-1].exists());self.assertFalse(backups[0].exists());self.assertTrue(baseline.exists());self.assertEqual(recovery.read_bytes(),b'INSTALLER')
 def test_size_limit_keeps_one_large_newest_recovery(self):
  older=self.backup(1,size=500);newest=self.backup(2,age=9000,size=500)
  report=r.cleanup(self.home,True,limits={**r.LIMITS,'backups':(5,10)})
  self.assertFalse(older.exists());self.assertTrue(newest.exists());self.assertEqual(report['backups']['after_count'],1)
 def test_incomplete_pinned_and_recent_backups_survive(self):
  incomplete=self.backup(1,complete=False);pinned=self.backup(2);(pinned/'.keep').touch();recent=self.backup(3,age=0)
  r.cleanup(self.home,True,limits={**r.LIMITS,'backups':(0,0)})
  for p in [incomplete,pinned,recent]:self.assertTrue(p.exists())
 def test_legacy_backups_require_explicit_adoption(self):
  p=self.folder('.local/share/xplasma/maintenance/legacy-20260101-000001')
  self.backup(2,age=9000)
  limits={**r.LIMITS,'backups':(1,100000)}
  r.cleanup(self.home,True,limits=limits);self.assertTrue(p.exists())
  r.cleanup(self.home,True,True,limits=limits);self.assertFalse(p.exists())
 def test_symlink_root_and_nested_links_never_touch_external_data(self):
  external=self.home/'outside';external.mkdir();(external/'keep').write_text('KEEP')
  root=self.home/'.cache/xplasma/dark-mode';root.parent.mkdir(parents=True);root.symlink_to(external,target_is_directory=True)
  r.cleanup(self.home,True);self.assertEqual((external/'keep').read_text(),'KEEP')
  root.unlink();p=self.backup(1);(p/'link').symlink_to(external,target_is_directory=True);os.utime(p,(self.now-10000,)*2);self.backup(2,age=9000)
  r.cleanup(self.home,True,limits={**r.LIMITS,'backups':(1,100000)})
  self.assertFalse(p.exists());self.assertEqual((external/'keep').read_text(),'KEEP')
 def test_referenced_installer_rollback_folder_is_protected(self):
  referenced=self.backup(1,age=10000);self.backup(2,age=9000)
  self.file('.local/share/kde-xp-suite/installation.json',json.dumps({'rollback':str(referenced)}).encode())
  r.cleanup(self.home,True,True,limits={**r.LIMITS,'backups':(0,0)})
  self.assertTrue(referenced.exists())
 def test_snapshot_markers_only_manage_maintenance_not_installer_backups(self):
  installer=self.folder('.local/share/kde-xp-suite')
  self.assertFalse(r.note_snapshot(self.home,installer,True));self.assertFalse((installer/r.MARKER).exists())
  backup=self.folder('.local/share/xplasma/maintenance/new-backup')
  self.assertTrue(r.note_snapshot(self.home,backup,False));self.assertFalse(json.loads((backup/r.MARKER).read_text())['complete'])
  self.assertTrue(r.note_snapshot(self.home,backup,True));self.assertTrue(json.loads((backup/r.MARKER).read_text())['complete'])
 def test_partial_snapshot_manifest_stays_protected_even_during_legacy_adoption(self):
  backup=self.folder('.local/share/xplasma/maintenance/broken-20260101-000001')
  (backup/'before.json').write_text('{}');(backup/'after.json').write_text('{')
  os.utime(backup,(self.now-10000,)*2)
  self.backup(2,age=9000)
  r.cleanup(self.home,True,True,limits={**r.LIMITS,'backups':(1,100000)})
  self.assertTrue(backup.exists())
 def test_cleanup_lock_skips_a_concurrent_run(self):
  import fcntl
  path=self.file('.cache/xplasma/retention.lock')
  with path.open('w') as lock:
   fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
   self.assertEqual(r.locked_cleanup(self.home,apply=True),{'busy':True})
 def test_second_cleanup_is_idempotent(self):
  for i in range(9):self.backup(i,age=10000-i)
  r.cleanup(self.home,True)
  self.assertEqual(r.cleanup(self.home,True)['backups']['removed'],[])
if __name__=='__main__':unittest.main()
