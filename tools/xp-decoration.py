#!/usr/bin/python3
"""Keep generated XP captions on the compatible Aurorae renderer."""
from pathlib import Path
import subprocess,sys,time,shutil
GROUP='org.kde.kdecoration2'
LEGACY='org.kde.kwin.aurorae'
V2='org.kde.kwin.aurorae.v2'
DBUS=shutil.which('qdbus6') or shutil.which('qdbus-qt6') or 'qdbus6'
def repair(home=Path.home()):
 args=['--file','kwinrc','--group',GROUP,'--key']
 def read(key):return subprocess.check_output(['kreadconfig6',*args,key],text=True).strip()
 if read('library')!=V2:return False
 theme=read('theme');prefix='__aurorae__svg__'
 if not theme.startswith(prefix):return False
 name=theme[len(prefix):]
 if not name or '/' in name or name in ('.','..'):return False
 rc=Path(home)/'.local/share/aurorae/themes'/name/(name+'rc')
 if not rc.is_file() or '[X-XPlasma-Generated]' not in rc.read_text():return False
 subprocess.run(['kwriteconfig6','--notify',*args,'library',LEGACY],check=True)
 subprocess.run([DBUS,'org.kde.KWin','/KWin','org.kde.KWin.reconfigure'],check=True,capture_output=True)
 return True
if __name__=='__main__':
 if '--login' in sys.argv:
  for _ in range(20):
   r=subprocess.run([DBUS,'org.kde.KWin','/KWin','org.kde.KWin.supportInformation'],capture_output=True)
   if r.returncode==0:break
   time.sleep(0.5)
 try:repair()
 except Exception as error:print('XP decoration compatibility:',error)
