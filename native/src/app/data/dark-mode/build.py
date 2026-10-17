#!/usr/bin/env python3
"""Build Zune Dark with original Zune shell artwork and dark applications.

Preserves resource identifiers, sizes and alpha, replacing only neutral artwork
colors and theme color properties. No Windows executable code is run.
"""
import argparse,configparser,re,struct
from pathlib import Path

ROOT=Path(__file__).resolve().parent
SHELL_COLORS={'activetitle','inactivetitle','titletext','inactivetitletext','gradientactivetitle','gradientinactivetitle','activeborder','inactiveborder','background','windowframe'}
U16=lambda b,o:struct.unpack_from('<H',b,o)[0]
U32=lambda b,o:struct.unpack_from('<I',b,o)[0]

def resources(blob):
 pe=U32(blob,0x3c)
 if blob[pe:pe+4]!=b'PE\0\0':raise ValueError('Not a PE visual style')
 opt=pe+24;directory=opt+(112 if U16(blob,opt)==0x20b else 96)
 table=opt+U16(blob,pe+20);sections=[]
 for i in range(U16(blob,pe+6)):
  at=table+40*i;sections.append((U32(blob,at+12),U32(blob,at+20),max(U32(blob,at+8),U32(blob,at+16)),at))
 def offset(rva):
  for va,raw,size,_ in sections:
   if va<=rva<va+size:return raw+rva-va
  raise ValueError('Invalid resource address')
 root=offset(U32(blob,directory+16));result=[]
 def name(n):
  if not n&0x80000000:return str(n)
  at=root+(n&0x7fffffff);return bytes(blob[at+2:at+2+2*U16(blob,at)]).decode('utf-16le')
 def walk(at,keys,depth):
  if depth>3:raise ValueError('Invalid resource nesting')
  count=U16(blob,at+12)+U16(blob,at+14)
  for i in range(count):
   key,next_=struct.unpack_from('<II',blob,at+16+8*i)
   keys2=keys+[name(key)];entry=root+(next_&0x7fffffff)
   if next_&0x80000000:walk(entry,keys2,depth+1)
   else:
    begin=offset(U32(blob,entry));size=U32(blob,entry+4)
    if begin+size>len(blob):raise ValueError('Truncated resource')
    result.append((keys2,entry,begin,size))
 walk(root,[],0)
 return result,sections,opt

def shell_section(section):
 return any(section.startswith('['+key) for key in ['window','start','task','tray','clock'])

def neutral(r,g,b):
 # A continuous, monotonic curve keeps the original bevel/gradient geometry.
 # Never transform color keys, which XP themes also use instead of alpha.
 if (r,g,b) in {(0,0,0),(255,0,0),(255,0,255),(242,241,231)}:return r,g,b
 if max(r,g,b)-min(r,g,b)>46:return r,g,b
 light=(r+g+b)/3
 anchors=[(0,0),(32,22),(64,35),(96,46),(160,66),(224,85),(255,100)]
 for (low,a),(high,z) in zip(anchors,anchors[1:]):
  if light<=high:
   value=round(a+(z-a)*(light-low)/(high-low));break
 return max(0,value-3),value,min(255,value+8)

def surface_section(section):
 return any(section.startswith('['+name) for name in ['combobox','edit','spin','tab','header','scrollbar','button.pushbutton','toolbar.button','rebar','status'])

def artwork_color(r,g,b,glyph=False,surface=False,keys=()):
 if (r,g,b) in keys or (r,g,b) in {(255,0,0),(255,0,255),(242,241,231)}:return r,g,b
 if glyph:
  if max(r,g,b)<150:return 224,232,243
  if min(r,g,b)>200:return r,g,b
 if surface and (r,g,b)!=(0,0,0):
  # Colored control backgrounds need the same dark curve as neutral ones.
  # Retain a small source-theme tint and all original bevel/state geometry.
  light=round(.2126*r+.7152*g+.0722*b)
  base=neutral(light,light,light)
  return tuple(max(0,min(255,round(v+.10*(c-light)))) for v,c in zip(base,(r,g,b)))
 return neutral(r,g,b)

def bitmap(data,name,surface=False,keys=()):
 out=bytearray(data)
 if len(out)<40 or U32(out,0)<40:return data
 bpp=U16(out,14);compression=U32(out,16)
 glyph=any(x in name.upper() for x in ['GLYPH','CHECKMARK'])
 def color(r,g,b):return artwork_color(r,g,b,glyph,surface,keys)
 if bpp in (4,8):
  count=U32(out,32) or (1<<bpp);start=U32(out,0)
  if start+count*4>len(out):return data
  for at in range(start,start+count*4,4):
   b,g,r=out[at:at+3];r,g,b=color(r,g,b);out[at:at+3]=bytes((b,g,r))
  return bytes(out)
 if bpp not in (24,32) or compression not in (0,3):return data
 width,height=struct.unpack_from('<ii',out,4);height=abs(height)
 if width<=0 or width*height>4*1024*1024:return data
 start=U32(out,0)+4*U32(out,32)
 if compression==3 and U32(out,0)==40:start+=12
 stride=((width*bpp+31)//32)*4
 if start+stride*height>len(out):return data
 # Match uxtheme/Renderer::frame: 0/255-only alpha is not a blend mask.
 uses_alpha=bpp==32 and any(0<out[start+y*stride+x*4+3]<255 for y in range(height) for x in range(width))
 for y in range(height):
  for x in range(width):
   at=start+y*stride+x*(bpp//8);b,g,r=out[at:at+3]
   if uses_alpha and out[at+3]==0:continue
   r,g,b=color(r,g,b);out[at:at+3]=bytes((b,g,r))
 return bytes(out)

def build(source,destination):
 blob=bytearray(Path(source).read_bytes());entries,sections,opt=resources(blob)
 shell_images=set();surface_images=set();image_keys={}
 resource_name=lambda value:re.sub(r'[^A-Z0-9]','_',value.strip().upper())
 for names,_,begin,size in entries:
  if names[0].upper()!='TEXTFILE' or names[1].upper()=='THEMES_INI':continue
  sections_ini={};section=''
  for line in bytes(blob[begin:begin+size]).decode('utf-16le').splitlines():
   line=line.split(';',1)[0].strip()
   if line.startswith('['):section=line.lower();sections_ini.setdefault(section,{})
   elif '=' in line:
    key,value=line.split('=',1);sections_ini.setdefault(section,{})[key.strip().lower()]=value.strip()
  for section,props in sections_ini.items():
   base=re.sub(r'\([^)]*\)','',section)
   parent=base.split('.',1)[0].rstrip(']')+']'
   inherited={**sections_ini.get(parent,{}),**sections_ini.get(base,{}),**props}
   for key,value in props.items():
    if not re.fullmatch(r'(?:glyph)?imagefile\d*',key):continue
    name=resource_name(value)
    if shell_section(section):shell_images.add(name)
    elif surface_section(section) and not key.startswith('glyph'):surface_images.add(name)
    mask_key='glyphtransparentcolor' if key.startswith('glyph') else 'transparentcolor'
    mask=inherited.get(mask_key,'255 0 255')
    values=re.findall(r'\d+',mask)
    if len(values)==3:image_keys.setdefault(name,set()).add(tuple(map(int,values)))
 palette=configparser.ConfigParser();palette.read(ROOT/'palette.theme')
 colors={k.lower():v for k,v in palette['Control Panel\\Colors'].items()}
 last=max(sections,key=lambda s:s[1]+s[2]);va,raw,_,section=last
 # Discard the original signature directory: transformed artwork is unsigned.
 directory=opt+(112 if U16(blob,opt)==0x20b else 96)
 struct.pack_into('<II',blob,directory+32,0,0)
 changed=0
 for keys,entry,begin,size in entries:
  data=bytes(blob[begin:begin+size]);updated=data
  if keys[0].upper()=='TEXTFILE':
   text=data.decode('utf-16le').rstrip('\0')
   keep_accent='[xplasmacustomluna]' in text.lower()
   protected=SHELL_COLORS | ({'hilight','hilighttext','hottrackingcolor'} if keep_accent else set())
   in_metrics=False;section_name='';lines=[]
   for line in text.splitlines():
    if line.strip().startswith('['):
     section_name=line.strip().lower();in_metrics=section_name=='[sysmetrics]'
    m=re.match(r'(\s*)([\w]+)(\s*=\s*)(\d+)[,\s]+(\d+)[,\s]+(\d+)(\s*)$',line)
    if m and not shell_section(section_name):
     key=m[2].lower();rgb=tuple(map(int,m.group(4,5,6)))
     aliases={'highlight':'hilight','highlighttext':'hilighttext','btnface':'buttonface','btnshadow':'buttonshadow','btnhighlight':'buttonhilight','btndkshadow':'buttondkshadow','btnlight':'buttonlight','btntext':'buttontext','activecaption':'activetitle','captiontext':'titletext','inactivecaption':'inactivetitle','inactivecaptiontext':'inactivetitletext','gradientactivecaption':'gradientactivetitle','gradientinactivecaption':'gradientinactivetitle','dkshadow3d':'buttondkshadow','light3d':'buttonlight'}
     canonical=aliases.get(key,key)
     if in_metrics and canonical in colors and canonical not in SHELL_COLORS and not (keep_accent and canonical in {'hilight','hilighttext','hottrackingcolor'}):values=tuple(map(int,colors[canonical].split()))
     elif key in {'textcolor','glyphtextcolor'} and max(rgb)<150:values=(233,237,244)
     elif key in {'fillcolor','bordercolor','edgefillcolor','edgehighlightcolor','edgelightcolor','edgeshadowcolor','edgedkshadowcolor','gradientcolor1','gradientcolor2','gradientcolor3','gradientcolor4','gradientcolor5'}:values=artwork_color(*rgb,surface=surface_section(section_name))
     else:values=rgb
     line=m[1]+m[2]+m[3]+', '.join(map(str,values))+m[7]
    lines.append(line)
   if keys[1].upper()=='THEMES_INI':
    joined='\r\n'.join(lines)
    joined=re.sub(r'(?im)^(DisplayName\s*=).*',r'\1Zune Dark',joined,count=1)
    lines=joined.splitlines()
   else:
    lines+=['','[SysMetrics]']+[key+' = '+value for key,value in colors.items() if key not in protected]
    mapping={'btnface':'buttonface','btntext':'buttontext','highlight':'hilight','highlighttext':'hilighttext','activecaption':'activetitle','captiontext':'titletext','inactivecaption':'inactivetitle','inactivecaptiontext':'inactivetitletext','gradientactivecaption':'gradientactivetitle','gradientinactivecaption':'gradientinactivetitle','btnshadow':'buttonshadow','btnhighlight':'buttonhilight','dkshadow3d':'buttondkshadow','light3d':'buttonlight'}
    lines+=[key+' = '+colors[value] for key,value in mapping.items() if value not in protected]
   updated=('\r\n'.join(lines)+'\r\n\0').encode('utf-16le')
  elif keys[0]=='2' and keys[1].upper() not in shell_images:updated=bitmap(data,keys[1],keys[1].upper() in surface_images,image_keys.get(keys[1].upper(),()))
  if updated==data:continue
  changed+=1
  if len(updated)<=size:
   blob[begin:begin+len(updated)]=updated
   struct.pack_into('<I',blob,entry+4,len(updated))
  else:
   blob.extend(b'\0'*((-len(blob))%4));at=len(blob);blob.extend(updated)
   struct.pack_into('<II',blob,entry,va+at-raw,len(updated))
 virtual=len(blob)-raw;alignment=U32(blob,opt+36)
 blob.extend(b'\0'*((-len(blob))%alignment))
 struct.pack_into('<I',blob,section+8,virtual);struct.pack_into('<I',blob,section+16,len(blob)-raw)
 section_alignment=U32(blob,opt+32);struct.pack_into('<I',blob,opt+56,((va+virtual+section_alignment-1)//section_alignment)*section_alignment)
 struct.pack_into('<I',blob,opt+64,0)
 destination=Path(destination);destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(blob)
 theme=(ROOT/'palette.theme').read_text().replace('using XP\'s Classic renderer and default Tahoma metrics.','using Royale Noir artwork and XP Tahoma metrics.')
 theme+='\n[VisualStyles]\nPath='+destination.name+'\nColorStyle=NormalColor\nSize=NormalSize\n'
 destination.with_suffix('.theme').write_text(theme)
 print('Built Zune Dark:',changed,'application resources modified;',len(shell_images),'shell images preserved')

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('source',type=Path);p.add_argument('destination',type=Path);a=p.parse_args();build(a.source,a.destination)
