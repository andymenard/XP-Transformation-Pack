import importlib.util
import struct
import unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('dark_builder',Path(__file__).resolve().parents[1]/'build.py')
b=importlib.util.module_from_spec(spec);spec.loader.exec_module(b)
class BuilderTests(unittest.TestCase):
 def test_neutral_surfaces_darken_and_colored_accents_stay(self):
  self.assertLess(max(b.neutral(255,255,255)),128)
  self.assertEqual(b.neutral(255,0,255),(255,0,255))
  self.assertEqual(b.neutral(70,120,210),(70,120,210))
 def test_bitmap_dimensions_and_alpha_are_preserved(self):
  header=struct.pack('<IiiHHIIiiII',40,2,1,1,32,0,8,0,0,0,0)
  data=header+bytes([240,240,240,128,255,0,255,0])
  result=b.bitmap(data,'BUTTON')
  self.assertEqual(result[:40],header)
  self.assertEqual(len(result),len(data))
  self.assertEqual(result[43],128)
  self.assertEqual(result[44:],data[44:])
  self.assertLess(max(result[40:43]),128)
 def test_neutral_curve_keeps_gradient_order_without_threshold_seam(self):
  values=[b.neutral(v,v,v)[1] for v in range(256)]
  self.assertEqual(values,sorted(values))
  self.assertLessEqual(max(z-a for a,z in zip(values,values[1:])),1)
 def test_xp_color_keys_survive(self):
  for key in [(0,0,0),(255,0,0),(255,0,255),(242,241,231)]:
   self.assertEqual(b.neutral(*key),key)
 def test_white_glyph_and_opaque_zero_alpha_dib_survive(self):
  header=struct.pack('<IiiHHIIiiII',40,1,1,1,32,0,4,0,0,0,0)
  self.assertEqual(b.bitmap(header+bytes([255,255,255,255]),'CLOSEGLYPH')[40:],bytes([255,255,255,255]))
  self.assertLess(max(b.bitmap(header+bytes([240,240,240,0]),'BUTTON')[40:43]),128)
class ControlArtworkTests(unittest.TestCase):
 def indexed(self,colors):
  header=struct.pack('<IiiHHIIiiII',40,1,1,1,8,0,4,0,0,len(colors),0)
  return header+b''.join(bytes((blue,green,red,0)) for red,green,blue in colors)+bytes(4)
 def test_indexed_arrow_gets_readable_ink_and_keeps_mask(self):
  data=self.indexed([(255,0,255),(0,0,0),(45,80,127)])
  result=b.bitmap(data,'BLUE_COMBOBUTTONGLYPH_BMP')
  self.assertEqual(result[40:44],data[40:44])
  self.assertGreater(min(result[44:47]),200)
  self.assertGreater(min(result[48:51]),200)
 def test_colored_controls_darken_but_keep_the_source_theme_tint(self):
  blue=b.artwork_color(200,214,251,surface=True)
  olive=b.artwork_color(210,214,175,surface=True)
  self.assertLess(max(blue),128);self.assertNotEqual(blue,olive)
  self.assertEqual(b.artwork_color(200,214,251),(200,214,251))
 def test_nonstandard_transparency_color_is_not_recolored(self):
  mask=(200,210,220);data=self.indexed([mask,(200,214,251)])
  result=b.bitmap(data,'COMBOBUTTON',surface=True,keys={mask})
  self.assertEqual(result[40:44],data[40:44]);self.assertNotEqual(result[44:47],data[44:47])
 def test_binary_alpha_dib_is_recolored_as_opaque_like_renderer(self):
  header=struct.pack('<IiiHHIIiiII',40,2,1,1,32,0,8,0,0,0,0)
  result=b.bitmap(header+bytes([240,240,240,0,240,240,240,255]),'BUTTON')
  self.assertEqual(result[40:43],result[44:47]);self.assertLess(max(result[40:43]),128)
 def test_embedded_builder_and_tested_tool_are_identical(self):
  native=Path(__file__).resolve().parents[3]/'native/src/app/data/dark-mode/build.py'
  self.assertEqual(native.read_bytes(),Path(b.__file__).read_bytes())
if __name__=='__main__':unittest.main()
