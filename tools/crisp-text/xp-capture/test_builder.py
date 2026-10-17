# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util,struct,tempfile,unittest
from pathlib import Path
from fontTools.ttLib import TTFont
spec=importlib.util.spec_from_file_location('xp_pixel_builder',Path(__file__).with_name('build.py'));builder=importlib.util.module_from_spec(spec);spec.loader.exec_module(builder)
class CaptureTests(unittest.TestCase):
 def capture(self,major=5,minor=1,complete=True):
  header=b'XPGDI001'+struct.pack('<8I',1,major,minor,2600,11,400,11,3)
  # A three-row glyph with a hole, DWORD-aligned and top-down like GDI.
  bits=b'\xe0\0\0\0\xa0\0\0\0\xe0\0\0\0'
  record=struct.pack('<9i',65,1,3,3,1,3,5,0,len(bits))+bits
  return header+record+(b'\xff'*4 if complete else b'')
 def test_only_complete_xp_captures_are_accepted(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/'capture.xpg'
   for blob in [self.capture(major=6),self.capture(complete=False),self.capture()[:-8]]:
    p.write_bytes(blob)
    with self.assertRaises(ValueError):builder.read_capture(p)
 def test_outlines_reproduce_captured_pixels_and_advance(self):
  import freetype
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/'capture.xpg';p.write_bytes(self.capture());out=Path(tmp)/'font.ttf'
   report=builder.build_font(p,out);self.assertEqual(report['renderer'],'Windows XP GDI GetGlyphOutlineW GGO_BITMAP')
   f=TTFont(out);self.assertEqual(f['cmap'].getBestCmap()[65],'g00001');self.assertEqual(f['hmtx']['g00001'],(500,100))
   face=freetype.Face(str(out));face.set_pixel_sizes(0,11);face.load_char('A',freetype.FT_LOAD_RENDER|freetype.FT_LOAD_TARGET_MONO|freetype.FT_LOAD_MONOCHROME)
   glyph=face.glyph;self.assertEqual((glyph.bitmap_left,glyph.bitmap_top,glyph.advance.x),(1,3,320))
   bitmap=glyph.bitmap;actual=[[bool(bitmap.buffer[y*bitmap.pitch+x//8]&(128>>(x%8))) for x in range(bitmap.width)] for y in range(bitmap.rows)]
   self.assertEqual(actual,[[1,1,1],[1,0,1],[1,1,1]])
 def test_xp_empty_space_box_and_conservative_buffer_are_supported(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/'capture.xpg';data=self.capture();header=data[:40]
   space=struct.pack('<9i',32,3,1,1,0,0,3,0,0)
   padded=struct.pack('<9i',65,1,3,3,1,3,5,0,16)+data[76:88]+b'junk'
   p.write_bytes(header+space+padded+b'\xff'*4)
   meta,cmap,glyphs=builder.read_capture(p)
   self.assertEqual(glyphs[3][-1],b'');self.assertEqual(glyphs[1][-1],data[76:88])
   builder.build_font(p,Path(tmp)/'font.ttf')
 def test_blank_edge_columns_and_diagonal_pixels_match_xp(self):
  import importlib.util
  spec=importlib.util.spec_from_file_location('xp_pixel_verify',Path(__file__).with_name('verify.py'))
  verifier=importlib.util.module_from_spec(spec)
  import sys
  sys.modules['build']=builder;spec.loader.exec_module(verifier)
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/'capture.xpg';out=Path(tmp)/'font.ttf'
   bitmap=b'\x54\0\0\0\x28\0\0\0\x54\0\0\0'
   record=struct.pack('<9i',65,1,7,3,-1,3,6,0,len(bitmap))+bitmap
   p.write_bytes(self.capture()[:40]+record+b'\xff'*4)
   builder.build_font(p,out)
   self.assertEqual(verifier.verify(p,out)['matching_glyphs'],1)
 def test_fractional_sizes_keep_hard_pixel_edges_and_ink(self):
  import freetype
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp)/'capture.xpg';p.write_bytes(self.capture());out=Path(tmp)/'font.ttf';builder.build_font(p,out)
   face=freetype.Face(str(out))
   for dpr in [1,1.25,1.5,1.75,2,2.25,2.5,3]:
    face.set_pixel_sizes(0,round(11*dpr));face.load_char('A',freetype.FT_LOAD_RENDER|freetype.FT_LOAD_TARGET_MONO|freetype.FT_LOAD_MONOCHROME)
    self.assertEqual(face.glyph.bitmap.pixel_mode,freetype.FT_PIXEL_MODE_MONO);self.assertTrue(any(face.glyph.bitmap.buffer))
if __name__=='__main__':unittest.main()
