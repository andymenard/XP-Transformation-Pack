#!/usr/bin/python3
"""After a Plasma update: whether XPlasma took its widget builds off, and whether this installer can rebuild them."""
import json
from pathlib import Path
BASE=Path(__file__).resolve().parent
STATE_NAME='kde-xp-suite'

def series(version):
 # As forks::seriesOf: 6.7.5 is 6.7, and 6.7.90 and up (KDE's betas) are 6.8.
 parts=[int(x) for x in version.split('.')[:3] if x.isdigit()]
 if len(parts)<2:return ''
 if len(parts)==3 and parts[2]>=90:parts[1]+=1
 return str(parts[0])+'.'+str(parts[1])

def status(home=None,base=BASE):
 # None unless a taskbar installation lost its builds: then the Plasma that
 # took them off, its series, and whether this installer has that series'
 # Plasma sources to build them from.
 home=Path(home or Path.home())
 record=home/'.local/share'/STATE_NAME/'installation.json'
 try:
  features=json.loads(record.read_text())['features']
  settings=json.loads((home/'.local/share/xplasma/state.json').read_text()).get('settings',{})
 except (OSError,ValueError,KeyError):return None
 lost=settings.get('forkLost')
 if 'taskbar' not in features or not isinstance(lost,str) or not series(lost):return None
 name=series(lost)
 available=all(any((Path(base)/'source').glob('plasma-'+project+'-'+name+'.*.tar.xz')) for project in ['desktop','workspace'])
 return {'plasma':lost,'series':name,'available':available}
