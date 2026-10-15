# SPDX-License-Identifier: GPL-3.0-or-later
"""Signed, per-feature user-file updates with an independent rollback journal."""
import contextlib,fcntl,hashlib,json,os,platform,re,shutil,signal,subprocess,sys,tarfile,tempfile,time
import urllib.request,urllib.error,urllib.parse
from pathlib import Path,PurePosixPath
BASE=Path(__file__).resolve().parent
STATE_NAMES=('kde-xp-suite','kubuntu-xp-suite','xp-accessory-suite','fedora-xp-installer')
ALLOWED=('.local/share/firefox-esr-ie6/profile/chrome/','.local/share/xplasma-updater/app/','.local/share/xp-accessories/','.local/share/xp-desktop-shell/','.local/share/applications/fyi.hotsocket.xp.','.local/share/plasma/','.local/share/aurorae/','.local/share/kwin/','.local/lib/qt6/plugins/')
EXACT_ALLOWED={'.local/share/firefox-esr-ie6/firefox/ie6-ui.cfg','.config/systemd/user/xp-session-theme.path','.config/systemd/user/xp-session-theme.service'}
class UpdateError(RuntimeError):pass

def version(v):
 if not isinstance(v,str) or len(v)>64:raise UpdateError('Invalid release version.')
 match=re.fullmatch(r'(\d+)\.(\d+)\.(\d+)(?:-(alpha|beta|rc)\.(\d+))?',v)
 if not match:raise UpdateError('Invalid release version.')
 return (*map(int,match.group(1,2,3)),{'alpha':0,'beta':1,'rc':2,None:3}[match.group(4)],int(match.group(5) or 0))

def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def atomic(path,data,mode=0o644):
 path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
 fd,name=tempfile.mkstemp(prefix='.xp-update-',dir=path.parent)
 try:
  with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
  os.chmod(name,mode);os.replace(name,path)
 finally:
  if Path(name).exists():Path(name).unlink()
def write_json(p,v):atomic(p,(json.dumps(v,indent=2)+'\n').encode(),0o600)
def find_installation(home):
 home=Path(home)
 for name in sorted(STATE_NAMES,key=lambda n:not (home/'.local/share'/n/'runtime/tools/keep-xp-session.py').is_file()):
  state=home/'.local/share'/name;p=state/'installation.json'
  if p.is_file():
   info=json.loads(p.read_text());features=set(info.get('features',info.get('apps',[])))
   if not features and name=='fedora-xp-installer':
    cfg=home/'.local/share/xplasma/state.json'
    if cfg.exists():features={k for k,v in json.loads(cfg.read_text()).get('settings',{}).get('installerFeatures',{}).items() if v}
   return state,info,features
 raise UpdateError('No existing XP installer record found. Install the transformation pack first.')
def target(home,state,name):
 if name.startswith('@state/'):
  suffix=name[7:]
  if not suffix.startswith(('runtime/','templates/','installer/')):raise UpdateError('Invalid installer target.')
  name=str(state.relative_to(home))+'/'+suffix
 elif name not in EXACT_ALLOWED and not name.startswith(ALLOWED):raise UpdateError('This update target is outside managed XP application files: '+name)
 p=PurePosixPath(name)
 if p.is_absolute() or '..' in p.parts or '\\' in name:raise UpdateError('Unsafe update path.')
 dest=Path(home).joinpath(*p.parts)
 for parent in [dest,*dest.parents]:
  if parent==Path(home).parent:break
  if parent.is_symlink():raise UpdateError('Refusing symbolic-link update target: '+name)
 if not dest.resolve().is_relative_to(Path(home).resolve()):raise UpdateError('Update escapes the home directory.')
 return dest

def verify_release(folder,key=BASE/'release-key.pem'):
 folder=Path(folder);manifest=folder/'release.json';signature=folder/'release.sig'
 if manifest.stat().st_size>2*1024*1024:raise UpdateError('Release manifest too large.')
 r=subprocess.run(['openssl','dgst','-sha256','-verify',str(key),'-signature',str(signature),str(manifest)],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
 if r.returncode:raise UpdateError('Release signature does not match the trusted publisher key.')
 data=json.loads(manifest.read_text());version(data['version'])
 if data.get('schema')!=1:raise UpdateError('Unsupported update format.')
 archive=folder/'payload.tar.gz'
 if archive.stat().st_size!=data['archive_size'] or digest(archive)!=data['archive_sha256']:raise UpdateError('Downloaded update is damaged.')
 if len(data.get('files',[]))>20000:raise UpdateError('Too many update files.')
 expected={}
 for item in data['files']:
  name=item['member']
  if name in expected or not re.fullmatch(r'files/\d+',name):raise UpdateError('Invalid or duplicate payload member.')
  if not re.fullmatch(r'[a-z][a-z0-9_]*',item['feature']):raise UpdateError('Invalid feature name.')
  if not re.fullmatch(r'[0-9a-f]{64}',item['sha256']):raise UpdateError('Invalid file digest.')
  if not 0<=item['size']<=128*1024*1024:raise UpdateError('Invalid file size.')
  if item.get('mode',0o644) not in (0o644,0o755):raise UpdateError('Invalid file permissions.')
  expected[name]=item
 with tarfile.open(archive,'r:gz') as tar:
  seen=set();total=0
  for member in tar:
   if not member.isfile() or member.name not in expected or member.name in seen:raise UpdateError('Unexpected archive entry.')
   item=expected[member.name];total+=member.size
   if member.size!=item['size'] or total>512*1024*1024:raise UpdateError('Archive size mismatch.')
   content=tar.extractfile(member).read()
   if hashlib.sha256(content).hexdigest()!=item['sha256']:raise UpdateError('Payload file checksum mismatch.')
   if content[:4]==b'\x7fELF':
    machine=int.from_bytes(content[18:20],'little' if content[5]==1 else 'big')
    expected_arch={62:'amd64',183:'arm64'}.get(machine)
    if not expected_arch or item.get('arch')!=expected_arch:raise UpdateError('Native binary architecture metadata is missing or incorrect.')
    if '/qt6/plugins/' in item['target'] and not item.get('qt'):raise UpdateError('Qt plugin must declare its exact Qt minor version.')
   seen.add(member.name)
  if seen!=set(expected):raise UpdateError('Incomplete update payload.')
 setup_descriptor(data)
 # Decode only authenticated, bounded text-patch/template payloads.
 with tarfile.open(archive,'r:gz') as tar:
  for item in data['files']:
   operation=item.get('operation','replace')
   if operation not in ('replace','text_patch','state_template'):raise UpdateError('Unsupported update operation.')
   if operation=='text_patch':
    if item['target'] not in ('.local/share/plasma/plasmoids/org.kde.desktopcontainment/contents/ui/FolderView.qml','.local/share/plasma/plasmoids/org.kde.desktopcontainment/contents/ui/FolderItemDelegate.qml'):raise UpdateError('Unsupported patch target.')
    rules=json.loads(tar.extractfile(item['member']).read())
    if not isinstance(rules,list) or not 1<=len(rules)<=20:raise UpdateError('Invalid patch rules.')
    if any(not isinstance(r,dict) or not isinstance(r.get('find'),str) or not r['find'] or not isinstance(r.get('replace'),str) for r in rules):raise UpdateError('Invalid text patch.')
    item['_rules']=rules
   elif operation=='state_template':
    if item['target']!='.config/systemd/user/xp-session-theme.service':raise UpdateError('Unsupported template target.')
    item['_template']=tar.extractfile(item['member']).read().decode()
 return data


SETUP_SCRIPTS={'.local/share/xp-accessories/desktop-update/setup-desktop.py',
               '.local/share/xp-accessories/alpha-update/setup.py'}

def setup_descriptor(data):
 descriptor=data.get('native_setup')
 if descriptor is None:
  legacy={'0.3.1':('.local/share/xp-accessories/desktop-update/setup-desktop.py','desktop-0.3.1'),
          '1.0.0-alpha.2':('.local/share/xp-accessories/alpha-update/setup.py','alpha-1.0.0-alpha.2')}
  if data['version'] not in legacy:return None
  script,backup=legacy[data['version']]
  if not any(i['target']==script for i in data['files']):return None
  descriptor={'script':script,'backup':'.local/share/xplasma/maintenance/'+backup}
 if not isinstance(descriptor,dict) or set(descriptor)!={'script','backup'}:raise UpdateError('Invalid native setup metadata.')
 if descriptor['script'] not in SETUP_SCRIPTS or not re.fullmatch(r'\.local/share/xplasma/maintenance/[A-Za-z0-9_.-]+',descriptor['backup']):raise UpdateError('Unsupported native setup path.')
 if not any(i['target']==descriptor['script'] and i.get('operation','replace')=='replace' for i in data['files']):raise UpdateError('Native setup script is missing from the signed release.')
 return descriptor

def properties_running():
 for path in Path('/proc').glob('[0-9]*/cmdline'):
  try:
   if path.stat().st_uid==os.getuid() and Path(os.fsdecode(path.read_bytes().split(b'\0')[0])).name=='xplasma-edit':return True
  except (OSError,ValueError):pass
 return False

def setup_path(home,name):
 path=PurePosixPath(name)
 if path.is_absolute() or '..' in path.parts or '\\' in name or not path.parts:raise UpdateError('Unsafe native setup snapshot path.')
 result=Path(home)/path
 for parent in result.parents:
  if parent==Path(home).parent:break
  if parent.is_symlink():raise UpdateError('Linked native setup destination: '+name)
 return result

def file_record(path):
 if path.is_symlink():return {'kind':'link','value':os.readlink(path)}
 if path.is_file():return {'kind':'file','value':digest(path)}
 if path.exists():raise UpdateError('Native snapshot destination is not a file: '+str(path))
 return None

def native_changes(home,journal):
 native=journal.get('native_setup')
 if not native or native.get('already_complete') or native.get('rolled_back'):return []
 backup=setup_path(home,native['backup'])
 if not (backup/'before.json').exists():return []
 if not (backup/'after.json').is_file():raise UpdateError('Native setup was interrupted before its recovery snapshot finished. Backup: '+str(backup))
 for name in ['before.json','after.json']:
  path=backup/name
  if path.is_symlink() or (native.get(name) and digest(path)!=native[name]):raise UpdateError('Native recovery snapshot changed: '+str(path))
 before=json.loads((backup/'before.json').read_text());after=json.loads((backup/'after.json').read_text());changes=[]
 for name in sorted(set(before)|set(after)):
  old,new=before.get(name),after.get(name)
  if old==new:continue
  dest=setup_path(home,name)
  # The helper is signed; still constrain recovery to per-user XP files/config.
  if not name.startswith(('.local/share/','.local/lib/','.config/')):raise UpdateError('Unsupported native recovery target: '+name)
  if file_record(dest) not in (old,new):raise UpdateError('Native rollback would overwrite later edits: '+name)
  saved=backup/'backup'/name
  if old and file_record(saved)!=old:raise UpdateError('Native rollback backup is damaged: '+name)
  changes.append((name,old,new,saved))
 return changes

def restore_native(home,changes):
 for name,old,new,saved in reversed(changes):
  dest=setup_path(home,name)
  if old is None:
   if dest.exists() or dest.is_symlink():dest.unlink()
  elif old['kind']=='file':atomic(dest,saved.read_bytes(),saved.stat().st_mode&0o777)
  elif old['kind']=='link':
   dest.parent.mkdir(parents=True,exist_ok=True)
   temporary=dest.with_name(dest.name+'.xp-restore-'+str(time.time_ns()))
   try:temporary.symlink_to(old['value']);os.replace(temporary,dest)
   finally:
    if temporary.is_symlink():temporary.unlink()
  else:raise UpdateError('Unknown native backup entry.')


def archive_native_backup(home,journal):
 native=journal.get('native_setup')
 if not native or native.get('already_complete') or native.get('rolled_back'):return
 backup=setup_path(home,native['backup'])
 if backup.exists():
  archived=backup.with_name(backup.name+'.rolled-back-'+str(time.time_ns()));backup.rename(archived)
  native['archived_backup']=str(archived.relative_to(home))
 native['rolled_back']=True

def pending_setup(home):
 home=Path(home);record=home/'.local/share/xplasma-updater/current.json'
 if not record.is_file():return None
 tx=Path(json.loads(record.read_text())['transaction'])
 if tx.is_symlink() or not tx.resolve().is_relative_to((home/'.local/share/xplasma-updater/transactions').resolve()):raise UpdateError('Invalid update transaction path.')
 journal=json.loads((tx/'journal.json').read_text())
 if journal.get('status')!='complete':return None
 native=journal.get('native_setup')
 if native and not native.get('rolled_back'):return None
 data={'version':journal['version'],'native_setup':journal.get('setup_spec'),'files':[{'target':i['path'],'sha256':i['installed_sha256']} for i in journal['files']]}
 if journal.get('setup_files'):data['files']=journal['setup_files']
 descriptor=setup_descriptor(data)
 if not descriptor:return None
 if (setup_path(home,descriptor['backup'])/'after.json').is_file():return None
 return tx,descriptor,data

def finish_pending(home,progress=None):
 home=Path(home);recover(home)
 with locked(home/'.local/share/xplasma-updater') as update_lock:
  pending=pending_setup(home)
  if not pending:raise UpdateError('There is no pending native setup.')
  if properties_running():raise UpdateError('Close XPlasma Properties, then finish native setup again.')
  tx,descriptor,data=pending
  try:
   run_native_setup(home,tx,descriptor,data,progress or (lambda message:None),lock_fd=update_lock.fileno())
   journal=json.loads((tx/'journal.json').read_text());journal['status']='complete';write_json(tx/'journal.json',journal)
  except BaseException as error:
   journal=json.loads((tx/'journal.json').read_text())
   try:
    changes=native_changes(home,journal);restore_native(home,changes);archive_native_backup(home,journal)
    journal['status']='complete';write_json(tx/'journal.json',journal)
   except Exception as recovery:
    journal['status']='recovery-required';write_json(tx/'journal.json',journal)
    raise UpdateError(str(error)+' Recovery needs attention: '+str(recovery)) from error
   raise
  return tx

def run_native_setup(home,transaction,descriptor,data,progress,lock_fd=None):
 journal=json.loads((transaction/'journal.json').read_text());backup=setup_path(home,descriptor['backup'])
 native={**descriptor,'already_complete':(backup/'after.json').is_file()}
 journal['native_setup']=native;journal['status']='native-applying';write_json(transaction/'journal.json',journal)
 if native['already_complete']:
  progress('Native setup was already completed on this computer.');return
 if backup.exists():raise UpdateError('An incomplete native setup backup needs attention: '+str(backup))
 script=setup_path(home,descriptor['script']);signed={i['target']:i for i in data['files']}
 # Every setup input, including its own file inventory, must match signed bytes.
 inventory=script.parent/'files.json';inventory_name=str(inventory.relative_to(home))
 if inventory_name not in signed or inventory.is_symlink() or digest(inventory)!=signed[inventory_name]['sha256']:raise UpdateError('Native setup inventory is not authenticated.')
 names=json.loads(inventory.read_text())
 if not isinstance(names,dict) or len(names)>20000:raise UpdateError('Invalid native setup inventory.')
 for relative,sha in {**names,script.name:signed[descriptor['script']]['sha256']}.items():
  path=setup_path(home,str(script.parent.relative_to(home))+'/'+relative);name=str(path.relative_to(home))
  if path.is_symlink() or not isinstance(sha,str) or not re.fullmatch(r'[0-9a-f]{64}',sha) or (name in signed and sha!=signed[name]['sha256']) or not path.is_file() or digest(path)!=sha:raise UpdateError('Native setup input missing or modified: '+name)
 progress('Preparing native components. Administrator authentication may be requested.')
 process=subprocess.Popen([sys.executable,'-u',str(script)],cwd=home,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',bufsize=1,start_new_session=True,pass_fds=(lock_fd,) if lock_fd is not None else ())
 try:
  for line in process.stdout:progress(line.rstrip())
  code=process.wait()
 except BaseException:
  if process.poll() is None:
   os.killpg(process.pid,signal.SIGTERM)
   try:process.wait(timeout=10)
   except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
  raise
 finally:process.stdout.close()
 if code:raise UpdateError('Native setup failed (exit '+str(code)+'). See installation output.')
 if not (backup/'after.json').is_file():raise UpdateError('Native setup did not create its recovery snapshot.')
 for name in ['before.json','after.json']:native[name]=digest(backup/name)
 journal['native_setup']=native;write_json(transaction/'journal.json',journal)
 native_changes(home,journal)
 progress('Native components installed. Log out and back in to reload them.')

def compatible(item,arch=None,qt=None):
 arch=arch or {'x86_64':'amd64','aarch64':'arm64'}.get(platform.machine(),platform.machine())
 if item.get('arch') and item['arch']!=arch:return False
 # Native binaries must state an exact Qt minor series, supplied by detection.
 if item.get('qt') and item['qt']!=qt:return False
 return True

def restored_backup(home,state,name,actual):
 # Only a completed transaction's intact backup proves a recognized old file.
 root=Path(home)/'.local/share/xplasma-updater/transactions'
 for tx in root.glob('*'):
  if tx.is_symlink():continue
  try:
   journal=json.loads((tx/'journal.json').read_text())
   if journal.get('status')!='complete' or journal.get('state')!=str(state):continue
   for item in journal.get('files',[]):
    if item.get('path')!=name or item.get('before_sha256')!=actual:continue
    backup=tx/'backup'/name
    if backup.is_file() and not backup.is_symlink() and digest(backup)==actual:return True
  except (OSError,ValueError,KeyError):continue
 return False

def plan(home,data,optional=(),arch=None,qt=None):
 home=Path(home);state,info,features=find_installation(home);updates=home/'.local/share/xplasma-updater'
 current=json.loads((updates/'current.json').read_text()).get('version','0.0.0') if (updates/'current.json').exists() else '0.0.0'
 if version(data['version'])<=version(current):raise UpdateError('This release is already installed or older.')
 offered={i['feature'] for i in data['files'] if i.get('optional')};optional=set(optional)
 if optional-offered:raise UpdateError('Unknown optional feature selected.')
 chosen=features|optional
 if (updates/'app/core.py').is_file():chosen.add('updates')
 client_files=json.loads((updates/'client-files.json').read_text()) if (updates/'client-files.json').exists() else {}
 after=json.loads((state/'after.json').read_text()) if (state/'after.json').exists() else {}
 entries=[];conflicts=[];skipped=[];paths=set();matched=set()
 for item in data['files']:
  if item['feature'] not in chosen and not set(item.get('features_any',[]))&chosen:continue
  if not compatible(item,arch,qt):skipped.append(item['target']);continue
  matched.add(item['target'])
  dest=target(home,state,item['target']);name=str(dest.relative_to(home))
  if name in paths:raise UpdateError('Duplicate destination in update.')
  paths.add(name)
  operation=item.get('operation','replace')
  if operation=='text_patch':
   if not dest.is_file():continue
   text=dest.read_text();original=text
   for rule in item['_rules']:
    if rule['replace'] in text:continue
    if text.count(rule['find'])!=1:conflicts.append(name);break
    text=text.replace(rule['find'],rule['replace'])
   else:
    if text==original:continue
    content=text.encode();item={**item,'generated':text,'sha256':hashlib.sha256(content).hexdigest(),'size':len(content)}
   if name in conflicts:continue
  elif operation=='state_template':
   text=item['_template'].replace('{{STATE_NAME}}',state.name);item={**item,'generated':text,'sha256':hashlib.sha256(text.encode()).hexdigest(),'size':len(text.encode())}
  if dest.exists():
   if not dest.is_file():raise UpdateError('Update destination is not a file.')
   actual=digest(dest)
   if actual==item['sha256']:continue
   known=after.get(name,{})
   baseline=known.get('value') if isinstance(known,dict) and known.get('kind')=='file' else client_files.get(name)
   if actual not in {baseline,client_files.get(name),*item.get('base_sha256',[])} and not restored_backup(home,state,name,actual):conflicts.append(name);continue
  entries.append({**item,'path':name,'before_sha256':digest(dest) if dest.exists() else None})
 # Keep an existing accessory selection marker in step with explicit additions.
 app_features={'notepad','calculator','paint','picture_viewer','media_player','sound_recorder','volume_control','command_prompt','task_manager','pinball','minesweeper','solitaire'}
 marker_name='.local/share/xp-accessories/installed-apps.json';marker=home/marker_name
 if optional&app_features and marker.exists():
  target(home,state,marker_name)
  old_selection=json.loads(marker.read_text())
  if not isinstance(old_selection,list) or not all(isinstance(x,str) for x in old_selection):raise UpdateError('Accessory selection marker is invalid.')
  content=json.dumps(sorted(set(old_selection)|(optional&app_features)),indent=2)+'\n'
  if hashlib.sha256(content.encode()).hexdigest()!=digest(marker):
   entries.append({'path':marker_name,'sha256':hashlib.sha256(content.encode()).hexdigest(),'before_sha256':digest(marker),'generated':content,'mode':0o644})
 descriptor=setup_descriptor(data)
 if descriptor and descriptor['script'] not in matched:descriptor=None
 return {'setup':descriptor,'state':str(state),'version':data['version'],'entries':entries,'conflicts':conflicts,'skipped':[name for name in skipped if name not in matched],'optional':sorted(optional),'release':data}

@contextlib.contextmanager
def locked(root):
 root.mkdir(parents=True,exist_ok=True)
 with (root/'update.lock').open('a') as f:
  try:fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB)
  except BlockingIOError:raise UpdateError('Another update is in progress.')
  yield f

def rollback(home,transaction):
 home=Path(home);transaction=Path(transaction);journal=json.loads((transaction/'journal.json').read_text());conflicts=[]
 # Native and OTA rollback are preflighted together before changing either.
 changes=native_changes(home,journal)
 projected={name:old for name,old,new,saved in changes}
 # Preflight all touched files first; rollback never silently overwrites later edits.
 for item in journal['files']:
  p=home/item['path'];record=projected.get(item['path']) if item['path'] in projected else file_record(p)
  if record and record['kind']!='file':conflicts.append(item['path']);continue
  actual=record.get('value') if record else None
  if actual not in (item['installed_sha256'],item['before_sha256']):conflicts.append(item['path'])
 if conflicts:raise UpdateError('Rollback would overwrite later edits: '+', '.join(conflicts))
 restore_native(home,changes)
 for item in reversed(journal['files']):
  p=home/item['path']
  if item['before_sha256'] is None:
   if p.exists():p.unlink()
  else:atomic(p,(transaction/'backup'/item['path']).read_bytes(),item['before_mode'])
 for name in ['after.json','installation.json']:
  backup=transaction/name
  if backup.exists():atomic(Path(journal['state'])/name,backup.read_bytes(),0o600)
 root=home/'.local/share/xplasma-updater'
 if (transaction/'previous-current.json').exists():atomic(root/'current.json',(transaction/'previous-current.json').read_bytes(),0o600)
 elif (root/'current.json').exists():(root/'current.json').unlink()
 if 'selection_unit_before' in journal:
  if journal['selection_unit_before']:selection_units(True,home)
  else:selection_units(False,home)
 archive_native_backup(home,journal)
 journal['status']='rolled-back';write_json(transaction/'journal.json',journal)

def apply(home,folder,optional=(),key=BASE/'release-key.pem',arch=None,qt=None,progress=None):
 progress=progress or (lambda message:None)
 home=Path(home);root=home/'.local/share/xplasma-updater'
 recover(home)
 with locked(root) as update_lock:
  data=verify_release(folder,key);p=plan(home,data,optional,arch,qt)
  if p['conflicts']:raise UpdateError('Locally modified files need review: '+', '.join(p['conflicts']))
  if p['skipped']:raise UpdateError('Selected components do not match this architecture/Qt version: '+', '.join(p['skipped']))
  if p['setup'] and not (setup_path(home,p['setup']['backup'])/'after.json').exists() and properties_running():raise UpdateError('Close XPlasma Properties, then click Install update again.')
  if not p['entries']:raise UpdateError('No files for your installed features need updating.')
  state=Path(p['state']);tx=root/'transactions'/(data['version']+'-'+str(time.time_ns()));tx.mkdir(parents=True)
  for name in ['after.json','installation.json']:
   if (state/name).exists():shutil.copy2(state/name,tx/name)
  if (root/'current.json').exists():shutil.copy2(root/'current.json',tx/'previous-current.json')
  journal={'state':str(state),'status':'applying','version':data['version'],'files':[],'setup_spec':p['setup']}
  if p['setup']:
   prefix=str(PurePosixPath(p['setup']['script']).parent)+'/'
   journal['setup_files']=[{'target':i['target'],'sha256':i['sha256']} for i in data['files'] if i['target'].startswith(prefix)]
  selection_activation=any(i['path']=='.config/systemd/user/xp-session-theme.path' for i in p['entries'])
  if selection_activation:
   r=subprocess.run(['systemctl','--user','is-enabled','xp-session-theme.path'],capture_output=True,text=True,timeout=10)
   journal['selection_unit_before']=r.returncode==0 or (home/'.config/systemd/user/default.target.wants/xp-session-theme.path').is_symlink()
  # Persist a complete rollback journal before changing any file.
  for item in p['entries']:
   dest=home/item['path'];actual=digest(dest) if dest.is_file() else None
   if actual!=item['before_sha256']:raise UpdateError('File changed while preparing update: '+item['path'])
   old_mode=dest.stat().st_mode&0o777 if dest.exists() else 0o644
   if dest.exists():backup=tx/'backup'/item['path'];backup.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(dest,backup)
   journal['files'].append({'path':item['path'],'installed_sha256':item['sha256'],'before_sha256':actual,'before_mode':old_mode})
  write_json(tx/'journal.json',journal)
  try:
   progress('Installing verified update files…')
   with tarfile.open(Path(folder)/'payload.tar.gz','r:gz') as tar:
    for item in p['entries']:
     content=item['generated'].encode() if 'generated' in item else tar.extractfile(item['member']).read()
     atomic(home/item['path'],content,item.get('mode',0o644))
   after=json.loads((state/'after.json').read_text()) if (state/'after.json').exists() else {}
   for item in p['entries']:after[item['path']]={'kind':'file','value':item['sha256']}
   write_json(state/'after.json',after)
   # Preserve original before.json and its backups for the original uninstaller.
   info=json.loads((state/'installation.json').read_text());field='apps' if 'apps' in info and 'features' not in info else 'features'
   info[field]=sorted(set(info.get(field,[]))|set(p['optional']));write_json(state/'installation.json',info)
   write_json(root/'current.json',{'version':data['version'],'transaction':str(tx)})
   if selection_activation:journal['activation_deferred']=selection_units(True,home)
   write_json(tx/'journal.json',journal)
   if p['setup']:
    run_native_setup(home,tx,p['setup'],data,progress,lock_fd=update_lock.fileno())
    journal=json.loads((tx/'journal.json').read_text())
   journal['status']='complete';write_json(tx/'journal.json',journal)
  except BaseException as error:
   try:rollback(home,tx)
   except Exception as recovery:
    journal=json.loads((tx/'journal.json').read_text());journal['status']='recovery-required';write_json(tx/'journal.json',journal)
    raise UpdateError(str(error)+' Recovery needs attention: '+str(recovery)) from error
   raise
  return tx

def selection_units(enable,home):
 # Enablement is persistent filesystem state; a missing session bus can defer
 # live activation until login, without undoing a successful file transaction.
 home=Path(home);units=home/'.config/systemd/user'
 link=units/'default.target.wants/xp-session-theme.path'
 for parent in [units,link.parent]:
  if parent.is_symlink():raise UpdateError('Refusing symbolic-link service directory.')
 if link.exists() or link.is_symlink():
  # systemctl enable writes an absolute link; earlier updaters wrote a relative one.
  if not link.is_symlink() or link.readlink() not in (Path('../xp-session-theme.path'),units/'xp-session-theme.path'):
   raise UpdateError('Unexpected theme service enablement entry.')
 if enable:
  link.parent.mkdir(parents=True,exist_ok=True)
  if not link.is_symlink():link.symlink_to('../xp-session-theme.path')
 elif link.is_symlink():link.unlink()
 commands=[['daemon-reload'],['restart' if enable else 'stop','xp-session-theme.path']]
 for command in commands:
  r=subprocess.run(['systemctl','--user',*command],capture_output=True,text=True,timeout=15)
  if r.returncode:
   error=(r.stderr or '')+(r.stdout or '')
   if any(message in error for message in ('Failed to connect to','No medium found','No data available')):return True
   raise UpdateError('Theme service activation failed: '+error.strip())
 return False

def recover(home):
 root=Path(home)/'.local/share/xplasma-updater'
 with locked(root):
  for tx in sorted((root/'transactions').glob('*')):
   p=tx/'journal.json'
   if p.exists() and json.loads(p.read_text()).get('status') in {'applying','native-applying','recovery-required'}:rollback(home,tx)

def repo_name(name):
 if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',name):raise UpdateError('Repository must be owner/name.')
 return name

def public_read(url,limit,destination=None):
 request=urllib.request.Request(url,headers={'User-Agent':'XP-Transformation-Pack-Updater','Accept':'application/vnd.github+json' if destination is None else 'application/octet-stream'})
 parsed=urllib.parse.urlparse(url)
 if parsed.scheme!='https' or parsed.hostname not in {'api.github.com','github.com'}:raise UpdateError('Invalid public update URL.')
 try:
  with urllib.request.urlopen(request,timeout=45) as response:
   if destination is None:
    data=response.read(limit+1)
    if len(data)>limit:raise UpdateError('GitHub metadata too large.')
    return data
   total=0
   with Path(destination).open('wb') as output:
    while chunk:=response.read(1024*1024):
     total+=len(chunk)
     if total>limit:raise UpdateError('Downloaded update exceeds size limit.')
     output.write(chunk)
 except (urllib.error.URLError,TimeoutError) as error:raise UpdateError('Public GitHub update request failed: '+str(error)) from error

def access_mode():
 config=Path.home()/'.local/share/xplasma-updater/config.json'
 if not config.exists():config=BASE/'config.json'
 return json.loads(config.read_text()).get('access','private') if config.exists() else 'public'

def github_json(endpoint):
 if access_mode()=='public':return json.loads(public_read('https://api.github.com/'+endpoint,4*1024*1024))
 if not shutil.which('gh'):raise UpdateError('Private updates require GitHub CLI (gh) and gh auth login.')
 r=subprocess.run(['gh','api','--hostname','github.com',endpoint],capture_output=True,timeout=45)
 if r.returncode:raise UpdateError('GitHub request failed. Check gh auth login and repository access.')
 if len(r.stdout)>4*1024*1024:raise UpdateError('GitHub metadata too large.')
 return json.loads(r.stdout)
def latest(repo,channel='stable'):
 if channel not in {'stable','alpha'}:raise UpdateError('Unknown update channel.')
 releases=github_json('repos/'+repo_name(repo)+'/releases?per_page=100')
 candidates=[]
 for release in releases:
  if release.get('draft') or (release.get('prerelease') and channel=='stable'):continue
  try:v=version(release.get('tag_name','').removeprefix('v'))
  except UpdateError:continue
  if channel=='stable' and v[3]!=3:continue
  names={a['name'] for a in release.get('assets',[])}
  if {'release.json','release.sig','payload.tar.gz'}<=names:candidates.append((v,release))
 if not candidates:raise UpdateError('No complete signed '+channel+' update release found.')
 return max(candidates,key=lambda item:item[0])[1]
def download(repo,release,dest):
 repo=repo_name(repo);dest=Path(dest);dest.mkdir(parents=True,exist_ok=True)
 assets={a['name']:a for a in release.get('assets',[])}
 for name,limit in [('release.json',2*1024*1024),('release.sig',4096),('payload.tar.gz',512*1024*1024)]:
  a=assets.get(name)
  if not a or a['size']>limit:raise UpdateError('Missing or oversized release asset: '+name)
  if access_mode()=='public':
   url=a.get('browser_download_url','')
   if not url.startswith('https://github.com/'+repo+'/releases/download/'):raise UpdateError('Asset URL does not belong to the selected repository.')
   public_read(url,limit,dest/name)
  else:
   with (dest/name).open('wb') as f:
    r=subprocess.run(['gh','api','--hostname','github.com','repos/'+repo+'/releases/assets/'+str(int(a['id'])),'-H','Accept: application/octet-stream'],stdout=f,stderr=subprocess.PIPE,timeout=120)
   if r.returncode or (dest/name).stat().st_size>limit:raise UpdateError('Unable to download '+name)
 return dest

def rollback_locked(home,transaction):
 with locked(Path(home)/'.local/share/xplasma-updater'):return rollback(home,transaction)
