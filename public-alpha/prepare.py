#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Export the alpha source and prune a separate local test installer, leaving the private suite intact."""
import argparse,json,shutil,subprocess,hashlib,tarfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PROFILE=json.loads((ROOT/'public-alpha/edition.json').read_text())
KEEP={'native','browser','installer','updater','themes','effects','tools','public-alpha'}
EXCLUDED={'applications','tray','extras'}
def configure(base):
 features=json.loads((ROOT/'installer/features.json').read_text())
 features=[f for f in features if f.get('project') not in EXCLUDED]
 for f in features:
  if f['id']=='updates':f['description']='Signed alpha updates from the public XP Transformation Pack repository. No GitHub account required.'
 (base/'features.json').write_text(json.dumps(features,indent=2)+'\n')
 (base/'edition.json').write_text(json.dumps(PROFILE,indent=2)+'\n')
def source(destination):
 destination.mkdir(parents=True,exist_ok=True)
 paths=subprocess.check_output(['git','ls-files','-z','--cached','--others','--exclude-standard'],cwd=ROOT).decode().split('\0')
 for name in sorted(set(paths)-{''}):
  p=Path(name)
  if p.parts[0] not in KEEP:continue
  if name.startswith('tools/dark-theme/tests/') or '__pycache__' in p.parts:continue
  if p.suffix.lower() in {'.exe','.dll','.ttf','.msstyles','.vlt'}:continue
  if p.suffix=='.pem' and name!='updater/release-key.pem':continue
  source=ROOT/p
  if source.is_symlink():raise RuntimeError('Source links must be reviewed before public export: '+name)
  if source.is_file():
   target=destination/p;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
 configure(destination/'installer')
 config={'repository':PROFILE['repository'],'channel':'alpha','access':'public'}
 (destination/'updater/config.json').write_text(json.dumps(config)+'\n')
 (destination/'installer/dependencies.py').write_text((destination/'installer/dependencies.py').read_text().replace("'updates':'openssl gh'","'updates':'openssl'").replace("'updates':'openssl github-cli'","'updates':'openssl'"))
 (destination/'CREDITS.md').write_text((ROOT/('POSTING-CREDITS.md' if (ROOT/'POSTING-CREDITS.md').exists() else 'CREDITS.md')).read_text()+'\n## Alpha scope\n\nAlpha 1.0 includes Desktop, optional IE6 Firefox and the updater. XP apps, custom tray menus, Codex colors and Clippy are excluded. Their credits above document the larger project history, not included alpha components.\n\nInstaller visual reference: [Gough Lui, Microsoft Windows XP Install Process](https://www.youtube.com/watch?v=vTheKjAtnKs&t=134s), from 2:14 onward. No video frames or audio are redistributed.\n')
 (destination/'LICENSE').write_text((ROOT/'native/LICENSE').read_text())
 (destination/'README.md').write_text((ROOT/'public-alpha/README.md').read_text())
 (destination/'installer/README.md').write_text((ROOT/'public-alpha/INSTALLER.md').read_text())
 (destination/'updater/README.md').write_text((ROOT/'public-alpha/UPDATER.md').read_text())
 (destination/'ASSET-DISTRIBUTION.md').write_text((ROOT/'public-alpha/ASSET-DISTRIBUTION.md').read_text())
 (destination/'.gitignore').write_text('build/\ndist/\n__pycache__/\n*.pyc\nprivate-assets/\n*.pem\n!updater/release-key.pem\n*signing-key*\n')
def bundle(suite,destination):
 if destination.exists():
  if not (destination/'LOCAL-ASSET-BUNDLE').is_file():raise RuntimeError('Destination is not an owned local alpha bundle')
  shutil.rmtree(destination)
 shutil.copytree(suite,destination,symlinks=True,ignore=shutil.ignore_patterns('__pycache__'))
 for name in ['accessories','shell','tray','codex']:
  folder=destination/'payload'/name
  if folder.exists():shutil.rmtree(folder)
 configure(destination)
 config={'repository':PROFILE['repository'],'channel':'alpha','access':'public'}
 for p in (ROOT/'updater').iterdir():
  if p.is_file():shutil.copy2(p,destination/'payload/updater'/p.name)
 (destination/'payload/updater/config.json').write_text(json.dumps(config)+'\n')
 (destination/'payload/updater/README.md').write_text((ROOT/'public-alpha/UPDATER.md').read_text())
 (destination/'dependencies.py').write_text((destination/'dependencies.py').read_text().replace("'updates':'openssl gh'","'updates':'openssl'").replace("'updates':'openssl github-cli'","'updates':'openssl'"))
 (destination/'CREDITS.md').write_text((ROOT/('POSTING-CREDITS.md' if (ROOT/'POSTING-CREDITS.md').exists() else 'CREDITS.md')).read_text()+'\nInstaller visual reference: Gough Lui — https://www.youtube.com/watch?v=vTheKjAtnKs&t=134s\n')
 (destination/'README.md').write_text((ROOT/'public-alpha/README.md').read_text())
 (destination/'ASSET-DISTRIBUTION.md').write_text((ROOT/'public-alpha/ASSET-DISTRIBUTION.md').read_text())
 (destination/'PROJECTS.md').write_text('Alpha 1.0: Desktop (default), Firefox (optional), signed public updater. Other projects are excluded.\n')
 # Excluded projects' integration tests are not part of this edition.
 for name in ['test_accessory_integration.py','test_shell_integration.py','test_updater_integration.py']:
  test=destination/'tests'/name
  if test.exists():test.unlink()
 test=destination/'tests/test_installer.py'
 if test.exists():
  test.write_text(test.read_text().replace(' def test_codex_changes_do_not_replace_other_config('," @unittest.skipUnless('codex_colors' in b.KNOWN,'Codex integration is excluded from this edition')\n def test_codex_changes_do_not_replace_other_config("))
 # This is a local test bundle containing user-supplied assets, not a public upload.
 (destination/'LOCAL-ASSET-BUNDLE').write_text('Contains supplied proprietary XP/stock-photo assets. Do not upload without reviewing their separate rights.\n')
 inventory={}
 for p in sorted(destination.rglob('*')):
  if p.name=='SHA256SUMS.json' or '__pycache__' in p.parts:continue
  if p.is_symlink():
   if not p.resolve().is_relative_to(destination.resolve()):raise RuntimeError('External bundle link: '+str(p))
   inventory[str(p.relative_to(destination))]={'kind':'link','target':str(p.readlink())}
  elif p.is_file():inventory[str(p.relative_to(destination))]=hashlib.sha256(p.read_bytes()).hexdigest()
 (destination/'SHA256SUMS.json').write_text(json.dumps(inventory,indent=2)+'\n')
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--source',type=Path);p.add_argument('--suite',type=Path);p.add_argument('--bundle',type=Path);a=p.parse_args()
 if a.source:source(a.source.resolve())
 if a.bundle:
  if not a.suite:p.error('--bundle requires --suite')
  bundle(a.suite.resolve(),a.bundle.resolve())
