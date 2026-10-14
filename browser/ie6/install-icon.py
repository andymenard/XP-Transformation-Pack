#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bind the ESR's original application ID to its IE artwork, preserving scaling."""
import configparser,hashlib,json,os,re,shutil,subprocess,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parent

def atomic(path,data,mode=0o644):
 path.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.NamedTemporaryFile(dir=path.parent,delete=False) as stream:stream.write(data);temporary=stream.name
 os.chmod(temporary,mode);os.replace(temporary,path)

def install(home=None,source=None):
 home=Path(home or Path.home());source=Path(source or ROOT);browser=home/'.local/share/firefox-esr-ie6'
 if not (browser/'firefox/firefox').is_file():raise RuntimeError('The IE6 Firefox installation was not found.')
 cfg=configparser.RawConfigParser();cfg.read(browser/'firefox/application.ini');identity=cfg.get('App','RemotingName',fallback='firefox-esr')
 if not re.fullmatch(r'[A-Za-z0-9._-]+',identity):raise RuntimeError('Unexpected Firefox application ID.')
 entries=home/'.local/share/applications';alias=entries/(identity+'.desktop');primary=entries/'firefox-esr-ie6.desktop'
 marker='X-XPlasma-IE6=true'
 if alias.is_file() and marker not in alias.read_text() and str(browser) not in alias.read_text():raise RuntimeError('An unrelated launcher already owns '+identity+'.desktop; it was preserved.')
 icon=browser/'profile/chrome/ie6-icons/application-ie.png'
 chrome=source/'chrome' if (source/'chrome').is_dir() else source/'profile/chrome'
 resources=[(chrome/'ie6-icons/application-ie.png',icon)]
 resources.extend((p,browser/'firefox/browser/chrome/icons/default'/p.name) for p in (source/'runtime-overrides/browser/chrome/icons/default').glob('default*.png'))
 if not resources[0][0].is_file() or len(resources)<6:raise RuntimeError('The multi-resolution IE icon assets are incomplete.')
 backup=home/'.local/share/xplasma/maintenance'/('ie6-icon-'+time.strftime('%Y%m%d-%H%M%S'));backup.mkdir(parents=True,exist_ok=True)
 targets=[dest for _,dest in resources]+[primary,alias]
 for dest in targets:
  if any(p.is_symlink() for p in [dest,*dest.parents] if p!=home.parent):raise RuntimeError('Refusing linked icon destination: '+str(dest))
  if dest.exists():saved=backup/dest.relative_to(home);saved.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(dest,saved)
 for src,dest in resources:atomic(dest,src.read_bytes())
 # Preserve the user's menu name and launcher command, including HiDPI options.
 text=primary.read_text() if primary.is_file() else '[Desktop Entry]\nType=Application\nName=Internet Explorer 6\nExec="'+str(browser/'launch')+'" %U\nTerminal=false\nCategories=Network;WebBrowser;\nStartupNotify=true\n'
 def key(text,name,value):
  pattern=r'^'+re.escape(name)+r'=.*$'
  return re.sub(pattern,name+'='+value,text,flags=re.M) if re.search(pattern,text,re.M) else text.rstrip()+'\n'+name+'='+value+'\n'
 text=key(text,'Icon',str(icon));text=key(text,'StartupWMClass',identity);text=key(text,'X-XPlasma-IE6','true');atomic(primary,text.encode())
 alias_text=key(text,'NoDisplay','true');atomic(alias,alias_text.encode())
 for state in ['fedora-xp-installer','kde-xp-suite','kubuntu-xp-suite','xp-accessory-suite']:
  ledger=home/'.local/share'/state/'after.json'
  if not ledger.is_file():continue
  data=json.loads(ledger.read_text())
  for dest in targets:
   rel=str(dest.relative_to(home))
   if rel in data:data[rel]={'kind':'file','value':hashlib.sha256(dest.read_bytes()).hexdigest()}
  atomic(ledger,(json.dumps(data,indent=2)+'\n').encode(),0o600)
 for tool in ['update-desktop-database','kbuildsycoca6']:
  binary=shutil.which(tool)
  if binary:subprocess.run([binary,str(entries)] if tool=='update-desktop-database' else [binary],check=False,capture_output=True)
 return identity,backup
if __name__=='__main__':
 identity,backup=install();print('Internet Explorer icon registered for '+identity+'. Restart the browser; HiDPI settings and launcher flags were preserved. Backup: '+str(backup))
