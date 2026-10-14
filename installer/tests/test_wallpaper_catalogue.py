import importlib.util,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class WallpaperCatalogue(unittest.TestCase):
 def test_original_is_retained_instead_of_larger_crop(self):
  spec=importlib.util.spec_from_file_location('xp_wallpaper_sync',ROOT/'sync-payload.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  with tempfile.TemporaryDirectory() as folder:
   source=Path(folder)/'source';destination=Path(folder)/'result';source.mkdir();destination.mkdir();(source/'Plus!').mkdir()
   for name in ['Bliss.jpg','Bliss (3840x2160).jpg','Bliss (1200x1920).jpg','Plus!/Plus! Space.jpg','Plus!/Plus! Space (1200x1920).jpg','Moon flower (Unedited Version).jpg']:(source/name).write_bytes(name.encode())
   module.collect_wallpapers(source,destination)
   self.assertEqual((destination/'Bliss.jpg').read_bytes(),b'Bliss.jpg')
   self.assertFalse(any('x2160' in p.name or 'x1920' in p.name for p in destination.iterdir()))
   self.assertTrue((destination/'Plus! — Plus! Space.jpg').is_file())
   self.assertTrue((destination/'Moon flower.jpg').is_file())
