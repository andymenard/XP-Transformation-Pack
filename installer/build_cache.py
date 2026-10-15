#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verified, platform-specific native bundles and reusable local build outputs."""
import argparse,hashlib,json,os,re,shutil,subprocess,tarfile,tempfile
from pathlib import Path,PurePosixPath
from system_profile import detect

def default_cache():return Path(os.environ.get('XP_BUILD_CACHE',str(Path.home()/'.cache/xplasma-plus/prebuilt')))

def source_id(suite):
 # Ignore archive timestamps: a CI checkout and a release export must match.
 digest=hashlib.sha256()
 with tarfile.open(Path(suite)/'source/xplasma-source.tar.gz') as archive:
  members=sorted((m for m in archive if m.isfile()),key=lambda m:m.name)
  for member in members:
   name=str(PurePosixPath(member.name)).removeprefix('xplasma/')
   digest.update(name.encode()+b'\0'+hashlib.sha256(archive.extractfile(member).read()).digest())
 return digest.hexdigest()

def identity(suite,kind,profile=None):
 profile=profile or detect();release={}
 for line in Path('/etc/os-release').read_text().splitlines():
  if '=' in line:
   key,value=line.split('=',1);release[key]=value.strip('"')
 value={'schema':1,'source':source_id(suite),'kind':kind,'distribution':release.get('ID'),
        'release':release.get('VERSION_ID'),'arch':profile['arch'],
        'qt':list(profile['qt'][:2]),'plasma':list(profile['plasma'][:2])}
 if kind=='taskbar':
  series='.'.join(map(str,profile['plasma'][:2]))
  value['plasma_sources']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((Path(suite)/'source').glob('plasma-*-'+series+'.*.tar.xz'))}
 return value

def key(value):return hashlib.sha256(json.dumps(value,sort_keys=True).encode()).hexdigest()[:24]

def inventory(runtime):
 result={};runtime=Path(runtime).resolve()
 for path in sorted(runtime.rglob('*')):
  relative=str(path.relative_to(runtime))
  if path.is_symlink():
   if not path.resolve().is_relative_to(runtime):raise RuntimeError('External runtime link: '+relative)
   result[relative]={'link':os.readlink(path)}
  elif path.is_file():result[relative]=hashlib.sha256(path.read_bytes()).hexdigest()
 return result

def validate(folder,expected):
 folder=Path(folder);manifest=folder/'manifest.json';runtime=folder/'runtime'
 if not manifest.is_file() or manifest.is_symlink() or not runtime.is_dir() or runtime.is_symlink():return False
 try:
  data=json.loads(manifest.read_text())
  if data['identity']!=expected or data['files']!=inventory(runtime):return False
  required=['bin/xplasma','bin/xplasma-edit','bin/xplasma-setup','lib/xplasma/qt6/styles/libxplasma.so','lib/xplasma/start-menu/libxpmenuguard.so']
  if any(not (runtime/name).is_file() for name in required):return False
  machine={'amd64':62,'arm64':183}.get(expected['arch'])
  for name in required:
   with (runtime/name).open('rb') as file:header=file.read(20)
   if header[:4]!=b'\x7fELF' or len(header)<20 or int.from_bytes(header[18:20],'little' if header[5]==1 else 'big')!=machine:return False
   if name.startswith('bin/') and not os.access(runtime/name,os.X_OK):return False
  if expected['kind']=='taskbar':
   series='.'.join(map(str,expected['plasma']))
   if len(list((runtime/'lib/xplasma/forks'/series/'plasma/applets').glob('*.so')))!=4:return False
  return True
 except (OSError,ValueError,KeyError,RuntimeError):return False

def find(suite,kind,profile=None,cache=None):
 if os.environ.get('XP_BUILD_FROM_SOURCE')=='1':return None
 expected=identity(suite,kind,profile)
 roots=[Path(suite)/'prebuilt',Path(cache) if cache else default_cache()]
 matches=[]
 for root in roots:
  folder=root/key(expected)
  if validate(folder,expected):matches.append(folder)
 if not matches and kind=='core':return find(suite,'taskbar',profile,cache)
 # A newly rebuilt local effect can match a KWin update better than the copy
 # in the original installer, while the native core remains compatible.
 return next((folder for folder in matches if caption_ready(suite,folder)),matches[0] if matches else None)

def caption_identity(suite):
 binary=shutil.which('kwin_wayland') or shutil.which('kwin_x11')
 if not binary:return None
 # File capabilities can prevent executing KWin in a build container. Query
 # its owning package first; include the vendor revision in the ABI identity.
 package='';match=None
 for tool,args in [('dpkg-query',['-W','-f=${Version}','kwin-wayland']),('rpm',['-qf',binary,'--qf','%{VERSION}-%{RELEASE}']),('pacman',['-Q','kwin'])]:
  command=shutil.which(tool)
  if not command:continue
  try:result=subprocess.run([command,*args],capture_output=True,text=True,timeout=10)
  except (OSError,subprocess.TimeoutExpired):continue
  if result.returncode==0:
   match=re.search(r'6\.\d+\.\d+',result.stdout)
   if match:package=result.stdout.strip();break
 if not match:
  try:result=subprocess.run([binary,'--version'],capture_output=True,text=True,timeout=10,env={**os.environ,'QT_QPA_PLATFORM':'offscreen'})
  except (OSError,subprocess.TimeoutExpired):return None
  match=re.search(r'6\.\d+\.\d+',result.stdout)
 source=Path(suite)/'payload/caption-animation'
 names=['CMakeLists.txt','effect.cpp','caption.h','metadata.json','test-caption.cpp','demo.cpp']
 if not match or any(not (source/name).is_file() for name in names):return None
 return {'kwin':match.group(),'package':package or match.group(),'source':{name:hashlib.sha256((source/name).read_bytes()).hexdigest() for name in names}}

def caption_ready(suite,folder):
 if not folder:return False
 data=json.loads((Path(folder)/'manifest.json').read_text())
 return bool(data.get('caption') and data['caption']==caption_identity(suite)
             and (Path(folder)/'runtime/lib/xplasma/caption/xp-caption-animation.so').is_file())

def usable(folder):
 for binary in [*(Path(folder)/'runtime/bin').glob('*'),*(Path(folder)/'runtime/lib').rglob('*.so')]:
  if not binary.is_file() or binary.read_bytes()[:4]!=b'\x7fELF':continue
  probe=subprocess.run(['ldd','-r',str(binary)],capture_output=True,text=True)
  if probe.returncode or any(text in probe.stdout+probe.stderr for text in ['not found','undefined symbol:']):return False
 return True

def store(suite,build,kind,destination=None,profile=None,caption=None):
 expected=identity(suite,kind,profile);root=Path(destination) if destination else default_cache()
 root.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='.prepare-',dir=root) as temp:
  folder=Path(temp);shutil.copytree(Path(build)/'runtime',folder/'runtime',symlinks=True)
  metadata={'identity':expected}
  if caption:
   metadata['caption']=caption_identity(suite)
   if not metadata['caption']:raise RuntimeError('Cannot identify the caption effect ABI.')
   target=folder/'runtime/lib/xplasma/caption/xp-caption-animation.so';target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(caption,target)
  metadata['files']=inventory(folder/'runtime')
  (folder/'manifest.json').write_text(json.dumps(metadata,indent=2)+'\n')
  if not validate(folder,expected):raise RuntimeError('Incomplete native runtime; prebuilt package was not saved.')
  target=root/key(expected)
  if target.exists():shutil.rmtree(target)
  folder.rename(target)
 if caption:store_component(caption,'lib/xplasma/caption/xp-caption-animation.so',caption_component_identity(suite,profile),root)
 records=Path(build)/'widget-components.json'
 if kind=='taskbar' and records.exists():
  for record in json.loads(records.read_text()):
   store_component(Path(build)/'runtime'/record['path'],record['path'],record['identity'],root)
 return target

def reuse(suite,build,kind):
 profile=detect();folder=find(suite,kind,profile)
 if not folder and kind=='taskbar':folder=find(suite,'core',profile)
 if not folder:return False
 # Check shared-library availability before accepting a binary built elsewhere.
 if not usable(folder):return False
 build=Path(build);runtime=build/'runtime'
 if runtime.exists():shutil.rmtree(runtime)
 shutil.copytree(folder/'runtime',runtime,symlinks=True)
 print('Using verified prebuilt native components.',flush=True)
 if kind=='taskbar' and json.loads((folder/'manifest.json').read_text())['identity']['kind']!='taskbar':
  subprocess.run([os.sys.executable,str(Path(suite)/'build-widgets.py'),str(build),str(jobs())],check=True)
  store(suite,build,kind)
 return True


def component_platform(profile=None):
 value=identity_platform(profile)
 return {'schema':2,**value}

def identity_platform(profile=None):
 profile=profile or detect();release={}
 for line in Path('/etc/os-release').read_text().splitlines():
  if '=' in line:
   name,value=line.split('=',1);release[name]=value.strip('"')
 return {'distribution':release.get('ID'),'release':release.get('VERSION_ID'),
         'arch':profile['arch'],'qt':list(profile['qt'][:2]),'plasma':list(profile['plasma'][:2])}

def widget_identity(project,widget,profile=None):
 # Hash the patched widget and all shared project inputs. Other applets and
 # containments cannot affect this target; common libraries/CMake files can.
 digest=hashlib.sha256();project=Path(project);widget=PurePosixPath(widget)
 for path in sorted(project.rglob('*')):
  relative=path.relative_to(project)
  if relative.parts[0] in {'applets','containments'} and not PurePosixPath(relative).is_relative_to(widget):continue
  if path.is_symlink():
   if not path.resolve().is_relative_to(project.resolve()):raise RuntimeError('External widget source link: '+str(relative))
  if path.is_file():digest.update(str(relative).encode()+b'\0'+hashlib.sha256(path.read_bytes()).digest())
 # Changes to the build recipe must also invalidate cached output.
 digest.update((Path(__file__).parent/'build-widgets.py').read_bytes())
 return {**component_platform(profile),'component':'widget','widget':str(widget),'source':digest.hexdigest()}

def caption_component_identity(suite,profile=None):
 abi=caption_identity(suite)
 return {**component_platform(profile),'component':'caption','abi':abi} if abi else None

def validate_component(folder,expected):
 folder=Path(folder)
 try:
  if folder.is_symlink() or (folder/'manifest.json').is_symlink() or (folder/'runtime').is_symlink():return False
  data=json.loads((folder/'manifest.json').read_text())
  if data['identity']!=expected or data['files']!=inventory(folder/'runtime') or not data['files']:return False
  machine={'amd64':62,'arm64':183}.get(expected['arch'])
  for name in data['files']:
   path=folder/'runtime'/name
   if path.suffix!='.so' or path.is_symlink():return False
   header=path.read_bytes()[:20]
   if len(header)<20 or header[:6]!=b'\x7fELF\x02\x01' or int.from_bytes(header[18:20],'little')!=machine:return False
  return len(data['files'])==1
 except (OSError,KeyError,ValueError,RuntimeError):return False

def find_component(suite,expected,cache=None,check_libraries=True):
 if not expected or os.environ.get('XP_BUILD_FROM_SOURCE')=='1':return None
 for root in [Path(suite)/'prebuilt',Path(cache) if cache else default_cache()]:
  folder=root/'components'/key(expected)
  if validate_component(folder,expected) and (not check_libraries or usable(folder)):return folder
 return None

def store_component(binary,relative,expected,destination=None):
 root=(Path(destination) if destination else default_cache())/'components'
 root.mkdir(parents=True,exist_ok=True)
 if Path(relative).is_absolute() or '..' in Path(relative).parts:raise ValueError('Invalid component destination')
 with tempfile.TemporaryDirectory(prefix='.prepare-',dir=root) as temporary:
  folder=Path(temporary);target=folder/'runtime'/relative;target.parent.mkdir(parents=True);shutil.copy2(binary,target)
  (folder/'manifest.json').write_text(json.dumps({'identity':expected,'files':inventory(folder/'runtime')},indent=2)+'\n')
  if not validate_component(folder,expected):raise RuntimeError('Invalid component output')
  destination=root/key(expected)
  if destination.exists():shutil.rmtree(destination)
  folder.rename(destination)
 return destination

def find_caption(suite,profile=None,cache=None):
 # The effect remains reusable when engine or widget sources change.
 expected=caption_component_identity(suite,profile)
 folder=find_component(suite,expected,cache)
 if folder:return folder/'runtime/lib/xplasma/caption/xp-caption-animation.so'
 # Legacy full packages remain usable as a source for just the matching effect.
 if not expected or os.environ.get('XP_BUILD_FROM_SOURCE')=='1':return None
 for root in [Path(suite)/'prebuilt',Path(cache) if cache else default_cache()]:
  for folder in sorted(root.glob('*')):
   try:
    data=json.loads((folder/'manifest.json').read_text());ident=data['identity']
    if any(ident.get(k)!=v for k,v in identity_platform(profile).items()):continue
    if data.get('caption')!=expected['abi'] or not validate(folder,ident):continue
    binary=folder/'runtime/lib/xplasma/caption/xp-caption-animation.so'
    if binary.is_file():
     probe=subprocess.run(['ldd','-r',str(binary)],capture_output=True,text=True)
     if not probe.returncode and not any(v in probe.stdout+probe.stderr for v in ['not found','undefined symbol:']):return binary
   except (OSError,KeyError,ValueError):continue
 return None


def jobs():
 if os.environ.get('XP_BUILD_JOBS'):return max(1,int(os.environ['XP_BUILD_JOBS']))
 available=1024**3
 for line in Path('/proc/meminfo').read_text().splitlines():
  if line.startswith('MemAvailable:'):available=int(line.split()[1])*1024
 return max(1,min(os.cpu_count() or 2,8,available//(900*1024**2)))

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('action',choices=['reuse','store','jobs']);p.add_argument('suite',nargs='?');p.add_argument('build',nargs='?');p.add_argument('kind',choices=['core','taskbar'],nargs='?');p.add_argument('--destination');p.add_argument('--caption');a=p.parse_args()
 if a.action=='jobs':print(jobs())
 elif a.action=='reuse':raise SystemExit(0 if reuse(a.suite,a.build,a.kind) else 1)
 else:print(store(a.suite,a.build,a.kind,a.destination,caption=a.caption))
