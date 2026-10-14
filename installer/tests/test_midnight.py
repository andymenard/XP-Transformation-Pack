import tarfile
import configparser
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

class MidnightTests(unittest.TestCase):
 def setUp(self):
  source=ROOT.parent/'tools/dark-theme/palette.theme'
  self.c=configparser.ConfigParser()
  if source.exists():self.c.read(source)
  else:
   with tarfile.open(ROOT/'source/xplasma-source.tar.gz') as archive:self.c.read_string(archive.extractfile('xplasma/src/app/data/dark-mode/palette.theme').read().decode())
 def test_complete_palette_and_noir_base(self):
  self.assertEqual(self.c['Theme']['DisplayName'],'Zune Dark')
  self.assertEqual(len(self.c['Control Panel\\Colors']),31)
  self.assertEqual(self.c['XPlasma']['BaseStyle'],'Zune')
 def test_text_and_selected_text_contrast(self):
  colors=self.c['Control Panel\\Colors']
  def luminance(rgb):
   values=[int(x)/255 for x in rgb.split()]
   values=[v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in values]
   return sum(v*w for v,w in zip(values,[.2126,.7152,.0722]))
  for fg,bg in [('WindowText','Window'),('ButtonText','ButtonFace'),('MenuText','Menu'),('HilightText','Hilight'),('TitleText','ActiveTitle'),('TitleText','GradientActiveTitle'),('InfoText','InfoWindow'),('HotTrackingColor','Window')]:
   a,b=sorted([luminance(colors[fg]),luminance(colors[bg])]);self.assertGreaterEqual((b+.05)/(a+.05),4.5,fg)
 def test_bevel_edges_are_ordered(self):
  colors=self.c['Control Panel\\Colors']
  level=lambda key:sum(map(int,colors[key].split()))
  self.assertGreater(level('ButtonHilight'),level('ButtonLight'))
  self.assertGreater(level('ButtonLight'),level('ButtonFace'))
  self.assertGreater(level('ButtonFace'),level('ButtonShadow'))
  self.assertGreater(level('ButtonShadow'),level('ButtonDkShadow'))

if __name__=='__main__':unittest.main()
