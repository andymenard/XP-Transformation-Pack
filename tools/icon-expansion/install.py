#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install the expanded XP layer and repair the primary theme's inheritance."""
import argparse,json,shutil,re,hashlib,tempfile,html
from pathlib import Path
from PIL import Image
LAYER='xplasma-xp-extended'

def copy_layer(source,home,hidpi=None):
 source=Path(source).resolve();home=Path(home);icons=home/'.local/share/icons';icons.mkdir(parents=True,exist_ok=True)
 for link in source.rglob('*'):
  if link.is_symlink() and (not link.exists() or not link.resolve().is_relative_to(source)):
   raise RuntimeError('Invalid icon alias: '+str(link.relative_to(source)))
 dest=icons/LAYER
 with tempfile.TemporaryDirectory(prefix='.xp-icons-',dir=icons) as folder:
  stage=Path(folder)/'theme';old=Path(folder)/'old'
  shutil.copytree(source,stage,symlinks=True)
  if hidpi is not None:(stage/'RESOLUTION.json').write_text(json.dumps({'hidpi':bool(hidpi)})+'\n')
  elif (dest/'RESOLUTION.json').is_file():shutil.copy2(dest/'RESOLUTION.json',stage/'RESOLUTION.json')
  if dest.exists():dest.rename(old)
  try:stage.rename(dest)
  except BaseException:
   if old.exists():old.rename(dest)
   raise
 # Refresh the owned layer's uninstall fingerprints after replacement.
 # Do not claim unrelated user icon themes.
 prefix=str(dest.relative_to(home))+'/'
 for state in ['fedora-xp-installer','kde-xp-suite','kubuntu-xp-suite','xp-accessory-suite']:
  ledger=home/'.local/share'/state/'after.json'
  if not ledger.is_file():continue
  data=json.loads(ledger.read_text())
  if prefix+'index.theme' not in data and '.local/share/icons/xplasma-icons/index.theme' not in data:continue
  data={key:value for key,value in data.items() if not key.startswith(prefix)}
  for path in dest.rglob('*'):
   key=str(path.relative_to(home))
   if path.is_symlink():data[key]={'kind':'link','value':str(path.readlink())}
   elif path.is_file():data[key]={'kind':'file','value':hashlib.sha256(path.read_bytes()).hexdigest()}
  ledger.write_text(json.dumps(data,indent=2)+'\n')

def repair(home=None):
 home=Path(home or Path.home());icons=home/'.local/share/icons';primary=icons/'xplasma-icons';layer=icons/LAYER
 if not (layer/'index.theme').is_file() or not (primary/'index.theme').is_file():return
 provenance=json.loads((layer/'ALIAS-PROVENANCE.json').read_text());names=set(provenance)
 controls={name:item['control'] for name,item in provenance.items() if item.get('reason')=='control-glyph' and item.get('control')}
 metadata=primary/'xplasma-icons.json'
 native_names=set(json.loads(metadata.read_text()).get('icons',{})) if metadata.is_file() else set()
 changed=set()
 resolution=layer/'RESOLUTION.json'
 hidpi=json.loads(resolution.read_text()).get('hidpi',False) if resolution.exists() else False
 backup=home/'.local/share/xplasma/maintenance/icon-coverage';backup.mkdir(parents=True,exist_ok=True)
 # Keep battery capacity and charging indication from the original XP renders.
 for alias in names:
  match=re.fullmatch(r'(battery-\d{3}(?:-charging)?)-profile-[^-]+(?:-symbolic)?',alias)
  if not match:continue
  source=match.group(1)
  originals=list(primary.glob(f'*x*/status/{source}.png'))
  if not originals:continue
  for target in layer.glob(f'*x*/status/{alias}.png'):
   size=int(target.parent.parent.name.split('x')[0]);chosen=min(originals,key=lambda p:abs(int(p.parent.parent.name.split('x')[0])-size))
   image=Image.open(chosen).convert('RGBA').resize((size,size),Image.Resampling.LANCZOS)
   if target.is_symlink():target.unlink()
   image.save(target)
 removed=0
 paths=set(primary.rglob('*.svg'))
 if (backup/'primary').exists():
  paths.update(primary/p.relative_to(backup/'primary') for p in (backup/'primary').rglob('*.svg'))
 for path in paths:
  if path.stem not in names or path.stem in controls:continue
  dest=backup/'primary'/path.relative_to(primary);dest.parent.mkdir(parents=True,exist_ok=True)
  if path.exists() and not dest.exists():shutil.copy2(path,dest)
  name=path.stem
  if path.exists():path.unlink();removed+=1
  png=path.with_suffix('.png')
  if name not in native_names:
   sized=path.parent.parent.name
   replacements=list(layer.glob(sized+'/*/'+name+'.png'))
   if replacements and (not png.exists() or png.read_bytes()!=replacements[0].read_bytes()):
    if png.exists() and not (backup/'primary'/png.relative_to(primary)).exists():
     saved=backup/'primary'/png.relative_to(primary);saved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(png,saved)
    shutil.copyfile(replacements[0],png);changed.add(png)
 index=primary/'index.theme';s=index.read_text()
 if not (backup/'index.theme.before').exists():shutil.copy2(index,backup/'index.theme.before')
 line=re.search(r'^Inherits=(.*)$',s,re.M)
 if line:
  old=[x for x in line.group(1).split(',') if x and x!=LAYER]
  s=s[:line.start()]+f'Inherits={LAYER},'+','.join(old)+s[line.end():]
  index.write_text(s)
 # Exact tray names must exist in the primary theme: otherwise icon-name
 # shortening can select a generic KDE icon before inheritance is searched.
 # Retain original Microsoft capacity, charging and native tray renders.
 for name,item in provenance.items():
  if name in controls:continue
  if item.get('reason') not in {'tray-state','control-glyph'} or (item.get('reason')=='tray-state' and name in native_names):continue
  for path in layer.glob('*x*/*/'+name+'.png'):
   target=primary/path.relative_to(layer);target.parent.mkdir(parents=True,exist_ok=True)
   if not target.is_file() or target.read_bytes()!=path.read_bytes():
    if target.exists() and not (backup/'primary'/target.relative_to(primary)).exists():
     saved=backup/'primary'/target.relative_to(primary);saved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(target,saved)
    shutil.copyfile(path,target);changed.add(target)
 if hidpi:
  original=json.loads(metadata.read_text()).get('icons',{}) if metadata.is_file() else {}
  for name,item in original.items():
   alias=provenance.get(name,{})
   if alias.get('reason')!='exact' or alias.get('context')=='status':continue
   for path in layer.glob('*x*/*/'+name+'.png'):
    if int(path.parent.parent.name.split('x')[0])<48:continue
    context=next(iter(primary.glob('*x*/*/'+name+'.png')),None)
    if not context:continue
    target=primary/path.parent.parent.name/context.parent.name/path.name
    target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,target);changed.add(target)
    folder=str(target.parent.relative_to(primary))
    if '\n['+folder+']' not in s:
     s=re.sub(r'^Directories=(.*)$',lambda m:m.group(0)+','+folder,s,flags=re.M)
     s+='\n['+folder+']\nSize='+path.parent.parent.name.split('x')[0]+'\nContext='+context.parent.name.title()+'\nType=Fixed\n'
  index.write_text(s)
 # Control marks are foreground ink, not fixed-color XP pictograms. Keep
 # their existing XP silhouettes while allowing KDE to apply the text palette.
 for name,spec in controls.items():
  svg=('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">'
       '<style type="text/css" id="current-color-scheme">.ColorScheme-Text {color:#000000;}</style>'
       '<path class="ColorScheme-Text" fill="currentColor" fill-rule="evenodd" d="'+html.escape(spec[1],quote=True)+'"/></svg>').encode()
  sources=list(layer.glob('*x*/*/'+name+'.png'))+list(layer.glob('*x*/*/'+name+'.svg'))
  folders={path.parent for path in sources}
  targets={folder/(name+'.svg') for folder in folders}
  targets.update(primary/path.relative_to(layer) for path in list(targets))
  targets.update(primary.glob('*x*/*/'+name+'.svg'))
  targets.update(path.with_suffix('.svg') for path in primary.glob('*x*/*/'+name+'.png'))
  for target in targets:
   target.parent.mkdir(parents=True,exist_ok=True)
   png=target.with_suffix('.png')
   if png.exists() or png.is_symlink():
    saved=backup/('primary' if png.is_relative_to(primary) else 'layer')/png.relative_to(primary if png.is_relative_to(primary) else layer)
    if not saved.exists():saved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(png,saved)
    png.unlink();changed.add(png)
   if target.is_symlink():target.unlink()
   if not target.exists() or target.read_bytes()!=svg:target.write_bytes(svg);changed.add(target)
   if target.is_relative_to(primary):
    folder=str(target.parent.relative_to(primary))
    if '\n['+folder+']' not in s:
     s=re.sub(r'^Directories=(.*)$',lambda m:m.group(0)+','+folder,s,flags=re.M)
     s+='\n['+folder+']\nSize='+target.parent.parent.name.split('x')[0]+'\nContext='+target.parent.name.title()+'\nType=Fixed\n'
 index.write_text(s)
 # KDE recolors SVGs only when the supplying theme opts into its palette.
 if controls:
  for theme_index in [index,layer/'index.theme']:
   text=s if theme_index==index else theme_index.read_text()
   if re.search(r'^FollowsColorScheme=',text,re.M):text=re.sub(r'^FollowsColorScheme=.*$', 'FollowsColorScheme=true',text,flags=re.M)
   else:text=text.replace('[Icon Theme]','[Icon Theme]\nFollowsColorScheme=true',1)
   theme_index.write_text(text);changed.add(theme_index)
 # Keep the generated-theme provenance in step with the actual files.
 metadata=primary/'xplasma-icons.json'
 if metadata.is_file():
  saved=backup/'xplasma-icons.json.before'
  if not saved.exists():shutil.copy2(metadata,saved)
  data=json.loads(metadata.read_text())
  data['pinned']={k:v for k,v in data.get('pinned',{}).items() if k not in names}
  data['coverage']={'theme':LAYER,'mapped_names':len(names),'credit':'marchmountain / CC0; XPlasma mappings'}
  metadata.write_text(json.dumps(data,indent=2)+'\n')
 # Update owned fingerprints for changed primary files; unrelated files stay untouched.
 for state in ['fedora-xp-installer','kde-xp-suite','kubuntu-xp-suite','xp-accessory-suite']:
  record=home/'.local/share'/state/'after.json'
  if not record.is_file():continue
  data=json.loads(record.read_text());key=str(index.relative_to(home))
  if key in data:data[key]={'kind':'file','value':hashlib.sha256(index.read_bytes()).hexdigest()}
  if metadata.is_file() and str(metadata.relative_to(home)) in data:data[str(metadata.relative_to(home))]={'kind':'file','value':hashlib.sha256(metadata.read_bytes()).hexdigest()}
  if key in data:
   for path in changed:
    key=str(path.relative_to(home))
    if path.is_file():data[key]={'kind':'file','value':hashlib.sha256(path.read_bytes()).hexdigest()}
    else:data.pop(key,None)
  record.write_text(json.dumps(data,indent=2)+'\n')
 print('XP fallback selected;',removed,'covered Breeze SVG overrides removed.')

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path);a=p.parse_args();home=Path.home()
 if a.source:
  copy_layer(a.source,home)
 repair(home)
