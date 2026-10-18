#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh the public repository's non-free/ folder: the Windows XP media an installer suite ships, marked as Microsoft's.

As in rozniak/xfce-winxp-tc: the project licence covers the source code, not
these assets, which stay Microsoft's copyright (or are derived from it); they
sit in the repository, in one folder, for convenience. Windows program files
(.exe, .dll, .cpl and the like) are left out: media only. So are the
photographers' high-resolution rescans and the Firefox runtimes, which aren't
Microsoft's, and the suite's copies of the project's own code.
"""
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
MEDIA={'.png','.bmp','.jpg','.jpeg','.gif','.ico','.svg','.ttf','.otf','.woff','.wav','.ogg','.cur','.ani','.msstyles','.theme','.tar.gz'}
PROGRAMS={'.exe','.dll','.cpl','.inf','.ocx','.sys','.scr','.drv'}
# Text that describes the media beside it (provenance, theme indexes).
METADATA={'PROVENANCE.json','ICON-PROVENANCE.json','xplasma-icons.json','index.theme','WALLPAPERS.json','SOURCE-NOTES.txt'}
EXCLUDED=('firefox/firefox-linux-','assets/hires-wallpapers/')
NOTICE='''# Non-free assets

Everything in this folder is **not** covered by the project licence (GPL).

These are Windows XP resources — images, icons, cursors, sounds, fonts and
visual styles — and files derived from them, such as the pixel fonts traced
from XP's Tahoma and icon themes built from XP's icons. They are Microsoft's
copyright (or derived from it), and Microsoft's trademarks where they show
its marks. They are in this repository, in this one folder, for convenience:
they are what the installer's payload ships, at the same paths under
`payload/`. Packaging marks the components that use them as non-free.

Windows program files (`.exe`, `.dll`, `.cpl`) and installers are not
included — only visual styles' resource-only `ShellStyle.dll` files, which hold
bitmaps and no code — nor are the high-resolution photo rescans, which belong
to their photographers, or third-party game skins. Where a
file here is a CC0 or otherwise licensed recreation rather than Microsoft's
own, its provenance file (`PROVENANCE.json`, `ICON-PROVENANCE.json`) says so.

`MANIFEST.json` lists every file with its checksum.

Rights holders: open an issue on this repository, and any of these files
will be removed.
'''

def resource_only(path):
 # A visual style's ShellStyle.dll: bitmaps and no code (no entry point),
 # part of the style rather than a program.
 if path.name.lower()!='shellstyle.dll':return False
 data=path.read_bytes()
 if data[:2]!=b'MZ':return False
 pe=int.from_bytes(data[0x3c:0x40],'little')
 return data[pe:pe+4]==b'PE\0\0' and int.from_bytes(data[pe+0x28:pe+0x2c],'little')==0

def suffix(path):
 return '.tar.gz' if path.name.endswith('.tar.gz') else path.suffix.lower()

def repository_blobs(public):
 listing=subprocess.check_output(['git','-C',str(public),'ls-tree','-r','HEAD'],text=True)
 return {line.split('\t',1)[0].split()[2] for line in listing.splitlines()}

def blob(path):
 data=path.read_bytes();return hashlib.sha1(b'blob %d\0'%len(data)+data).hexdigest()

def select(payload,known):
 # The payload's media that the repository doesn't already have as source.
 chosen=[]
 for path in sorted(payload.rglob('*')):
  if not path.is_file() or path.is_symlink():continue
  relative=path.relative_to(payload).as_posix()
  if relative.startswith(EXCLUDED) or (suffix(path) in PROGRAMS and not resource_only(path)):continue
  if suffix(path) not in MEDIA and path.name not in METADATA and not resource_only(path) and not (path.parent.name=='cursors' and not path.suffix):continue
  if blob(path) in known:continue
  chosen.append(relative)
 # (The pixel fonts' tracing records travel with them.)
 for path in sorted(payload.glob('assets/fonts/xplasma-tahoma-pixel*.json')):
  relative=path.relative_to(payload).as_posix()
  if relative not in chosen:chosen.append(relative)
 return sorted(chosen)

def refresh(suite,public):
 suite=Path(suite).resolve();public=Path(public).resolve();payload=suite/'payload'
 if not payload.is_dir():raise ValueError('Expected an installer suite with payload/')
 if not (public/'.git').exists() or public==ROOT:raise ValueError('Expected a separate public repository checkout')
 chosen=select(payload,repository_blobs(public))
 target=public/'non-free'
 if target.exists():shutil.rmtree(target)
 manifest={}
 for relative in chosen:
  source=payload/relative;dest=target/'payload'/relative
  dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,dest)
  manifest['payload/'+relative]={'sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'license':'non-free'}
  if source.stat().st_size>=95*1024*1024:raise RuntimeError('Too large for GitHub: '+relative)
 (target/'NOTICE.md').write_text(NOTICE)
 (target/'MANIFEST.json').write_text(json.dumps(manifest,indent=1,sort_keys=True)+'\n')
 size=sum((target/'payload'/r).stat().st_size for r in chosen)
 print('non-free/:',len(chosen),'files,',round(size/1e6,1),'MB')
 return chosen

if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('suite',type=Path);p.add_argument('public',type=Path);a=p.parse_args()
 refresh(a.suite,a.public)
