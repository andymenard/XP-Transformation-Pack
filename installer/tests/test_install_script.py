"""install.sh stops with a clear message when the release folder is incomplete."""
import json,os,shutil,subprocess,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class InstallScript(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
  self.bundle=Path(self.temp.name)/'XPlasma-Plus';self.bundle.mkdir()
  shutil.copy2(ROOT/'install.sh',self.bundle/'install.sh')
  self.ran=Path(self.temp.name)/'ran';self.ran.mkdir()
  (self.bundle/'gui.py').write_text('import pathlib,sys\npathlib.Path(sys.argv[1]).joinpath("gui").touch()\n'.replace('sys.argv[1]',repr(str(self.ran))))
  # Stand-ins: the GUI binding check may ask pkexec, and the message goes to kdialog.
  self.bin=Path(self.temp.name)/'bin';self.bin.mkdir()
  for name in ['pkexec','kdialog']:
   stub=self.bin/name;stub.write_text('#!/bin/sh\nprintf "%s\\n" "$@" > "'+str(self.ran/name)+'"\n');stub.chmod(0o755)
 def release(self,names):
  for name in names:(self.bundle/name).write_text(name)
  (self.bundle/'BUILD-PROVENANCE.json').write_text('{}')
  manifest={n:'0'*64 for n in [*names,'gui.py','install.sh','BUILD-PROVENANCE.json']}
  (self.bundle/'SHA256SUMS.json').write_text(json.dumps(manifest))
 def run_script(self):
  env={**os.environ,'PATH':str(self.bin)+os.pathsep+os.environ.get('PATH','')}
  return subprocess.run(['sh',str(self.bundle/'install.sh')],env=env,text=True,capture_output=True,timeout=60)

 def test_a_complete_release_starts_the_installer(self):
  self.release(['system_profile.py','backend.py'])
  done=self.run_script()
  self.assertEqual(done.returncode,0,done.stderr);self.assertTrue((self.ran/'gui').exists())
  self.assertFalse((self.ran/'kdialog').exists())

 def test_missing_files_stop_it_with_a_message(self):
  self.release(['system_profile.py','backend.py']);(self.bundle/'system_profile.py').unlink()
  done=self.run_script()
  self.assertEqual(done.returncode,1);self.assertFalse((self.ran/'gui').exists())
  self.assertIn('incomplete: 1 of 5 files are missing, including system_profile.py',done.stderr)
  shown=(self.ran/'kdialog').read_text()
  self.assertIn('--error',shown);self.assertIn('extract the archive again',shown)

 def test_a_release_without_its_file_list_is_incomplete(self):
  self.release(['backend.py']);(self.bundle/'SHA256SUMS.json').unlink()
  done=self.run_script()
  self.assertEqual(done.returncode,1);self.assertIn('SHA256SUMS.json is missing',done.stderr)

 def test_an_unreadable_file_list_is_incomplete(self):
  self.release(['backend.py']);(self.bundle/'SHA256SUMS.json').write_text('{')
  done=self.run_script()
  self.assertEqual(done.returncode,1);self.assertIn('incomplete: its file list SHA256SUMS.json is damaged',done.stderr)

 def test_resource_links_count_as_present(self):
  self.release(['backend.py']);os.symlink('backend.py',self.bundle/'link')
  manifest=json.loads((self.bundle/'SHA256SUMS.json').read_text());manifest['link']={'kind':'link','target':'backend.py'}
  (self.bundle/'SHA256SUMS.json').write_text(json.dumps(manifest))
  self.assertEqual(self.run_script().returncode,0)

 def test_a_source_checkout_is_not_checked(self):
  done=self.run_script()
  self.assertEqual(done.returncode,0,done.stderr);self.assertTrue((self.ran/'gui').exists())

if __name__=='__main__':unittest.main()
