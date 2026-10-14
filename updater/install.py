#!/usr/bin/python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""One-time updater bootstrap; never reapplies the desktop theme."""
import os,json
from pathlib import Path
import core

def register(home,source=None,require_existing=True,switch_feed=False):
 home=Path(home);source=Path(source or core.BASE)
 if require_existing:core.find_installation(home)
 dest=home/'.local/share/xplasma-updater/app';dest.mkdir(parents=True,exist_ok=True)
 for name in ['core.py','gui.py','release-key.pem','config.json','README.md']:
  core.atomic(dest/name,(source/name).read_bytes())
 config=home/'.local/share/xplasma-updater/config.json'
 if switch_feed or not config.exists():core.atomic(config,(source/'config.json').read_bytes(),0o600)
 entry=home/'.local/share/applications/xplasma-update.desktop'
 # Quote according to Desktop Entry Exec syntax, not shell syntax.
 path=str(dest/'gui.py').replace('\\','\\\\').replace('"','\\"').replace('`','\\`').replace('$','\\$').replace('%','%%')
 core.atomic(entry,('[Desktop Entry]\nType=Application\nName=XPlasma Update\nComment=Update the Windows XP transformation pack\nExec=python3 "'+path+'"\nIcon=system-software-update\nTerminal=false\nCategories=Settings;System;\n').encode())
 ledger={str(p.relative_to(home)):core.digest(p) for p in dest.iterdir() if p.is_file()}
 ledger[str(entry.relative_to(home))]=core.digest(entry)
 core.write_json(home/'.local/share/xplasma-updater/client-files.json',ledger)
 return dest
if __name__=='__main__':
 if os.getuid()==0:raise SystemExit('Run as your desktop user, without sudo.')
 register(Path.home(),switch_feed=json.loads((core.BASE/'config.json').read_text()).get('access')=='public');print('Updater installed. Open XPlasma Update from Start. Existing theme and installer backups were preserved.')
