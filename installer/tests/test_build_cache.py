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
 def test_full_runtime_can_serve_a_style_only_install(self):
  plugins=self.build/'runtime/lib/xplasma/forks/6.6/plasma/applets';plugins.mkdir(parents=True)
  for index in range(4):(plugins/(str(index)+'.so')).write_bytes((self.build/'runtime/lib/xplasma/qt6/styles/libxplasma.so').read_bytes())
  folder=cache.store(self.suite,self.build,'taskbar',self.root/'cache',self.profile)
  self.assertEqual(cache.find(self.suite,'core',self.profile,self.root/'cache'),folder)
 def test_prebuilt_does_not_request_compiler_or_kwin_sdk(self):
  import dependencies
  selected={'desktop','apps','taskbar','caption_animation'}
  compiled=dependencies.packages('apt',selected)
  ready=dependencies.packages('apt',selected,native_ready=True,caption_ready=True)
  self.assertIn('build-essential',compiled);self.assertIn('kwin-dev',compiled)
  self.assertNotIn('build-essential',ready);self.assertNotIn('kwin-dev',ready)
  self.assertNotIn('qt6-base-dev',ready)
 def test_kwin_package_probe_does_not_execute_privileged_binary(self):
  import subprocess
  source=self.suite/'payload/caption-animation';source.mkdir(parents=True)
  for name in ['CMakeLists.txt','effect.cpp','caption.h','metadata.json','test-caption.cpp','demo.cpp']:(source/name).write_text('source')
  with patch.object(cache.shutil,'which',side_effect=lambda name:'/usr/bin/'+name if name in ['kwin_wayland','dpkg-query'] else None),patch.object(cache.subprocess,'run',return_value=subprocess.CompletedProcess([],0,'4:6.6.4-0ubuntu1','')) as run:
   result=cache.caption_identity(self.suite)
   self.assertEqual(result['kwin'],'6.6.4');self.assertEqual(result['package'],'4:6.6.4-0ubuntu1')
   self.assertEqual(run.call_count,1);self.assertEqual(run.call_args.args[0][0],'/usr/bin/dpkg-query')


class ComponentCacheTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name)
  self.profile={'arch':'amd64','qt':(6,10,1),'plasma':(6,6,3)}
  self.project=self.root/'project'
  for name in ['CMakeLists.txt','libshared/common.cpp','applets/notifications/Button.qml','applets/systemtray/main.qml','containments/panel/main.qml']:
   p=self.project/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(name)
  self.binary=self.root/'plugin.so';elf=bytearray(64);elf[:6]=b'\x7fELF\x02\x01';elf[18:20]=(62).to_bytes(2,'little');self.binary.write_bytes(elf)
 def tearDown(self):self.temp.cleanup()
 def identity(self,widget='applets/notifications'):return cache.widget_identity(self.project,widget,self.profile)
 def test_only_changed_widget_is_invalidated(self):
  before={w:self.identity(w) for w in ['applets/notifications','applets/systemtray','containments/panel']}
  (self.project/'applets/notifications/Button.qml').write_text('contrast fix')
  self.assertNotEqual(before['applets/notifications'],self.identity())
  for w in ['applets/systemtray','containments/panel']:self.assertEqual(before[w],self.identity(w))
 def test_common_code_changes_invalidate_each_dependent_widget(self):
  before=self.identity();(self.project/'libshared/common.cpp').write_text('new ABI')
  self.assertNotEqual(before,self.identity())
 def test_component_hashes_and_architecture_are_verified(self):
  expected=self.identity();folder=cache.store_component(self.binary,'lib/test.so',expected,self.root/'cache')
  self.assertEqual(cache.find_component(self.root,expected,self.root/'cache',False),folder)
  self.assertFalse(cache.validate_component(folder,{**expected,'arch':'arm64'}))
  (folder/'runtime/lib/test.so').write_bytes(b'changed')
  self.assertIsNone(cache.find_component(self.root,expected,self.root/'cache',False))
 def test_force_source_bypasses_component_cache(self):
  expected=self.identity();cache.store_component(self.binary,'lib/test.so',expected,self.root/'cache')
  with patch.dict(os.environ,{'XP_BUILD_FROM_SOURCE':'1'}):self.assertIsNone(cache.find_component(self.root,expected,self.root/'cache',False))
 def test_missing_libraries_reject_a_matching_component(self):
  expected=self.identity();cache.store_component(self.binary,'lib/test.so',expected,self.root/'cache')
  with patch.object(cache,'usable',return_value=False):self.assertIsNone(cache.find_component(self.root,expected,self.root/'cache'))
 def test_caption_cache_does_not_depend_on_engine_source(self):
  abi={'kwin':'6.6.4','package':'6.6.4-1','source':{'effect.cpp':'hash'}}
  with patch.object(cache,'caption_identity',return_value=abi):first=cache.caption_component_identity(self.root,self.profile)
  (self.root/'unrelated-engine.cpp').write_text('changed')
  with patch.object(cache,'caption_identity',return_value=abi):self.assertEqual(first,cache.caption_component_identity(self.root,self.profile))
  with patch.object(cache,'caption_identity',return_value={**abi,'package':'6.6.4-2'}):self.assertNotEqual(first,cache.caption_component_identity(self.root,self.profile))

 def test_effect_lookup_survives_engine_update_but_rejects_new_kwin(self):
  abi={'kwin':'6.6.4','package':'6.6.4-1','source':{'effect.cpp':'hash'}}
  with patch.object(cache,'caption_identity',return_value=abi),patch.object(cache,'detect',return_value=self.profile),patch.object(cache,'usable',return_value=True):
   expected=cache.caption_component_identity(self.root)
   folder=cache.store_component(self.binary,'lib/xplasma/caption/xp-caption-animation.so',expected,self.root/'cache')
   (self.root/'source').mkdir();(self.root/'source/xplasma-source.tar.gz').write_bytes(b'new engine')
   self.assertEqual(cache.find_caption(self.root,cache=self.root/'cache'),folder/'runtime/lib/xplasma/caption/xp-caption-animation.so')
   with patch.object(cache,'caption_identity',return_value={**abi,'package':'6.6.4-2'}):
    self.assertIsNone(cache.find_caption(self.root,cache=self.root/'cache'))

if __name__=='__main__':unittest.main()
