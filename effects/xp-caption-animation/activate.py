#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Activate only the released, tested caption-effect build; never restart KWin."""
from pathlib import Path
import hashlib,platform,re,shutil,subprocess

RELEASE_SHA256 = 'b5bc873a4ec73666cb04459d918553117c1f5a16045461e6b7c5d9769c0d5aa1'

def main():
    if platform.machine() != 'aarch64':
        raise SystemExit('This prebuilt effect is for ARM64. Build against your matching KWin SDK first.')
    kwin=shutil.which('kwin_wayland') or shutil.which('kwin_x11')
    if not kwin:raise SystemExit('KWin is unavailable.')
    version=subprocess.run([kwin,'--version'],check=True,capture_output=True,text=True).stdout
    if not re.search(r'\b6\.7\.5\b',version):
        raise SystemExit('The released native effect requires KWin 6.7.5. Use a matching build for other versions.')
    plugin=Path.home()/'.local/lib/qt6/plugins/kwin/effects/plugins/xp-caption-animation.so'
    if not plugin.is_file() or hashlib.sha256(plugin.read_bytes()).hexdigest()!=RELEASE_SHA256:
        raise SystemExit('Install the optional Caption Animation update first. The tested binary was not found.')
    dbus=shutil.which('qdbus6') or shutil.which('qdbus-qt6')
    writer=shutil.which('kwriteconfig6')
    if not dbus or not writer:raise SystemExit('Plasma 6 configuration and D-Bus tools are required.')
    loaded=subprocess.run([dbus,'org.kde.KWin','/Effects','org.kde.kwin.Effects.loadedEffects'],check=True,capture_output=True,text=True).stdout.split()
    if 'xp-caption-animation' not in loaded:
        result=subprocess.run([dbus,'org.kde.KWin','/Effects','org.kde.kwin.Effects.loadEffect','xp-caption-animation'],check=True,capture_output=True,text=True)
        if result.stdout.strip()!='true':raise SystemExit('KWin could not load the matching effect. No stock effects were changed.')
    backup=Path.home()/'.local/share/xplasma/maintenance/caption-animation-activation'
    backup.mkdir(parents=True,exist_ok=True)
    config=Path.home()/'.config/kwinrc'
    if config.exists() and not (backup/'kwinrc.before').exists():shutil.copy2(config,backup/'kwinrc.before')
    for effect,value in [('xp-caption-animation','true'),('maximize','false'),('squash','false'),('magiclamp','false')]:
        subprocess.run([writer,'--file','kwinrc','--group','Plugins','--key',effect+'Enabled',value],check=True)
        if value=='false':subprocess.run([dbus,'org.kde.KWin','/Effects','org.kde.kwin.Effects.unloadEffect',effect],check=False,capture_output=True)
    print('XP caption animation activated. Configuration backup: '+str(backup))

if __name__=='__main__':main()
