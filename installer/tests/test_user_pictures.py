import ast,hashlib,json,os,shutil,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class UserPictures(unittest.TestCase):
 def test_installs_into_kde_avatar_catalogue_without_changing_account(self):
  tree=ast.parse((ROOT/'backend.py').read_text());functions=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in {'copy','user_pictures'}]
  with tempfile.TemporaryDirectory() as directory:
   base=Path(directory);payload=base/'payload/assets/user-pictures';payload.mkdir(parents=True);(payload/'xp-cat.png').write_bytes(b'portrait')
   home=base/'home';home.mkdir();(home/'.face').write_bytes(b'current face')
   ns={'Path':Path,'BASE':base,'shutil':shutil,'tempfile':tempfile,'os':os,'log':lambda _:None}
   exec(compile(ast.Module(body=functions,type_ignores=[]),'backend.py','exec'),ns);ns['user_pictures'](home)
   self.assertEqual((home/'.local/share/plasma/avatars/windows-xp/xp-cat.png').read_bytes(),b'portrait')
   self.assertEqual((home/'.face').read_bytes(),b'current face')
 def test_original_iso_pictures_are_complete_and_verified(self):
  folder=ROOT/'payload/assets/user-pictures'
  if not folder.exists():self.skipTest('Source checkout without original asset payload')
  records=json.loads((folder/'PROVENANCE.json').read_text())['pictures'];self.assertEqual(len(records),24)
  for record in records:self.assertEqual(hashlib.sha256((folder/record['file']).read_bytes()).hexdigest(),record['sha256'])
