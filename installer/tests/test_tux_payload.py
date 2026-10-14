import io,tarfile,unittest
from pathlib import Path
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
class TuxPayloadTests(unittest.TestCase):
 @unittest.skipUnless((ROOT/'source/xplasma-source.tar.gz').exists(),'Requires complete installer source payload')
 def test_tux_option_and_art_are_in_native_build_input(self):
  with tarfile.open(ROOT/'source/xplasma-source.tar.gz') as archive:
   prefix='xplasma/src/app/'
   editor=archive.extractfile(prefix+'editor/displayproperties.cpp').read().decode()
   self.assertIn('&Tux Start logo',editor)
   self.assertIn('installerFeatures',editor)
   self.assertIn('PREFIX /logos',archive.extractfile(prefix+'CMakeLists.txt').read().decode())
   image=Image.open(archive.extractfile(prefix+'editor/logos/tux.png')).convert('RGBA')
   self.assertIsNotNone(image.getchannel('A').getbbox())
 @unittest.skipUnless((ROOT/'source/xplasma-source.tar.gz').exists(),'Requires complete installer source payload')
 def test_kde_option_and_original_vector_are_bundled(self):
  with tarfile.open(ROOT/'source/xplasma-source.tar.gz') as archive:
   prefix='xplasma/src/app/'
   editor=archive.extractfile(prefix+'editor/displayproperties.cpp').read().decode()
   self.assertIn('&KDE Start logo',editor)
   self.assertIn('kdeBlocker(m_kdeLogo)',editor)
   image=Image.open(archive.extractfile(prefix+'editor/logos/kde-crystal.png')).convert('RGBA')
   self.assertEqual(image.size,(256,256))
   self.assertEqual(image.getpixel((0,0))[3],0)
   self.assertIsNotNone(image.getchannel('A').getbbox())
   self.assertIn(b'<svg',archive.extractfile(prefix+'editor/logos/kde-classic.svg').read())
   self.assertIn(b'GNU LESSER GENERAL PUBLIC LICENSE',archive.extractfile(prefix+'editor/logos/COPYING-KDE-LOGO').read())
if __name__=='__main__':unittest.main()
