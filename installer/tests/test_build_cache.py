import importlib.util,io,os,sys,tarfile,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
BASE=Path(__file__).resolve().parents[1];sys.path.insert(0,str(BASE))
spec=importlib.util.spec_from_file_location('xp_build_cache',BASE/'build_cache.py');cache=importlib.util.module_from_spec(spec);spec.loader.exec_module(cache)

class BuildCacheTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);self.suite=self.root/'suite';(self.suite/'source').mkdir(parents=True)
  self.archive(b'first',1)
  self.profile={'arch':'amd64','qt':(6,10,1),'plasma':(6,6,3)}
  self.build=self.root/'build';runtime=self.build/'runtime'
  elf=bytearray(64);elf[:6]=b'\x7fELF\x02\x01';elf[18:20]=(62).to_bytes(2,'little')
  for name in ['bin/xplasma','bin/xplasma-edit','bin/xplasma-setup','lib/xplasma/qt6/styles/libxplasma.so','lib/xplasma/start-menu/libxpmenuguard.so']:
   p=runtime/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(elf);p.chmod(0o755)
 def archive(self,data,mtime):
  with tarfile.open(self.suite/'source/xplasma-source.tar.gz','w:gz') as archive:
   item=tarfile.TarInfo('xplasma/src/main.cpp');item.size=len(data);item.mtime=mtime;archive.addfile(item,io.BytesIO(data))
 def tearDown(self):self.temp.cleanup()
 def test_archive_timestamps_do_not_force_rebuild(self):
  first=cache.source_id(self.suite);self.archive(b'first',500);self.assertEqual(first,cache.source_id(self.suite))
  self.archive(b'changed',500);self.assertNotEqual(first,cache.source_id(self.suite))
 def test_valid_package_rejects_tampering_and_other_architecture_or_qt(self):
  expected=cache.identity(self.suite,'core',self.profile)
  folder=cache.store(self.suite,self.build,'core',self.root/'cache',self.profile)
  self.assertTrue(cache.validate(folder,expected))
  for profile in [{**self.profile,'arch':'arm64'},{**self.profile,'qt':(6,11,1)}]:
   self.assertFalse(cache.validate(folder,cache.identity(self.suite,'core',profile)))
  (folder/'runtime/bin/xplasma').write_bytes(b'tampered');self.assertFalse(cache.validate(folder,expected))
 def test_other_plasma_series_archives_do_not_invalidate_matching_build(self):
  (self.suite/'source/plasma-workspace-6.6.3.tar.xz').write_bytes(b'matching')
  before=cache.identity(self.suite,'taskbar',self.profile)
  (self.suite/'source/plasma-workspace-6.7.0.tar.xz').write_bytes(b'other')
  self.assertEqual(before,cache.identity(self.suite,'taskbar',self.profile))
 def test_external_links_are_not_packaged(self):
  (self.build/'runtime/escape').symlink_to('/etc/passwd')
  with self.assertRaises(RuntimeError):cache.store(self.suite,self.build,'core',self.root/'cache',self.profile)
 def test_prebuilt_does_not_request_compiler_or_kwin_sdk(self):
  import dependencies
  selected={'desktop','apps','taskbar','caption_animation'}
  compiled=dependencies.packages('apt',selected)
  ready=dependencies.packages('apt',selected,native_ready=True,caption_ready=True)
  self.assertIn('build-essential',compiled);self.assertIn('kwin-dev',compiled)
  self.assertNotIn('build-essential',ready);self.assertNotIn('kwin-dev',ready)
  self.assertNotIn('qt6-base-dev',ready)

if __name__=='__main__':unittest.main()
