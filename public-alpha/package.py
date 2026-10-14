#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package and verify the complete alpha installer, without changing its inputs."""
import argparse,hashlib,json,os,re,shutil,tarfile,tempfile
from pathlib import Path

def package(bundle,output):
 bundle=Path(bundle).resolve();output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True)
 edition=json.loads((bundle/'edition.json').read_text());version=edition['version']
 if not re.fullmatch(r'\d+\.\d+\.\d+-alpha\.\d+',version):raise ValueError('Expected an alpha semantic version')
 features=json.loads((bundle/'features.json').read_text())
 if any(f.get('project') not in {'desktop','firefox','shared'} for f in features):raise ValueError('Excluded project remains in bundle')
 manifest=json.loads((bundle/'SHA256SUMS.json').read_text())
 for relative,expected in manifest.items():
  file=bundle/relative
  if not file.resolve().is_relative_to(bundle):raise ValueError('External link: '+relative)
  if isinstance(expected,dict):
   if not file.is_symlink() or str(file.readlink())!=expected['target']:raise ValueError('Invalid link: '+relative)
  elif hashlib.sha256(file.read_bytes()).hexdigest()!=expected:raise ValueError('Checksum mismatch: '+relative)
 root='XP-Transformation-Pack-Alpha-1.0'
 archive=output/f'xp-transformation-pack-{version}-installer.tar.gz'
 temporary=archive.with_suffix('.new')
 def clean(info):
  if '__pycache__' in Path(info.name).parts or info.name.endswith('.pyc'):return None
  info.uid=info.gid=0;info.uname=info.gname='root'
  return info
 with tarfile.open(temporary,'w:gz',compresslevel=6) as tar:tar.add(bundle,arcname=root,filter=clean)
 temporary.replace(archive)
 with tarfile.open(archive) as tar:
  for relative,expected in manifest.items():
   member=tar.getmember(root+'/'+relative)
   if isinstance(expected,dict):
    if member.linkname!=expected['target']:raise ValueError('Archive link differs: '+relative)
   elif hashlib.sha256(tar.extractfile(member).read()).hexdigest()!=expected:raise ValueError('Archive content differs: '+relative)
  if not tar.getmember(root+'/Start-Installer.desktop').mode&0o111:raise ValueError('GUI launcher must be executable')
 (output/'SHA256SUMS').write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')
 print('Verified',len(manifest),'resources;',archive.stat().st_size,'bytes;',archive,flush=True)
 return archive
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('bundle',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args();package(args.bundle,args.output)
