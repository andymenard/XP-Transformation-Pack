"""Prerelease feature selection and packaging must not reintroduce OTA."""
import importlib.util,json,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
def load(name,relative):
 spec=importlib.util.spec_from_file_location(name,ROOT/relative)
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module
class NoOtaTests(unittest.TestCase):
 def test_export_excludes_updater_feature(self):
  prepare=load('no_ota_prepare','public-alpha/prepare.py')
  with tempfile.TemporaryDirectory() as directory:
   path=Path(directory);prepare.configure(path)
   ids={f['id'] for f in json.loads((path/'features.json').read_text())}
   self.assertNotIn('updates',ids)
   self.assertIn('firefox',ids)
 def test_packager_rejects_updater_payload_even_when_unselected(self):
  package=load('no_ota_package','public-alpha/package.py')
  with tempfile.TemporaryDirectory() as directory:
   path=Path(directory);bundle=path/'bundle';bundle.mkdir()
   (bundle/'edition.json').write_text(json.dumps({'version':'1.0.0-alpha.7'}))
   (bundle/'features.json').write_text('[]')
   (bundle/'payload/updater').mkdir(parents=True)
   with self.assertRaisesRegex(ValueError,'OTA updater is excluded'):
    package.package(bundle,path/'out')
 def test_packager_rejects_updater_feature_without_payload(self):
  package=load('no_ota_package','public-alpha/package.py')
  with tempfile.TemporaryDirectory() as directory:
   path=Path(directory);bundle=path/'bundle';bundle.mkdir()
   (bundle/'edition.json').write_text(json.dumps({'version':'1.0.0-alpha.7'}))
   (bundle/'features.json').write_text(json.dumps([{'id':'updates','project':'shared'}]))
   with self.assertRaisesRegex(ValueError,'OTA updater is excluded'):
    package.package(bundle,path/'out')
if __name__=='__main__':unittest.main()
