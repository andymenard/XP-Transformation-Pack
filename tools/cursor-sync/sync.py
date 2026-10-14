#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Keep the shared cursor fallback aligned with KDE, including custom XP looks."""
import configparser,re,subprocess
from pathlib import Path

def desired(home):
    parser=configparser.RawConfigParser();parser.read(Path(home)/'.config/kcminputrc')
    theme=parser.get('Mouse','cursorTheme',fallback='breeze_cursors');size=parser.getint('Mouse','cursorSize',fallback=32)
    if not re.fullmatch(r'[A-Za-z0-9_.-]+',theme) or not 8<=size<=128:raise ValueError('Invalid desktop cursor settings')
    return theme,size

def apply(home=None):
    home=Path(home or Path.home());theme,size=desired(home)
    target=home/'.local/share/icons/default';index=target/'index.theme';legacy=home/'.icons/default'
    content='[Icon Theme]\nName=Desktop cursor fallback\nInherits='+theme+'\nX-XPlasma-CursorSync=true\n'
    if index.exists() and 'X-XPlasma-CursorSync=true' not in index.read_text():raise RuntimeError('An existing cursor fallback was preserved.')
    if legacy.exists() or legacy.is_symlink():
        if not legacy.is_symlink() or legacy.resolve()!=target.resolve():raise RuntimeError('An existing legacy cursor fallback was preserved.')
    target.mkdir(parents=True,exist_ok=True)
    if not index.exists() or index.read_text()!=content:index.write_text(content)
    legacy.parent.mkdir(parents=True,exist_ok=True)
    if not legacy.is_symlink():legacy.symlink_to(target)
    print('Shared cursor fallback:',theme,'; logical size:',size)
    return theme,size
if __name__=='__main__':apply()
