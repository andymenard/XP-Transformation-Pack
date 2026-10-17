#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare a completed square-pixel font with every XP-captured glyph.

FreeType is used only to verify the finished outlines, never to produce the
source pixels. Comparison uses baseline-relative coordinates, so trimmed blank
rows/columns do not create false differences.
"""
import argparse,json
from pathlib import Path
import freetype
from build import read_capture

def ink(bitmap,width,height,stride,left,top):
 if not bitmap:return set()
 return {(left+x,top-y) for y in range(height) for x in range(width) if bitmap[y*stride+x//8]&(128>>(x%8))}

def verify(capture,font,design_size=None):
 meta,cmap,records=read_capture(capture)
 if design_size:
  from optimize import optimize_record
  records={i:optimize_record(r,design_size,meta['ppem'])[0] for i,r in records.items()};meta={**meta,'ppem':design_size}
 face=freetype.Face(str(font));face.set_pixel_sizes(0,meta['ppem']);seen=set();failures=[]
 for code,index in cmap.items():
  if index in seen:continue
  seen.add(index);width,height,left,top,advance,bitmap=records[index]
  expected=ink(bitmap,width,height,((width+31)//32)*4,left,top)
  face.load_char(chr(code),freetype.FT_LOAD_RENDER|freetype.FT_LOAD_TARGET_MONO|freetype.FT_LOAD_MONOCHROME)
  glyph=face.glyph;b=glyph.bitmap;actual=ink(b.buffer,b.width,b.rows,b.pitch,glyph.bitmap_left,glyph.bitmap_top)
  if actual!=expected or glyph.advance.x!=advance*64:failures.append({'codepoint':code,'glyph':index,'different_pixels':len(actual^expected),'advance_match':glyph.advance.x==advance*64})
 result={'checked_glyphs':len(seen),'matching_glyphs':len(seen)-len(failures),'failures':failures}
 if failures:raise ValueError(json.dumps(result,indent=2))
 return result

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('capture',type=Path);p.add_argument('font',type=Path);a=p.parse_args();print(json.dumps(verify(a.capture,a.font),indent=2))
