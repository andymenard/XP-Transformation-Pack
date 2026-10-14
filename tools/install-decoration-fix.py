#!/usr/bin/python3
"""Install the XP Aurorae compatibility preference without reapplying the theme."""
import os,shutil,subprocess
from pathlib import Path

def install(home=Path.home()):
 home=Path(home)
 states=['kde-xp-suite','kubuntu-xp-suite','xp-accessory-suite','fedora-xp-installer']
 installed=[home/'.local/share'/n for n in states if (home/'.local/share'/n/'installation.json').is_file()]
 # Prefer the desktop installation over a separate accessory-only record.
 state=next((p for p in installed if (p/'runtime/tools/keep-xp-session.py').is_file()),installed[0] if installed else None)
 if state is None:raise RuntimeError('No existing XPlasma installation found.')
 tools=state/'runtime/tools';tools.mkdir(parents=True,exist_ok=True)
 backup=home/'.local/share/xplasma/maintenance/decoration-compatibility';backup.mkdir(parents=True,exist_ok=True)
 dest=tools/'xp-decoration.py'
 if dest.exists() and not (backup/'xp-decoration.py').exists():shutil.copy2(dest,backup/'xp-decoration.py')
 shutil.copy2(Path(__file__).with_name('xp-decoration.py'),dest)
 # The existing appearance watcher notices kwinrc changes. Integrate only
 # where the installer already supplies its session-maintenance helper.
 keeper=tools/'keep-xp-session.py'
 marker='# XP decoration compatibility preference'
 if keeper.exists() and marker not in keeper.read_text():
  if not (backup/'keep-xp-session.py').exists():shutil.copy2(keeper,backup/'keep-xp-session.py')
  with keeper.open('a') as stream:
   stream.write('\n'+marker+'\nimport subprocess as _xp_subprocess\nfrom pathlib import Path as _xp_Path\n_xp_subprocess.run([\"python3\", str(_xp_Path(__file__).with_name(\"xp-decoration.py\"))], check=True)\n')
 entry=home/'.config/autostart/xp-decoration-compatibility.desktop';entry.parent.mkdir(parents=True,exist_ok=True)
 path=str(dest).replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$').replace('%','%%')
 entry.write_text('[Desktop Entry]\nType=Application\nName=XP decoration compatibility\nExec=python3 "'+path+'" --login\nOnlyShowIn=KDE;\nX-KDE-autostart-phase=2\nTerminal=false\n')
 return dest
if __name__=='__main__':
 if os.getuid()==0:raise SystemExit('Run as your desktop user, without sudo.')
 dest=install()
 r=subprocess.run([shutil.which('qdbus6') or shutil.which('qdbus-qt6') or 'qdbus6','org.kde.KWin','/KWin','org.kde.KWin.supportInformation'],capture_output=True)
 if r.returncode==0:subprocess.run(['python3',str(dest)],check=True)
 print('XP decoration compatibility installed. It selects the working Aurorae plugin only for generated XP themes. Login protection is enabled; no theme reapplication is needed.')
