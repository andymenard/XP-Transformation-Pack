#!/usr/bin/python3
"""Transactional, feature-selectable portable KDE XP installer backend."""
import argparse,configparser,hashlib,json,os,platform,re,shutil,subprocess,sys,tarfile,tempfile,traceback
from pathlib import Path
BASE=Path(__file__).resolve().parent
sys.path.insert(0,str(BASE/'lib'))
import tomlkit
ACCESSORIES={'notepad','calculator','paint','picture_viewer','media_player','sound_recorder','volume_control','command_prompt','task_manager','pinball','minesweeper','solitaire'}
from system_profile import detect,capabilities
STATE_NAME='kde-xp-suite'
FEATURES=json.loads((BASE/'features.json').read_text())
KNOWN={f['id'] for f in FEATURES}
NATIVE={f['id'] for f in FEATURES if f['native']}
LOOK={'desktop','apps','taskbar','fonts','icons','sounds','wallpaper'}
THEMES={'Luna Blue':('Luna/luna.msstyles','NormalColor'),'Luna Silver':('Luna/luna.msstyles','Metallic'),
 'Luna Olive Green':('Luna/luna.msstyles','HomeStead'),'Royale':('Royale/Royale.msstyles','NormalColor'),
 'Royale Noir':('Royale Noir/Royale Noir.msstyles','Metallic'),'Zune':('Zune/Zune.msstyles','NormalColor'),
 'Embedded':('Embedded/Embedded.msstyles','NormalColor')}
USER_PATHS=['.local/share/xplasma-updater/app','.local/share/xplasma-updater/config.json','.local/share/applications/xplasma-update.desktop','.local/share/applications/fyi.hotsocket.xp.accessories.desktop','.local/share/xp-accessories','.config/menus/applications-merged/xp-accessories.menu','.local/share/desktop-directories/xp-accessories.directory','.local/share/desktop-directories/xp-entertainment.directory','.local/share/desktop-directories/xp-games.directory','.local/share/konsole/XP Command Prompt.profile','.local/share/konsole/XP Command Prompt.colorscheme',*[('.local/share/applications/fyi.hotsocket.xp.'+key.replace('_','-')+'.desktop') for key in sorted(ACCESSORIES)],'.config/kdeglobals','.config/mimeapps.list','.config/kwinrc','.config/kwinrulesrc','.config/klaunchrc','.config/kcminputrc',
 '.config/plasmashellrc','.config/plasma-org.kde.plasma.desktop-appletsrc','.config/ksplashrc','.local/share/plasma/look-and-feel/local.xp.welcome.desktop','.config/xplasmarc','.config/plasma_workspace.notifyrc','.config/ksmserver.notifyrc',
 '.config/gtk-3.0','.config/gtk-4.0','.config/fontconfig','.config/plasma-workspace/env',
 '.local/share/applications/fyi.hotsocket.xp.caption-animation.desktop','.config/autostart/xp-decoration-compatibility.desktop','.config/systemd/user/xp-session-theme.path','.config/systemd/user/xp-session-theme.service',
 '.config/systemd/user/xp-cursor-sync.path','.config/systemd/user/xp-cursor-sync.service',
 '.local/share/plasma','.local/share/knotifications6/xplasma.notifyrc','.local/share/aurorae','.local/share/kwin','.local/share/color-schemes',
 '.local/share/icons','.local/share/fonts/xp','.local/share/themes','.local/share/xplasma','.local/share/sounds',
 '.local/lib/qt6/plugins','.local/share/flatpak/overrides','.local/share/flatpak/extension',
 '.local/share/firefox-esr-ie6','.local/share/applications/firefox-esr-ie6.desktop',
 '.local/share/applications/fyi.hotsocket.xplasma.desktop','.local/share/applications/fyi.hotsocket.xplasma.datetime.desktop',
 '.local/share/applications/kde-xp-suite.desktop','.local/share/xp-desktop-shell',
 '.local/share/applications/fyi.hotsocket.xp.explorer.desktop','.local/share/applications/fyi.hotsocket.xp.control-panel.desktop','.local/share/applications/fyi.hotsocket.xp.shell.desktop','.codex/config.toml','.codex/pets/clippy','.icons']
GNOME_KEYS=['gtk-theme','icon-theme','font-name','monospace-font-name','cursor-theme','cursor-size']
def log(message):print(message,flush=True)
def phase(message):print('@@'+json.dumps({'phase':message}),flush=True)
def run(args,capture=False,check=True,env=None):
 if capture:
  p=subprocess.run(list(map(str,args)),text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,check=check,env=env)
  return p.stdout.strip()
 log('$ '+' '.join(map(str,args)))
 return subprocess.run(list(map(str,args)),check=check,env=env).returncode
def kdbus():return shutil.which('qdbus6') or shutil.which('qdbus-qt6') or '/usr/lib/qt6/bin/qdbus'
def qt_env():return {**os.environ,'QT_QPA_PLATFORM':'offscreen','QT_QUICK_BACKEND':'software'}
def copy(source,dest):
 source,dest=Path(source),Path(dest);dest.parent.mkdir(parents=True,exist_ok=True)
 if source.is_symlink():
  if dest.exists() or dest.is_symlink():dest.unlink()
  dest.symlink_to(os.readlink(source))
 elif source.is_dir():
  dest.mkdir(parents=True,exist_ok=True)
  for child in source.iterdir():copy(child,dest/child.name)
 else:
  fd,name=tempfile.mkstemp(prefix='.xp-copy-',dir=dest.parent);os.close(fd)
  try:shutil.copy2(source,name);os.replace(name,dest)
  finally:
   if Path(name).exists():Path(name).unlink()
def digest(p):
 if p.is_symlink():return {'kind':'link','value':os.readlink(p)}
 if p.is_file():return {'kind':'file','value':hashlib.sha256(p.read_bytes()).hexdigest()}
 return None
def inventory(home,paths):
 result={}
 for name in paths:
  p=home/name
  if p.is_dir() and not p.is_symlink():
   for child in p.rglob('*'):
    if (value:=digest(child)):result[str(child.relative_to(home))]=value
  elif (value:=digest(p)):result[name]=value
 return result
def ini(p):
 c=configparser.RawConfigParser(strict=False,delimiters=('=',));c.optionxform=str;c.read(p);return c
def ini_map(c):return {(g,k):v for g in c.sections() for k,v in c.items(g)}
def restore_ini(before,after,current):
 b,a,c=map(ini,[before,after,current]);bm,am,cm=map(ini_map,[b,a,c]);conflicts=[]
 for g,k in set(bm)|set(am):
  key=(g,k)
  if bm.get(key)==am.get(key):continue
  if cm.get(key)!=am.get(key):conflicts.append(g+'/'+k);continue
  if key in bm:
   if not c.has_section(g):c.add_section(g)
   c.set(g,k,bm[key])
  else:c.remove_option(g,k)
 for g in c.sections():
  if not c.items(g) and g not in b.sections():c.remove_section(g)
 with current.open('w') as f:c.write(f)
 return conflicts
def restore_toml(before,after,current):
 b=tomlkit.parse(before.read_text());a=tomlkit.parse(after.read_text());c=tomlkit.parse(current.read_text())
 bm,am,cm=[v.get('desktop',{}) for v in [b,a,c]];conflicts=[]
 if 'desktop' not in c:c['desktop']=tomlkit.table()
 for key in set(bm)|set(am):
  if bm.get(key)==am.get(key):continue
  if cm.get(key)!=am.get(key):conflicts.append(key);continue
  if key in bm:c['desktop'][key]=bm[key]
  elif key in c['desktop']:del c['desktop'][key]
 current.write_text(tomlkit.dumps(c));return conflicts
def gnome():
 if not shutil.which('gsettings'):return {}
 return {key:run(['gsettings','get','org.gnome.desktop.interface',key],capture=True,check=False) for key in GNOME_KEYS}
class Snapshot:
 def __init__(self,home,state,paths):self.home,self.state,self.paths=home,state,paths
 def save(self):
  self.state.mkdir(parents=True,exist_ok=True);self.state.chmod(0o700)
  data=inventory(self.home,self.paths)
  for name in data:copy(self.home/name,self.state/'backup'/name)
  (self.state/'before.json').write_text(json.dumps(data,indent=2)+'\n')
  (self.state/'tracked.json').write_text(json.dumps(self.paths)+'\n')
  (self.state/'gnome-before.json').write_text(json.dumps(gnome())+'\n')
  (self.state/'session-unit-before.json').write_text(json.dumps({'enabled':run(['systemctl','--user','is-enabled','xp-session-theme.path'],capture=True,check=False)=='enabled'})+'\n')
  (self.state/'cursor-unit-before.json').write_text(json.dumps({'enabled':run(['systemctl','--user','is-enabled','xp-cursor-sync.path'],capture=True,check=False)=='enabled'})+'\n')
 def finish(self):
  data=inventory(self.home,self.paths)
  (self.state/'after.json').write_text(json.dumps(data,indent=2)+'\n')
  for name in data:
   p=self.home/name
   if name.startswith('.config/') or name=='.codex/config.toml':copy(p,self.state/'installed-config'/name)
  (self.state/'gnome-after.json').write_text(json.dumps(gnome())+'\n')
 def restore(self):
  before=json.loads((self.state/'before.json').read_text());after=json.loads((self.state/'after.json').read_text());conflicts=[]
  for name,installed in after.items():
   if name in before:continue
   p=self.home/name
   if digest(p)==installed:p.unlink()
   elif p.exists() or p.is_symlink():conflicts.append(name)
  for name,saved in before.items():
   if saved==after.get(name):continue
   p=self.home/name;backup=self.state/'backup'/name;installed=self.state/'installed-config'/name
   if digest(p)==after.get(name) or not p.exists():copy(backup,p)
   elif installed.is_file() and p.is_file() and not p.is_symlink():
    try:
     changed=restore_toml(backup,installed,p) if name=='.codex/config.toml' else restore_ini(backup,installed,p)
     conflicts.extend(name+': '+x for x in changed)
    except (ValueError,configparser.Error):conflicts.append(name)
   elif saved!=after.get(name):conflicts.append(name)
  b=json.loads((self.state/'gnome-before.json').read_text());a=json.loads((self.state/'gnome-after.json').read_text());now=gnome()
  for key,value in b.items():
   if value and value!=a.get(key) and now.get(key)==a.get(key):run(['gsettings','set','org.gnome.desktop.interface',key,value],check=False)
  for name in self.paths:
   p=self.home/name
   if p.is_dir() and not p.is_symlink():
    for d in sorted([x for x in p.rglob('*') if x.is_dir() and not x.is_symlink()],key=lambda x:len(x.parts),reverse=True):
     try:d.rmdir()
     except OSError:pass
  return conflicts
def validate_selection(selected,theme):
 selected=set(selected)
 if not selected:raise RuntimeError('Select at least one feature.')
 if not selected<=KNOWN:raise RuntimeError('Unknown feature: '+', '.join(sorted(selected-KNOWN)))
 if 'explorer_default' in selected and 'explorer' not in selected:raise RuntimeError('Select XP Explorer before making it the default file manager.')
 if theme not in THEMES:raise RuntimeError('Unknown XP theme.')
 return selected
def preflight(selected):
 if os.geteuid()==0:raise RuntimeError('Launch this installer as your desktop user, not as root.')
 for other in ['kubuntu-xp-suite']:
  if (Path.home()/'.local/share'/other/'installation.json').exists():raise RuntimeError('Remove the existing '+other+' installation before using the portable installer.')
 if selected&(NATIVE|{'fonts','icons','pointers','sounds','wallpaper','behavior','session','lock','login','boot'}) and (Path.home()/'.local/share/fedora-xp-installer/installation.json').exists():raise RuntimeError('The older Fedora XP installer manages these desktop features. Remove it before installing overlapping components; Explorer and Control Panel can be installed separately.')
 profile=detect()
 unsupported=capabilities(profile,FEATURES)
 errors=[name+': '+unsupported[name] for name in selected if unsupported[name]]
 if errors:raise RuntimeError('Unsupported selection: '+'; '.join(errors))
 if not profile['kde']:raise RuntimeError('Run it inside a KDE Plasma desktop session.')
 required=5 if selected&NATIVE else 2
 if shutil.disk_usage(Path.home()).free<required*1024**3:raise RuntimeError('At least '+str(required)+' GB of free space is required for the selected features.')
 if selected&(NATIVE|ACCESSORIES|{'login','boot','firefox','explorer','control_panel','tray','caption_animation','icons'}) and detect()['family'] and not shutil.which('pkexec'):raise RuntimeError('The desktop authentication tool pkexec is missing.')
 # Directly managed roots must be normal files/directories so the before/after
 # inventory can safely restore them; individual theme-resource links are fine.
 home=Path.home()
 managed=[home/'.local/share'/STATE_NAME]
 if selected&NATIVE or selected&{'behavior','session','lock','fonts','icons','pointers','sounds','wallpaper'}:
  managed += [home/p for p in USER_PATHS if p.startswith('.config/') or p.startswith('.local/share/plasma') or p.startswith('.local/lib/')]
 for feature,paths in {'fonts':['.local/share/fonts/xp'],'icons':['.local/share/icons'],'pointers':['.local/share/icons','.icons','.local/share/xplasma/cursor-sync.py','.config/systemd/user/xp-cursor-sync.path','.config/systemd/user/xp-cursor-sync.service'],'sounds':['.local/share/sounds','.local/share/knotifications6'],'wallpaper':['.local/share/xplasma']}.items():
  if feature in selected:managed.extend(home/path for path in paths)
 if selected&{'explorer','control_panel'}:managed.extend([home/'.local/share/xp-desktop-shell',home/'.config/mimeapps.list',home/'.config/kdeglobals'])
 if selected&ACCESSORIES:managed.extend([home/'.local/share/xp-accessories',home/'.config/menus/applications-merged',home/'.local/share/desktop-directories',home/'.local/share/konsole'])
 if 'tray' in selected:managed.extend([home/'.local/share/plasma/plasmoids',home/'.config/plasma-org.kde.plasma.desktop-appletsrc'])
 if 'firefox' in selected:managed.extend([home/'.local/share/firefox-esr-ie6',home/'.local/share/applications'])
 if selected&{'codex_colors','clippy'}:managed += [home/'.codex/config.toml',home/'.codex/pets/clippy']
 links=set()
 for target in managed:
  for path in [target,*target.parents]:
   if path==home:break
   if path.is_symlink():links.add(str(path))
 if links:raise RuntimeError('Managed configuration contains symbolic links; safe backup requires regular paths: '+', '.join(sorted(links)))
 if 'lock' in selected and (Path.home()/'.local/share/plasma/shells/org.kde.plasma.desktop').exists():raise RuntimeError('A custom Plasma shell is already present. Uncheck XP lock screen to preserve it.')
def verify():
 manifest=BASE/'SHA256SUMS.json'
 if not manifest.exists():raise RuntimeError('The package checksum manifest is missing.')
 for name,expected in json.loads(manifest.read_text()).items():
  p=BASE/name
  if isinstance(expected,dict) and (expected.get('kind')=='link' or 'symlink' in expected):
   if not p.is_symlink() or os.readlink(p)!=expected.get('target',expected.get('symlink')) or not p.resolve().is_relative_to(BASE.resolve()):
    raise RuntimeError('Package resource link is missing or damaged: '+name)
  elif not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest()!=expected:
   raise RuntimeError('Package file missing or damaged: '+name)
def root(action,selected=(),native_ready=False,caption_ready=False):
 args=['pkexec',sys.executable,BASE/'root_helper.py',action,'--features',','.join(sorted(selected))]
 if native_ready:args.append('--native-ready')
 if caption_ready:args.append('--caption-ready')
 run(args)
def desktop_path(home):
 p=Path(run(['xdg-user-dir','DESKTOP'],capture=True)) if shutil.which('xdg-user-dir') else home/'Desktop'
 try:p.relative_to(home)
 except ValueError:raise RuntimeError('The configured Desktop directory must be inside your home folder.')
 return p
def tracked_paths(home):
 paths=list(USER_PATHS)
 for name in ['My Computer.desktop','My Documents.desktop','My Network Places.desktop','Internet Explorer.desktop','Recycle Bin.desktop']:
  paths.append(str((desktop_path(home)/name).relative_to(home)))
 return paths
def prepare_wallpapers(home,options):
 if not options.get('hires_wallpapers'):return
 source=BASE/'payload/assets/hires-wallpapers'
 if not (source/'Bliss.jpg').is_file():raise RuntimeError('High-resolution wallpapers are missing from this installer.')
 copy(source,home/'.local/share/xplasma/assets/wallpapers')

def presets(home):
 base=home/'.local/share/xplasma/assets/themes';dest=base/'XP Presets';dest.mkdir(parents=True,exist_ok=True)
 for name,(style,colour) in THEMES.items():
  (dest/(name+'.theme')).write_text('[Theme]\nDisplayName='+name+'\n\n[VisualStyles]\nPath='+str(base/style)+'\nColorStyle='+colour+'\nSize=NormalSize\n\n[Control Panel\\Desktop]\nWallpaper='+str(home/'.local/share/xplasma/assets/wallpapers/Bliss.jpg')+'\nWallpaperStyle=2\nTileWallpaper=0\n')
 return dest
def look_args(cli,theme,selected):
 args=[str(cli),'apply',str(theme)]
 for feature,flags in {'desktop':['--no-colours','--no-decoration'],'fonts':['--no-fonts'],'apps':['--no-widgets'],
  'taskbar':['--no-taskbar'],'icons':['--no-icons'],'sounds':['--no-sounds'],'wallpaper':['--no-wallpaper'],'theme_manager':['--no-properties']}.items():
  if feature not in selected:args.extend(flags)
 return args
def prepare_native(home,state,selected,theme,asset_options=None):
 copy(BASE/'payload/assets/xplasma',home/'.local/share/xplasma')
 prepare_wallpapers(home,asset_options or {})
 path=home/'.local/share/xplasma/state.json';data=json.loads(path.read_text()) if path.exists() else {}
 data['media']=str(home/'.local/share/xplasma/extracted')
 data.setdefault('settings',{})['darkMode']=(asset_options or {}).get('dark_mode',False)
 data.setdefault('settings',{})['installerFeatures']={f['id']:f['id'] in selected for f in FEATURES}
 path.write_text(json.dumps(data,indent=2)+'\n')
 if 'fonts' in selected:copy(BASE/'payload/assets/fonts',home/'.local/share/fonts/xp');run(['fc-cache','-f'])
 build=state/'build'
 phase('Preparing desktop components — first installation may take several minutes')
 run([BASE/'build-native.sh',build,'taskbar' if 'taskbar' in selected else 'core'])
 copy(build/'runtime',state/'runtime')
 cli=state/'runtime/bin/xplasma';choice=presets(home)/(theme+'.theme')
 if selected&LOOK:phase('Applying selected desktop features');run(look_args(cli,choice,selected))
 # Apply explicitly after the desktop operation: its sound step is optional,
 # and optimized/repeated applies can skip it. Installation must report failures.
 if 'sounds' in selected:
  phase('Activating Windows XP sounds')
  run([cli,'sounds','Windows Default'])
 if 'pointers' in selected:run([cli,'pointers','Windows Default'])
 if 'fonts' in selected:
  if shutil.which('gsettings'):
   run(['gsettings','set','org.gnome.desktop.interface','font-name','Tahoma OS 8'],check=False)
   run(['gsettings','set','org.gnome.desktop.interface','monospace-font-name','Lucida Console 10'],check=False)
 if 'theme_manager' in selected:
  for p in (state/'runtime/share/applications').glob('*.desktop'):
   text=p.read_text().replace('Exec=xplasma-edit','Exec="'+str(state/'runtime/bin/xplasma-edit')+'"')
   dest=home/'.local/share/applications'/p.name;dest.parent.mkdir(parents=True,exist_ok=True);dest.write_text(text)
 if 'taskbar' in selected or 'desktop_icons' in selected:
  run([kdbus(),'org.kde.plasmashell','/PlasmaShell','org.kde.PlasmaShell.evaluateScript',
   'desktops().forEach(function(d){d.currentConfigGroup=["General"];d.writeConfig("iconSize",1);d.writeConfig("arrangement",1);d.writeConfig("labelWidth",0);d.writeConfig("textLines",2);d.reloadConfig();});'],check=False)
def config(file,group,key,value):
 run([detect()['writer'],'--file',file,'--group',group,'--key',key,str(value)])
def basic(home,selected,native=False,asset_options=None):
 if 'fonts' in selected:
  copy(BASE/'payload/assets/fonts',home/'.local/share/fonts/xp')
  from tahoma_os import generate
  generate(home/'.local/share/fonts/xp',home/'.local/share/fonts/xp/tahoma-os')
  font_rules=home/'.config/fontconfig/conf.d/99-xp-fonts.conf'
  font_rules.parent.mkdir(parents=True,exist_ok=True)
  font_rules.write_text('<?xml version="1.0"?><fontconfig><!-- XP interface fonts are selected explicitly; keep system generic defaults. --></fontconfig>\n')
  for key,value in {'font':'Tahoma OS,8,-1,5,400,0,0,0,0,0,0,0,0,0,0,1,,0,0','menuFont':'Tahoma OS,8,-1,5,400,0,0,0,0,0,0,0,0,0,0,1,,0,0','smallestReadableFont':'Tahoma OS,8,-1,5,400,0,0,0,0,0,0,0,0,0,0,1,,0,0','toolBarFont':'Tahoma OS,8,-1,5,400,0,0,0,0,0,0,0,0,0,0,1,,0,0','fixedFont':'Lucida Console,10,-1,5,400,0,0,0,0,0,0,0,0,0,0,1,,0,0'}.items():config('kdeglobals','General',key,value)
  if not native:
   for key in ['activeFont','inactiveFont']:config('kdeglobals','WM',key,'Trebuchet MS,10,-1,5,700,0,0,0,0,0,0,0,0,0,0,1,,0,0')
  if shutil.which('gsettings'):
   for key,value in [('font-name','Tahoma OS 8'),('monospace-font-name','Lucida Console 10')]:run(['gsettings','set','org.gnome.desktop.interface',key,value],check=False)
  run(['fc-cache','-f'])
 if 'icons' in selected and not native:
  copy(BASE/'payload/basic/icons',home/'.local/share/icons/xplasma-icons');config('kdeglobals','Icons','Theme','xplasma-icons')
 if 'pointers' in selected and not native:
  name='xplasma-pointers-936a2db6';copy(BASE/'payload/basic/pointers',home/'.local/share/icons'/name)
  config('kcminputrc','Mouse','cursorSize',32)
  run(['plasma-apply-cursortheme','--size','32',name])
 if 'sounds' in selected and not native:
  name='xplasma-sounds-055676e0';copy(BASE/'payload/basic/sounds',home/'.local/share/sounds'/name)
  config('kdeglobals','Sounds','Theme',name)
  for file,group,sound in [('plasma_workspace.notifyrc','startkde',None),('plasma_workspace.notifyrc','exitkde',None),('ksmserver.notifyrc','locked','service-logout'),('ksmserver.notifyrc','unlocked','service-login')]:
   config(file,'Event/'+group,'Action','Sound')
   if sound:config(file,'Event/'+group,'Sound',sound)
 if 'wallpaper' in selected and not native:
  dest=home/'.local/share/xplasma/assets/wallpapers/Bliss.jpg'
  source=BASE/('payload/assets/hires-wallpapers/Bliss.jpg' if (asset_options or {}).get('hires_wallpapers') else 'payload/assets/xplasma/assets/wallpapers/Bliss.jpg');copy(source,dest)
  script='desktops().forEach(function(d){d.wallpaperPlugin="org.kde.image";d.currentConfigGroup=["Wallpaper","org.kde.image","General"];d.writeConfig("Image",'+json.dumps(dest.as_uri())+');});'
  run([kdbus(),'org.kde.plasmashell','/PlasmaShell','org.kde.PlasmaShell.evaluateScript',script])
def accessories(home,selected,state):
 import importlib.util
 path=BASE/'payload/accessories/integration.py';spec=importlib.util.spec_from_file_location('xp_accessory_registration',path)
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.register(home,BASE/'payload/accessories',selected,copy)
 games=sorted(selected&{'pinball','minesweeper','solitaire'})
 if games:run([sys.executable,home/'.local/share/xp-accessories/build-games.py',*games,'--build-dir',state/'accessory-build'])
 if shutil.which('update-desktop-database'):run(['update-desktop-database',home/'.local/share/applications'],check=False)
 if shutil.which('kbuildsycoca6'):run(['kbuildsycoca6'],check=False)
def desktop_shell(home,selected):
 import importlib.util
 path=BASE/'payload/shell/integration.py';spec=importlib.util.spec_from_file_location('xp_shell_registration',path)
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.register(home,BASE/'payload/shell',selected,copy)
 if 'explorer' in selected:
  run([sys.executable,home/'.local/share/xp-desktop-shell/build-kio.py'])
 if shutil.which('update-desktop-database'):run(['update-desktop-database',home/'.local/share/applications'],check=False)
 # Preserve these per-applet flags when XPlasma reapplies another preset.
 script='panels().forEach(function(p){p.widgets().forEach(function(w){if(w.type==="fyi.hotsocket.xplasma.start"){w.currentConfigGroup=["General"];w.writeConfig("xpExplorerEnabled",'+str('explorer' in selected).lower()+');w.writeConfig("xpControlPanelEnabled",'+str('control_panel' in selected).lower()+');w.reloadConfig();}});});'
 run([kdbus(),'org.kde.plasmashell','/PlasmaShell','org.kde.PlasmaShell.evaluateScript',script],check=False)
def tray_widgets(home):
 import importlib.util
 path=BASE/'payload/tray/integration.py';spec=importlib.util.spec_from_file_location('xp_tray_registration',path)
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.register(home,BASE/'payload/tray',copy,run,kdbus())
def behavior(home,state):
 folder=state/'behavior-backup';folder.mkdir(exist_ok=True)
 for name in ['kwinrc','klaunchrc','plasma-org.kde.plasma.desktop-appletsrc']:
  p=home/'.config'/name
  if p.exists():copy(p,folder/name)
 (folder/'loaded-effects.txt').write_text(run([kdbus(),'org.kde.KWin','/Effects','org.kde.kwin.Effects.loadedEffects'],capture=True,check=False)+'\n')
 run([sys.executable,state/'runtime/tools/xp-behavior.py'])
def session(home,state):
 dest=home/'.local/share/plasma/look-and-feel/local.xp.session.desktop';copy(state/'templates/session',dest)
 run(['kwriteconfig6','--file','kdeglobals','--group','KDE','--key','LookAndFeelPackage','local.xp.session.desktop'])
 units=home/'.config/systemd/user';units.mkdir(parents=True,exist_ok=True)
 for name in ['xp-session-theme.path','xp-session-theme.service']:copy(state/'runtime/tools'/name,units/name)
 run(['systemctl','--user','daemon-reload']);run(['systemctl','--user','enable','--now','xp-session-theme.path'])
def taskbar_defaults(home,selected):
 # Seed the installer's Quick Launch once. Later theme changes retain edits.
 roots=[home/'.local/share/applications',Path('/usr/local/share/applications'),Path('/usr/share/applications'),
        Path('/var/lib/snapd/desktop/applications'),home/'.local/share/flatpak/exports/share/applications',Path('/var/lib/flatpak/exports/share/applications')]
 def entry(names):
  return next((p.as_uri() for name in names for root in roots if (p:=root/name).is_file()),None)
 settings=entry(['systemsettings.desktop','org.kde.systemsettings.desktop'])
 manager=entry(['org.kde.dolphin.desktop','dolphin.desktop'])
 browser=entry(['firefox-esr-ie6.desktop']) if 'firefox' in selected else None
 browser=browser or entry(['firefox.desktop','org.mozilla.firefox.desktop','firefox_firefox.desktop'])
 urls=[url for url in [settings,manager,browser] if url]
 if len(urls)!=3:print('A requested default taskbar application was not found; pinning the installed applications only.',flush=True)
 script='''panels().forEach(function(p){
 var quick=null;p.widgets().forEach(function(w){
  if(w.type==="org.kde.plasma.quicklaunch"&&!quick)quick=w;
  if(w.type==="org.kde.plasma.taskmanager"||w.type==="org.kde.plasma.icontasks"){
   w.currentConfigGroup=["General"];w.writeConfig("launchers",[]);w.reloadConfig();}
 });
 if(!quick)quick=p.addWidget("org.kde.plasma.quicklaunch");
 quick.currentConfigGroup=["General"];quick.writeConfig("launcherUrls",URLS);
 quick.writeConfig("maxSectionCount",1);quick.writeConfig("showLauncherNames",false);quick.writeConfig("enablePopup",false);quick.reloadConfig();
 var start=p.widgets().filter(function(w){return w.type==="fyi.hotsocket.xplasma.start";})[0];
 if(start)quick.index=start.index+1;
});'''.replace('URLS',json.dumps(urls))
 run([kdbus(),'org.kde.plasmashell','/PlasmaShell','org.kde.PlasmaShell.evaluateScript',script])

def desktop_updates(home,state,selected,asset_options=None):
 import importlib.util
 tools=state/'runtime/tools'
 if 'icons' in selected:
  with tempfile.TemporaryDirectory(prefix='xp-icons-') as temp:
   with tarfile.open(tools/'icons.tar.gz') as archive:archive.extractall(temp,filter='data')
   spec=importlib.util.spec_from_file_location('xp_icons',tools/'xp-icons.py')
   module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
   module.copy_layer(Path(temp)/'xplasma-xp-extended',home,hidpi=(asset_options or {}).get('hidpi_icons',False));module.repair(home)
 if 'behavior' in selected:
  copy(tools/'outline.qml',home/'.local/share/kwin/xplasma/outline.qml')
  run([sys.executable,tools/'xp-window-outlines.py'])
 if 'desktop' in selected:run([sys.executable,tools/'xp-decoration.py'])
 if selected&{'desktop','taskbar','behavior','session'}:
  units=home/'.config/systemd/user'
  for name in ['xp-session-theme.path','xp-session-theme.service']:copy(tools/name,units/name)
  run(['systemctl','--user','daemon-reload']);run(['systemctl','--user','enable','--now','xp-session-theme.path'])
 if 'pointers' in selected:
  helper=home/'.local/share/xplasma/cursor-sync.py';copy(tools/'cursor-sync.py',helper)
  if run([sys.executable,helper],check=False)==0:
   if detect()['systemd']:
    for name in ['xp-cursor-sync.path','xp-cursor-sync.service']:copy(tools/name,home/'.config/systemd/user'/name)
    run(['systemctl','--user','daemon-reload']);run(['systemctl','--user','enable','--now','xp-cursor-sync.path'])
  else:log('Existing shared cursor fallback preserved; no application override was installed.')
 if 'caption_animation' in selected:
  dest=home/'.local/share/kwin/xplasma/caption-source';copy(BASE/'payload/caption-animation',dest)
  import build_cache
  cached=build_cache.find_caption(BASE)
  if cached:
   copy(cached,home/'.local/lib/qt6/plugins/kwin/effects/plugins/xp-caption-animation.so')
   log('Using the verified caption animation for this KWin version.')
  else:
   run([sys.executable,dest/'build-local.py'])
   build_cache.store_component(home/'.local/lib/qt6/plugins/kwin/effects/plugins/xp-caption-animation.so',
     'lib/xplasma/caption/xp-caption-animation.so',build_cache.caption_component_identity(BASE))
  config('kwinrc','Plugins','xp-caption-animationEnabled','true')
  for effect in ['maximize','squash','magiclamp']:config('kwinrc','Plugins',effect+'Enabled','false')
  log('XP caption animation prepared for this computer. Log out/in to activate it.')

def welcome_splash(home):
 copy(BASE/'payload/templates/welcome-splash',home/'.local/share/plasma/look-and-feel/local.xp.welcome.desktop')
 config('ksplashrc','KSplash','Theme','local.xp.welcome.desktop')
 config('ksplashrc','KSplash','Engine','KSplashQML')

def user_pictures(home):
 copy(BASE/'payload/assets/user-pictures',home/'.local/share/plasma/avatars/windows-xp')
 log('XP user pictures added to System Settings → Users → Change avatar; current picture preserved.')

def lock_screen(home,state):
 src=Path('/usr/share/plasma/shells/org.kde.plasma.desktop');dest=home/'.local/share/plasma/shells/org.kde.plasma.desktop'
 dest.mkdir(parents=True);copy(src/'metadata.json',dest/'metadata.json');(dest/'contents').mkdir()
 for p in (src/'contents').iterdir():
  if p.name!='lockscreen':(dest/'contents'/p.name).symlink_to(p)
 copy(state/'templates/lockscreen',dest/'contents/lockscreen')
 copy(state/'templates/sddm/assets',dest/'contents/lockscreen/assets')
def firefox_profile_defaults(profile, seed=None):
 # user.js is reapplied on every launch: mutable toolbar choices belong in
 # prefs.js, seeded only for a new profile. Also migrate older payloads.
 user_js=profile/'user.js'
 lines=user_js.read_text().splitlines(keepends=True) if user_js.exists() else []
 keys=('browser.toolbars.bookmarks.visibility','browser.uiCustomization.state')
 initial=[line for line in lines if any('"'+key+'"' in line for key in keys)]
 prefs=profile/'prefs.js'
 if not prefs.exists():
  defaults=seed.read_text() if seed and seed.exists() else ''.join(initial)
  if defaults:prefs.write_text(defaults)
 if initial:user_js.write_text(''.join(line for line in lines if line not in initial))

def firefox(home):
 dest=home/'.local/share/firefox-esr-ie6';dest.mkdir(parents=True,exist_ok=True)
 with tarfile.open(BASE/('payload/firefox/firefox-linux-'+detect()['arch']+'.tar.xz')) as t:t.extractall(dest,filter='data')
 copy(BASE/'payload/firefox/runtime-overrides',dest/'firefox')
 profile=dest/'profile';profile.mkdir(parents=True,exist_ok=True)
 for child in (BASE/'payload/firefox/profile').iterdir():
  if child.name in {'prefs.js','initial-prefs.js'}:continue
  copy(child,profile/child.name)
 seed=BASE/'payload/firefox/profile/initial-prefs.js'
 firefox_profile_defaults(profile,seed if seed.exists() else BASE/'payload/firefox/profile/prefs.js')
 launcher=dest/'launch';launcher.write_text('#!/bin/sh\nexec "$HOME/.local/share/firefox-esr-ie6/firefox/firefox" --new-instance --profile "$HOME/.local/share/firefox-esr-ie6/profile" "$@"\n');launcher.chmod(0o755)
 entries=home/'.local/share/applications';entries.mkdir(parents=True,exist_ok=True)
 (entries/'firefox-esr-ie6.desktop').write_text('[Desktop Entry]\nType=Application\nName=IE6 Replica — Firefox ESR\nExec="'+str(launcher)+'" %U\nIcon=firefox\nTerminal=false\nCategories=Network;WebBrowser;\nStartupNotify=true\n')
 import importlib.util
 icon_helper=BASE/'payload/firefox/install-icon.py'
 spec=importlib.util.spec_from_file_location('ie6_install_icon',icon_helper)
 module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
 module.install(home=home,source=BASE/'payload/firefox')

def shortcuts(home,state,selected,asset_options=None):
 icons=home/'.local/share/icons/xp-desktop-shortcuts';copy(BASE/'payload/assets/desktop-icons',icons)
 if (asset_options or {}).get('hidpi_icons'):
  # Use the chosen bundle even when the system icon feature is deselected.
  with tempfile.TemporaryDirectory(prefix='xp-shortcut-icons-') as temp:
   with tarfile.open(BASE/'payload/tools/icons.tar.gz') as archive:archive.extractall(temp,filter='data')
   layer=Path(temp)/'xplasma-xp-extended'
   for filename,alias in {'computer':'computer','documents':'user-home','network':'network-workgroup','trash-full':'user-trash-full','trash-empty':'user-trash-empty','browser':'firefox'}.items():
    candidates=list(layer.glob('128x128/*/'+alias+'.png'))
    if candidates:copy(candidates[0].resolve(strict=True),icons/(filename+'.png'))
 desktop=desktop_path(home);desktop.mkdir(parents=True,exist_ok=True)
 documents=Path(run(['xdg-user-dir','DOCUMENTS'],capture=True)) if shutil.which('xdg-user-dir') else home/'Documents'
 targets={'My Computer':('computer.png','file:///'),'My Documents':('documents.png',documents.as_uri()),
  'My Network Places':('network.png','remote:/'),'Recycle Bin':('trash-full.png','trash:/')}
 for name,(icon,url) in targets.items():
  p=desktop/(name+'.desktop')
  if p.exists():
   # Repair our legacy launcher after an old installer's private state was
   # removed. Preserve custom launchers and all unrelated entry settings.
   text=p.read_text()
   legacy=str(state/'desktop-icons')+'/'
   if 'Icon='+legacy in text:
    text=re.sub(r'^Icon=.*$', 'Icon='+str(icons/icon), text, flags=re.MULTILINE)
    if name=='Recycle Bin':text=re.sub(r'^EmptyIcon=.*$', 'EmptyIcon='+str(icons/'trash-empty.png'), text, flags=re.MULTILINE)
    p.write_text(text);p.chmod(0o755)
   else:log('Kept existing desktop shortcut: '+p.name)
   continue
  text='[Desktop Entry]\nType=Link\nName='+name+'\nIcon='+str(icons/icon)+'\nURL='+url+'\n'
  if name=='Recycle Bin':text+='EmptyIcon='+str(icons/'trash-empty.png')+'\n'
  p.write_text(text);p.chmod(0o755)
 p=desktop/'Internet Explorer.desktop'
 if p.exists():
  text=p.read_text()
  if 'Icon='+str(state/'desktop-icons')+'/' in text:
   p.write_text(re.sub(r'^Icon=.*$', 'Icon='+str(icons/'browser.png'), text, flags=re.MULTILINE));p.chmod(0o755)
 else:
  command='"'+str(home/'.local/share/firefox-esr-ie6/launch')+'" %U' if 'firefox' in selected else 'xdg-open https://www.google.com/'
  p.write_text('[Desktop Entry]\nType=Application\nName=Internet Explorer\nComment=Modern browser with an XP-era desktop icon\nIcon='+str(icons/'browser.png')+'\nExec='+command+'\nTerminal=false\nStartupNotify=true\n');p.chmod(0o755)
def codex(home,selected):
 folder=home/'.codex';folder.mkdir(exist_ok=True);path=folder/'config.toml'
 doc=tomlkit.parse(path.read_text()) if path.exists() else tomlkit.document()
 if 'desktop' not in doc:doc['desktop']=tomlkit.table()
 desk=doc['desktop']
 if 'codex_colors' in selected:
  desk['appearanceTheme']='light';desk['appearanceLightChromeTheme']={
   'accent':'#0054e3','accentSource':'custom','surface':'#ece9d8','ink':'#000000','contrast':45,'opaqueWindows':True,
   'fonts':{'ui':'Tahoma','content':'Tahoma','code':'Lucida Console'},
   'semanticColors':{'diffAdded':'#008000','diffRemoved':'#ba2623','skill':'#924ff7'}}
 if 'clippy' in selected:
  dest=folder/'pets/clippy'
  if dest.exists():raise RuntimeError('A Clippy pet already exists; uncheck Clippy to preserve it.')
  copy(BASE/'payload/codex/clippy',dest)
  desk['selected-avatar-id']='custom:clippy';desk['avatar-overlay-pet-visible']=True;desk['pet-visibility']='always-visible'
 path.write_text(tomlkit.dumps(doc))
def update_client(home):
 import importlib.util
 source=BASE/'payload/updater'
 # The bootstrap's core module is local to its standalone payload.
 sys.path.insert(0,str(source))
 try:
  spec=importlib.util.spec_from_file_location('xp_update_bootstrap',source/'install.py')
  module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
  if (BASE/'edition.json').is_file():
   module.register(home,source,require_existing=False,switch_feed=True)
  else:
   module.register(home,source,require_existing=False)
 finally:sys.path.remove(str(source))

def install(selected,theme,asset_options=None):
 asset_options=asset_options or {}
 selected=validate_selection(selected,theme);phase('Checking system and package');preflight(selected);verify()
 home=Path.home();state=home/'.local/share'/STATE_NAME
 if (state/'installation.json').exists():raise RuntimeError('An XP Suite installation already exists. Use Remove installed features before choosing a new selection.')
 phase('Preparing Linux dependencies — authentication may be requested')
 if detect()['family'] and selected&(NATIVE|ACCESSORIES|{'login','boot','firefox','explorer','control_panel','tray','caption_animation','icons'}):
  import build_cache
  cached=build_cache.find(BASE,'taskbar' if 'taskbar' in selected else 'core') if selected&NATIVE else None
  ready=bool(cached and build_cache.usable(cached))
  if ready:log('A verified prebuilt package matches this distribution, architecture, Qt and Plasma version.')
  caption_was_ready=bool(build_cache.find_caption(BASE))
  root('dependencies',selected,native_ready=ready,caption_ready=caption_was_ready)
  if ready or caption_was_ready:
   after=build_cache.find(BASE,'taskbar' if 'taskbar' in selected else 'core')
   still_ready=bool(after and build_cache.usable(after))
   caption_ok=bool(build_cache.find_caption(BASE))
   if (ready and not still_ready) or ('caption_animation' in selected and caption_was_ready and not caption_ok):
    log('System libraries changed while installing packages; refreshing matching build dependencies.')
    root('dependencies',selected,native_ready=still_ready,caption_ready=caption_ok)
 state.mkdir(parents=True,exist_ok=True)
 snapshot=Snapshot(home,state,tracked_paths(home));snapshot.save()
 try:
  phase('Preparing bundled resources')
  copy(BASE/'payload/templates',state/'templates');copy(BASE/'payload/tools',state/'runtime/tools')
  # Keep the installer bundle so recovery and future reinstall do not need
  # the original extraction folder (or an XP ISO).
  bundle=state/'installer'
  if BASE.resolve()!=bundle.resolve():copy(BASE,bundle)
  apps=home/'.local/share/applications';apps.mkdir(parents=True,exist_ok=True)
  (apps/'kde-xp-suite.desktop').write_text('[Desktop Entry]\nType=Application\nName=Windows XP Theme Installer\nExec=python3 "'+str(bundle/'gui.py')+'"\nIcon=preferences-desktop-theme\nTerminal=false\nCategories=Settings;DesktopSettings;\n')
  (state/'platform.json').write_text(json.dumps(detect(),indent=2)+'\n')
  if selected&NATIVE:prepare_native(home,state,selected,theme,asset_options)
  if not selected&NATIVE and 'wallpaper' in selected:prepare_wallpapers(home,asset_options)
  basic(home,selected,native=bool(selected&NATIVE),asset_options=asset_options)
  if selected&ACCESSORIES:phase('Installing selected XP accessories and games');accessories(home,selected,state)
  if selected&{'explorer','control_panel'}:phase('Installing XP Explorer and Control Panel');desktop_shell(home,selected)
  if 'tray' in selected:phase('Installing XP system tray panels');tray_widgets(home)
  if 'behavior' in selected:phase('Applying window and alert behavior');behavior(home,state)
  if 'session' in selected:phase('Installing persistent XP session menus');session(home,state)
  desktop_updates(home,state,selected,asset_options)
  if 'user_pictures' in selected:phase('Installing XP user account pictures');user_pictures(home)
  if 'lock' in selected:phase('Installing XP lock screen');lock_screen(home,state)
  if 'firefox' in selected:phase('Installing IE6-style Firefox ESR');firefox(home)
  if 'taskbar' in selected:phase('Setting default taskbar shortcuts');taskbar_defaults(home,selected)
  if 'desktop_icons' in selected:phase('Adding XP desktop shortcuts');shortcuts(home,state,selected,asset_options)
  if selected&{'codex_colors','clippy'}:phase('Configuring Codex extras');codex(home,selected)
  if 'updates' in selected:phase('Installing signed XPlasma update client');update_client(home)
  if selected&{'login','boot'}:
   run(['kwriteconfig6','--file','ksplashrc','--group','KSplash','--key','Engine','none'])
   phase('Installing selected login and boot screens');root('screens',selected)
  if 'welcome_splash' in selected:phase('Installing XP Welcome desktop splash');welcome_splash(home)
  snapshot.finish();(state/'installation.json').write_text(json.dumps({'version':4,'edition':json.loads((BASE/'edition.json').read_text()) if (BASE/'edition.json').is_file() else None,'features':sorted(selected),'theme':theme,'asset_options':asset_options},indent=2)+'\n')
  phase('Installed successfully')
  log('Reopen applications to refresh their themes. No logout or reboot was performed.')
  if 'login' in selected or 'boot' in selected:log('Login and boot screens apply at the next login/reboot. Test the lock screen first.')
  if selected&{'codex_colors','clippy'}:log('Restart Codex; use /pet if Clippy is hidden.')
 except Exception:
  log('Installation failed. Restoring managed settings…')
  run(['systemctl','--user','disable','--now','xp-session-theme.path'],check=False)
  if 'pointers' in selected:run(['systemctl','--user','disable','--now','xp-cursor-sync.path'],check=False)
  if selected&{'login','boot'}:
   try:root('restore')
   except Exception as e:log('System recovery needs retry: '+str(e))
  snapshot.finish();conflicts=snapshot.restore();refresh(home,state)
  (state/'incomplete.json').write_text(json.dumps({'features':sorted(selected),'conflicts':conflicts},indent=2)+'\n')
  raise
 return 0
def refresh(home,state):
 run(['fc-cache','-f'],check=False);run([kdbus(),'org.kde.KWin','/KWin','reconfigure'],check=False)
 run(['systemctl','--user','daemon-reload'],check=False)
 old=state/'session-unit-before.json'
 if old.exists() and json.loads(old.read_text()).get('enabled'):run(['systemctl','--user','enable','--now','xp-session-theme.path'],check=False)
 cursor=state/'cursor-unit-before.json'
 if cursor.exists() and json.loads(cursor.read_text()).get('enabled'):run(['systemctl','--user','enable','--now','xp-cursor-sync.path'],check=False)
 run(['systemctl','--user','restart','plasma-plasmashell.service'],check=False)
def uninstall():
 home=Path.home();state=home/'.local/share'/STATE_NAME
 record=state/'installation.json'
 if not record.exists():record=state/'incomplete.json'
 if not record.exists():raise RuntimeError('There is no managed installation to remove.')
 info=json.loads(record.read_text());phase('Restoring your saved settings')
 run(['systemctl','--user','disable','--now','xp-session-theme.path'],check=False)
 if 'pointers' in set(info['features']):run(['systemctl','--user','disable','--now','xp-cursor-sync.path'],check=False)
 if set(info['features'])&{'login','boot'}:root('restore')
 paths=json.loads((state/'tracked.json').read_text());conflicts=Snapshot(home,state,paths).restore();refresh(home,state)
 record.rename(state/'removed.json')
 phase('Selected features removed')
 if conflicts:log('Later edits were retained: '+'; '.join(conflicts))
 log('Managed settings restored where they still matched the installed values. Linux dependencies and backups are retained. Reopen applications.')
 return 0
def main():
 p=argparse.ArgumentParser();p.add_argument('action',choices=['install','uninstall','check']);p.add_argument('--features',default='');p.add_argument('--theme',default='Luna Blue');p.add_argument('--hidpi-icons',action='store_true');p.add_argument('--hires-wallpapers',action='store_true');p.add_argument('--dark-mode',action='store_true');a=p.parse_args()
 selected=validate_selection([x for x in a.features.split(',') if x],a.theme) if a.action!='uninstall' else set()
 if a.action=='install':return install(selected,a.theme,{'hidpi_icons':a.hidpi_icons,'hires_wallpapers':a.hires_wallpapers,'dark_mode':a.dark_mode})
 if a.action=='uninstall':return uninstall()
 preflight(selected);verify();log('Preflight passed; no settings changed.');return 0
if __name__=='__main__':
 try:sys.exit(main())
 except Exception as e:log('ERROR: '+str(e));traceback.print_exc();sys.exit(1)
