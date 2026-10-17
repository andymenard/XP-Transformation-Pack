#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install signed Alpha native updates, reusing matching prebuilt binaries or building against the receiver's Qt."""
import argparse,hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent
VERSION='1.0.0-alpha.4'

def verify():
 for name,sha in json.loads((ROOT/'files.json').read_text()).items():
  path=ROOT/name
  if Path(name).is_absolute() or '..' in Path(name).parts or path.is_symlink() or not path.is_file() or hashlib.sha256(path.read_bytes()).hexdigest()!=sha:
   raise RuntimeError('Missing or modified update resource: '+name)

def main():
 parser=argparse.ArgumentParser();parser.add_argument('--check',action='store_true');parser.add_argument('--undo',action='store_true');parser.add_argument('--interactive',action='store_true');args=parser.parse_args()
 if os.getuid()==0:raise RuntimeError('Run as your desktop user, without sudo.')
 verify()
 if args.check:print('Signed setup inventory verified.');return
 sys.path.insert(0,str(ROOT/'installer'));import backend as b
 home=Path.home()
 state=None
 for name in ['kde-xp-suite','kubuntu-xp-suite','fedora-xp-installer']:
  candidate=home/'.local/share'/name
  if (candidate/'installation.json').is_file():state=candidate;break
 if state is None:raise RuntimeError('No XP desktop installation found.')
 info=json.loads((state/'installation.json').read_text());features=set(info.get('features',[]))
 if not features and state.name=='fedora-xp-installer':
  data=json.loads((home/'.local/share/xplasma/state.json').read_text())
  features={k for k,v in data.get('settings',{}).get('installerFeatures',{}).items() if v}
 backup=home/'.local/share/xplasma/maintenance'/('alpha-'+VERSION)
 if args.undo:
  snapshot=b.Snapshot(home,backup,json.loads((backup/'tracked.json').read_text()));conflicts=snapshot.restore()
  if conflicts:raise RuntimeError('Preserved later edits: '+', '.join(conflicts))
  print('Previous native files restored. Log out and back in.');return
 if (backup/'after.json').exists():raise RuntimeError('Setup already completed. Backup: '+str(backup))
 import build_cache
 kind='taskbar' if 'taskbar' in features else 'core'
 cached=build_cache.find(ROOT/'installer',kind);ready=bool(cached and build_cache.usable(cached))
 caption='caption_animation' in features
 caption_binary=build_cache.find_caption(ROOT/'installer') if caption else None
 b.root('dependencies',{'apps'}|({'taskbar'} if kind=='taskbar' else set())|({'caption_animation'} if caption else set()),native_ready=ready,caption_ready=bool(caption_binary))
 # A shared build directory keeps later alpha updates incremental.
 build=home/'.cache/xplasma-alpha-update'
 b.run([ROOT/'installer/build-native.sh',build,kind])
 runtime=str(state.relative_to(home))+'/runtime'
 paths=[runtime,'.local/lib/qt6/plugins/plasma/applets','.local/lib/qt6/plugins/styles','.local/share/fonts/xp','.local/share/plasma/plasmoids/fyi.hotsocket.xplasma.start','.config/kwinrc',str(state.relative_to(home))+'/after.json']
 session='session' in features and (state/'templates/session').is_dir()
 if session:paths+=[str(state.relative_to(home))+'/templates/session','.local/share/plasma/look-and-feel/local.xp.session.desktop']
 if caption:paths+=['.local/share/kwin/xplasma/caption-source','.local/lib/qt6/plugins/kwin/effects/plugins/xp-caption-animation.so']
 flatpak=home/'.local/share/flatpak/extension/org.kde.KStyle.XPlasma'
 if flatpak.exists():paths.append(str(flatpak.relative_to(home)))
 for name in paths:
  destination=home/name
  if any(p.is_symlink() for p in [destination,*destination.parents] if p!=home.parent):raise RuntimeError('Refusing linked destination: '+name)
 snapshot=b.Snapshot(home,backup,paths);snapshot.save()
 try:
  b.copy(build/'runtime',state/'runtime')
  styles=list((build/'runtime').rglob('styles/libxplasma.so'))
  if len(styles)!=1:raise RuntimeError('Cannot locate the matching style plugin.')
  if 'apps' in features:
   b.copy(styles[0],home/'.local/lib/qt6/plugins/styles/libxplasma.so')
   for existing in flatpak.glob('*/'+'.'.join(map(str,b.detect()['qt'][:2]))+'/styles/libxplasma.so'):
    b.copy(styles[0],existing)
  if 'taskbar' in features:
   series='.'.join(map(str,b.detect()['plasma'][:2]))
   for plugin in (build/'runtime/lib/xplasma/forks'/series/'plasma/applets').glob('*.so'):
    b.copy(plugin,home/'.local/lib/qt6/plugins/plasma/applets'/plugin.name)
   package=build/'source/xplasma/src/app/start-menu'
   b.copy(package/'contents',home/'.local/share/plasma/plasmoids/fyi.hotsocket.xplasma.start/contents')
   b.copy(package/'metadata.json',home/'.local/share/plasma/plasmoids/fyi.hotsocket.xplasma.start/metadata.json')
   # Locate the package's versioned QML module rather than assuming its name.
   modules=[p.parent for p in (package/'contents/ui').glob('*/qmldir') if 'plugin xpmenuguard' in p.read_text()]
   if len(modules)!=1:raise RuntimeError('Cannot locate the Start-menu helper module.')
   target=home/'.local/share/plasma/plasmoids/fyi.hotsocket.xplasma.start'/modules[0].relative_to(package)
   b.copy(build/'runtime/lib/xplasma/start-menu/libxpmenuguard.so',target/'libxpmenuguard.so')
  if features&{'fonts','apps','theme_manager'}:
   b.copy(ROOT/'fonts',home/'.local/share/fonts/xp');b.run(['fc-cache','-f'])
  if session:
   # The session-theme watcher restores from the state copy after theme changes.
   b.copy(ROOT/'session',state/'templates/session')
   b.copy(ROOT/'session',home/'.local/share/plasma/look-and-feel/local.xp.session.desktop')
  if caption:
   b.copy(ROOT/'installer/payload/caption-animation',home/'.local/share/kwin/xplasma/caption-source')
   effect=home/'.local/lib/qt6/plugins/kwin/effects/plugins/xp-caption-animation.so'
   if caption_binary:b.copy(caption_binary,effect)
   else:
    b.run([sys.executable,home/'.local/share/kwin/xplasma/caption-source/build-local.py'])
    build_cache.store_component(effect,'lib/xplasma/caption/xp-caption-animation.so',build_cache.caption_component_identity(ROOT/'installer'))
  if 'behavior' in features:
   for effect in ['overview','windowview','slide','windowaperture']:
    b.config('kwinrc','Plugins',effect+'Enabled','true')
   b.run([b.kdbus(),'org.kde.KWin','/KWin','reconfigure'],check=False)
  ledger=state/'after.json'
  if ledger.exists():
   data=json.loads(ledger.read_text())
   # Update owned files and newly installed files inside existing managed trees;
   # the original before.json and its backup are never changed.
   tracked=json.loads((state/'tracked.json').read_text()) if (state/'tracked.json').exists() else []
   for name,value in b.inventory(home,paths).items():
    if name==str(ledger.relative_to(home)):continue
    if name in data or any(name==p or name.startswith(p+'/') for p in tracked):data[name]=value
   ledger.write_text(json.dumps(data,indent=2)+'\n')
  snapshot.finish()
 except BaseException:
  snapshot.finish();snapshot.restore();raise
 print('Alpha native updates installed. Log out and back in to reload them. Backup:',backup)

if __name__=='__main__':
 try:main()
 except Exception as error:raise SystemExit(str(error))
 finally:
  if '--interactive' in sys.argv:
   try:input('\nPress Enter to close…')
   except EOFError:pass
