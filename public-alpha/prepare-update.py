#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare the bounded, feature-gated Alpha 2 OTA and native setup inventory."""
import argparse,hashlib,json,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PREFIX='.local/share/xp-accessories/alpha-update/'

def prepare(bundle,previous,out):
 bundle,previous,out=map(lambda p:Path(p).resolve(),(bundle,previous,out));out.mkdir(parents=True,exist_ok=True)
 stage=out/'setup';stage.mkdir(exist_ok=True)
 helpers=stage/'installer';helpers.mkdir(exist_ok=True)
 for name in ['backend.py','features.json','system_profile.py','dependencies.py','root_helper.py','build-native.sh','build-widgets.py','build_cache.py']:
  shutil.copy2(bundle/name,helpers/name)
 (helpers/'source').mkdir(exist_ok=True);shutil.copy2(bundle/'source/xplasma-source.tar.gz',helpers/'source/xplasma-source.tar.gz')
 for archive in (bundle/'source').glob('plasma-*.tar.xz'):shutil.copy2(archive,helpers/'source'/archive.name)
 (stage/'fonts').mkdir(exist_ok=True)
 for name in ['ms_sans_serif.woff','ms_sans_serif_bold.woff','CRISP-FONT-CREDITS.md']:
  shutil.copy2(bundle/'payload/assets/fonts'/name,stage/'fonts'/name)
 shutil.copy2(ROOT/'public-alpha/update-setup.py',stage/'setup.py')
 inventory={str(p.relative_to(stage)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(stage.rglob('*')) if p.is_file() and p.name!='files.json' and '__pycache__' not in p.parts}
 (stage/'files.json').write_text(json.dumps(inventory,indent=2)+'\n')
 desktop=out/'update-setup.desktop'
 desktop.write_text('[Desktop Entry]\nType=Application\nName=XP Alpha Update Setup\nComment=Build the signed Alpha 2 fixes for this computer\nExec=sh -c "cd && exec python3 .local/share/xp-accessories/alpha-update/setup.py --interactive"\nIcon=preferences-desktop-theme\nTerminal=true\nCategories=Settings;\n')
 files=[]
 def add(source,target,feature,old=None,mode=0o644,features_any=None):
  item={'source':str(source),'target':target,'feature':feature,'mode':mode}
  if old and old.is_file():item['base_sha256']=[hashlib.sha256(old.read_bytes()).hexdigest()]
  if features_any:item['features_any']=features_any
  files.append(item)
 for p in sorted(stage.rglob('*')):
  if p.is_file() and '__pycache__' not in p.parts:
   add(p,PREFIX+str(p.relative_to(stage)),'desktop',mode=0o755 if p.suffix=='.sh' else 0o644,features_any=['apps','theme_manager','taskbar','fonts'])
 add(desktop,'.local/share/applications/fyi.hotsocket.xp.alpha-update.desktop','desktop',features_any=['apps','theme_manager','taskbar','fonts'])
 add(bundle/'payload/tools/xp-behavior.py','@state/runtime/tools/xp-behavior.py','behavior',previous/'payload/tools/xp-behavior.py')
 chrome=bundle/'payload/firefox/profile/chrome'
 for p in sorted(chrome.rglob('*')):
  if p.is_file():
   relative=str(p.relative_to(chrome));add(p,'.local/share/firefox-esr-ie6/profile/chrome/'+relative,'firefox',previous/'payload/firefox/profile/chrome'/relative)
 add(bundle/'payload/firefox/runtime-overrides/ie6-ui.cfg','.local/share/firefox-esr-ie6/firefox/ie6-ui.cfg','firefox',previous/'payload/firefox/runtime-overrides/ie6-ui.cfg')
 for name in ['core.py','gui.py','cli.py','install.py','README.md']:
  p=bundle/'payload/updater'/name
  if p.is_file():add(p,'.local/share/xplasma-updater/app/'+name,'updates',previous/'payload/updater'/name)
 total=sum(Path(f['source']).stat().st_size for f in files)
 if total>512*1024*1024:raise RuntimeError('OTA exceeds client limit')
 notes=(ROOT/'public-alpha/ALPHA-2.md').read_text()
 notes='\n'.join('**Existing installation:** close XPlasma Properties, then install through the current XPlasma Update client. Native setup runs in the same window with live output and a coordinated rollback backup. Use Finish pending native setup for an older applied update. If the updater updates itself, reopen it once to load the new client. Log out/in after native changes.' if line.startswith('**Existing Alpha 1.0 installation:**') else line for line in notes.splitlines())+'\n'
 spec={'version':'1.0.0-alpha.2','notes':notes,'files':files,'native_setup':{'script':PREFIX+'setup.py','backup':'.local/share/xplasma/maintenance/alpha-1.0.0-alpha.2'}}
 (out/'spec.json').write_text(json.dumps(spec,indent=2)+'\n')
 print('Prepared',len(files),'OTA resources;',total,'uncompressed bytes')

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('bundle');p.add_argument('previous');p.add_argument('output');a=p.parse_args();prepare(a.bundle,a.previous,a.output)
