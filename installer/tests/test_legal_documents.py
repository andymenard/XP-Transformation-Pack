"""Release credits must match the documents embedded in Properties."""
import importlib.util, tempfile, unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
class LegalDocumentsTests(unittest.TestCase):
 def test_packaged_copies_are_current(self):
  spec=importlib.util.spec_from_file_location('sync_legal',ROOT/'tools/sync-legal.py')
  module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  module.sync(check=True)
 def test_export_preserves_finished_credits_and_license_files(self):
  spec=importlib.util.spec_from_file_location('alpha_prepare',ROOT/'public-alpha/prepare.py')
  module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  with tempfile.TemporaryDirectory() as folder:
   target=Path(folder)/'source';module.source(target)
   for path in ['CREDITS.md','installer/CREDITS.md','native/CREDITS.md']:
    self.assertEqual((target/path).read_bytes(),(ROOT/'CREDITS.md').read_bytes())
   for license in (ROOT/'native/LICENSES').glob('*.txt'):
    self.assertEqual((target/'installer/LICENSES'/license.name).read_bytes(),license.read_bytes())
if __name__=='__main__':unittest.main()
