import io,tempfile,unittest
from unittest.mock import patch
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import core
class PublicUpdateTests(unittest.TestCase):
 def test_semver_prereleases_order_before_stable(self):
  self.assertLess(core.version('1.0.0-alpha.1'),core.version('1.0.0-alpha.2'))
  self.assertLess(core.version('1.0.0-alpha.9'),core.version('1.0.0-beta.1'))
  self.assertLess(core.version('1.0.0-rc.1'),core.version('1.0.0'))
 def test_stable_excludes_alpha_and_alpha_selects_signed_prerelease(self):
  assets=[{'name':n} for n in ['release.json','release.sig','payload.tar.gz']]
  releases=[{'tag_name':'v0.3.1','assets':assets},{'tag_name':'v1.0.0-alpha.1','prerelease':True,'assets':assets}]
  with patch.object(core,'github_json',return_value=releases):
   self.assertEqual(core.latest('owner/repo')['tag_name'],'v0.3.1')
   self.assertEqual(core.latest('owner/repo','alpha')['tag_name'],'v1.0.0-alpha.1')
 def test_public_metadata_does_not_call_gh(self):
  with patch.object(core,'access_mode',return_value='public'),patch.object(core,'public_read',return_value=b'[]') as read,patch.object(core.subprocess,'run',side_effect=AssertionError('Unexpected authentication')):
   self.assertEqual(core.github_json('repos/owner/repo/releases'),[])
   self.assertTrue(read.call_args.args[0].startswith('https://api.github.com/'))
 def test_bounded_public_read_and_url_validation(self):
  class Response(io.BytesIO):
   def __enter__(self):return self
   def __exit__(self,*args):self.close()
  with patch.object(core.urllib.request,'urlopen',return_value=Response(b'12345')):
   with self.assertRaises(core.UpdateError):core.public_read('https://api.github.com/test',4)
  with self.assertRaises(core.UpdateError):core.public_read('http://localhost/private',10)
 def test_public_download_only_accepts_selected_repo_assets(self):
  assets=[{'name':n,'size':1,'browser_download_url':'https://github.com/other/repo/releases/download/v1/'+n} for n in ['release.json','release.sig','payload.tar.gz']]
  with tempfile.TemporaryDirectory() as path,patch.object(core,'access_mode',return_value='public'):
   with self.assertRaises(core.UpdateError):core.download('owner/repo',{'assets':assets},path)

 def test_bootstrap_preserves_feed_unless_explicitly_switching(self):
  import json,install
  with tempfile.TemporaryDirectory() as directory:
   home=Path(directory)/'home';source=Path(directory)/'source';source.mkdir()
   for name in ['core.py','gui.py','release-key.pem','README.md']:(source/name).write_text('test')
   public={'repository':'andymenard/XPlasma-Plus','channel':'alpha','access':'public'}
   (source/'config.json').write_text(json.dumps(public))
   existing=home/'.local/share/xplasma-updater/config.json';existing.parent.mkdir(parents=True)
   private={'repository':'andymenard/XPlasma','channel':'stable','access':'private'}
   existing.write_text(json.dumps(private))
   install.register(home,source,require_existing=False)
   self.assertEqual(json.loads(existing.read_text()),private)
   install.register(home,source,require_existing=False,switch_feed=True)
   self.assertEqual(json.loads(existing.read_text()),public)
