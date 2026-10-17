#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build scalable square-pixel Tahoma from captures made by Windows XP GDI.

This never rasterizes the original Tahoma font on Linux. Input must be a complete
XPGDI001 capture made on Windows XP (5.1); FreeType is not a build dependency.
"""
import argparse,hashlib,json,struct
from pathlib import Path
from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen


def read_capture(path):
 data=Path(path).read_bytes()
 if len(data)<44 or data[:8]!=b'XPGDI001':raise ValueError('Not a complete XP GDI capture')
 version,major,minor,build,ppem,weight,ascent,descent=struct.unpack_from('<8I',data,8)
 if version!=1 or (major,minor)!=(5,1):raise ValueError('Capture must come from Windows XP 5.1')
 if ppem!=11 or weight not in (400,700):raise ValueError('Expected original 11-pixel Tahoma regular or bold')
 offset=40;records={};glyphs={}
 while offset+4<=len(data):
  code=struct.unpack_from('<I',data,offset)[0]
  if code==0xffffffff:
   if offset+4!=len(data):raise ValueError('Unexpected data after capture terminator')
   if not records:raise ValueError('Capture has no glyphs')
   return dict(build=build,ppem=ppem,weight=weight,ascent=ascent,descent=descent),records,glyphs
  if offset+36>len(data):raise ValueError('Truncated glyph record')
  code,index,width,height,left,top,advance,advance_y,size=struct.unpack_from('<9i',data,offset);offset+=36
  if not (0<=code<=0xffff and 0<=index<=0xffff and 0<=width<=256 and 0<=height<=256 and 0<=advance<=512):raise ValueError('Invalid glyph metrics')
  stride=((width+31)//32)*4
  # XP can report a conservative buffer size before rasterizing, and an
  # empty space can retain a 1x1 metrics box with a zero-byte bitmap.
  needed=stride*height
  if size and (size<needed or size>needed+16*max(1,stride)):raise ValueError('Invalid DWORD-aligned bitmap size')
  if offset+size>len(data):raise ValueError('Truncated glyph bitmap')
  bitmap=data[offset:offset+size][:needed];offset+=size
  glyph=(width,height,left,top,advance,bitmap)
  if index in glyphs and glyphs[index]!=glyph:raise ValueError('Inconsistent glyph index')
  glyphs[index]=glyph;records[code]=index
 raise ValueError('Incomplete capture: completion marker missing')


def pixel_glyph(record):
 width,height,left,top,advance,bitmap=record;stride=((width+31)//32)*4;pen=TTGlyphPen(None)
 # Each horizontal run becomes a rectangle on the original XP pixel grid.
 # Touching rows share exact coordinates; no resampling or smoothing is baked in.
 for y in range(height if bitmap else 0):
  x=0
  while x<width:
   if not bitmap[y*stride+x//8]&(128>>(x%8)):x+=1;continue
   start=x;x+=1
   while x<width and bitmap[y*stride+x//8]&(128>>(x%8)):x+=1
   x0=(left+start)*100;x1=(left+x)*100;y1=(top-y)*100;y0=y1-100
   pen.moveTo((x0,y0));pen.lineTo((x0,y1));pen.lineTo((x1,y1));pen.lineTo((x1,y0));pen.closePath()
 glyph=pen.glyph();glyph.recalcBounds(None)
 # GDI's box can contain blank edge columns. TrueType's side bearing must
 # describe the actual outline bound, or FreeType shifts those glyphs.
 bearing=glyph.xMin if glyph.numberOfContours else 0
 return glyph,(advance*100,bearing)


def build_font(capture,destination,design=None):
 meta,cmap,records=read_capture(capture)
 source_ppem=meta['ppem'];family='XPlasma Tahoma Pixel'
 if design:
  target,records,family=design;scale=target/source_ppem
  meta={**meta,'ppem':target,'ascent':round(meta['ascent']*scale),'descent':round(meta['descent']*scale)}
 indices=sorted(records)
 names={i:('.notdef' if i==0 else f'g{i:05d}') for i in indices}
 order=['.notdef']+[names[i] for i in indices if i!=0]
 glyphs={};metrics={}
 for i,record in records.items():glyphs[names[i]],metrics[names[i]]=pixel_glyph(record)
 if '.notdef' not in glyphs:glyphs['.notdef']=TTGlyphPen(None).glyph();metrics['.notdef']=(600,0)
 bold=meta['weight']==700;style='Bold' if bold else 'Regular'
 fb=FontBuilder(meta['ppem']*100,isTTF=True);fb.setupGlyphOrder(order)
 fb.setupCharacterMap({code:names[index] for code,index in cmap.items()})
 fb.setupGlyf(glyphs);fb.setupHorizontalMetrics(metrics)
 fb.setupHorizontalHeader(ascent=meta['ascent']*100,descent=-meta['descent']*100,lineGap=0)
 top=max(meta['ascent'],max(r[3] for r in records.values()));bottom=max(meta['descent'],max(r[1]-r[3] for r in records.values()))
 fb.setupNameTable({'familyName':family,'styleName':style,'uniqueFontIdentifier':family+' '+style+' XP-GDI-v1','fullName':family+' '+style,'psName':family.replace(' ','')+'-'+style,'version':'Version 1.000','copyright':'Glyph shapes derived from Microsoft Tahoma using Windows XP GDI. Original Tahoma copyright Microsoft Corporation.','description':f'XP GDI {source_ppem}-pixel capture; '+('stem-balanced optical pixel design.' if design else 'faithful square-pixel outlines.')})
 fb.setupOS2(version=4,fsType=8,sTypoAscender=meta['ascent']*100,sTypoDescender=-meta['descent']*100,sTypoLineGap=0,usWinAscent=top*100,usWinDescent=bottom*100,usWeightClass=meta['weight'],fsSelection=(32 if bold else 64)|128)
 fb.setupPost();fb.setupMaxp();fb.font['head'].macStyle=1 if bold else 0
 # Request grid fitting without grayscale at every rendered size.
 from fontTools.ttLib import newTable
 gasp=newTable('gasp');gasp.gaspRange={65535:1};fb.font['gasp']=gasp
 # The pixel shapes are already complete. A generic outline rasterizer must
 # not add dropout-control bridge pixels between diagonal/isolated blocks.
 from fontTools.ttLib.tables.ttProgram import Program
 prep=newTable('prep');prep.program=Program()
 prep.program.fromAssembly(['PUSHB[ ]','2','SCANTYPE[ ]']);fb.font['prep']=prep
 fb.font['maxp'].maxStackElements=2
 out=Path(destination);out.parent.mkdir(parents=True,exist_ok=True);fb.save(out)
 report={**meta,'source_ppem':source_ppem,'family':family,'style':style,'characters':len(cmap),'glyphs':len(records),'capture_sha256':hashlib.sha256(Path(capture).read_bytes()).hexdigest(),'renderer':'Windows XP GDI GetGlyphOutlineW GGO_BITMAP','output_sha256':hashlib.sha256(out.read_bytes()).hexdigest()}
 out.with_suffix('.json').write_text(json.dumps(report,indent=2)+'\n');return report

if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('capture',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args();print(json.dumps(build_font(args.capture,args.output),indent=2))
