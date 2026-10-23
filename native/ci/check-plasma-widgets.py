#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the production XP patches against pinned upstream Plasma branches."""
import argparse,hashlib,json,os,subprocess,tempfile,urllib.request
from pathlib import Path

def blob_sha(data):
 return hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()

def fetch_sources(manifest,cache):
 cache.mkdir(parents=True,exist_ok=True)
 for series,branch in manifest['branches'].items():
  for spec in branch['files']:
   target=cache/spec['blob']
   if target.exists():
    if blob_sha(target.read_bytes())!=spec['blob']:raise RuntimeError('Cached source was modified: '+str(target))
    continue
   commit=branch['projects'][spec['project']]['commit']
   url='https://raw.githubusercontent.com/KDE/'+spec['project']+'/'+commit+'/'+spec['path']
   with urllib.request.urlopen(url,timeout=60) as response:data=response.read()
   if blob_sha(data)!=spec['blob']:raise RuntimeError('Upstream source hash mismatch: '+series+' '+spec['path'])
   with tempfile.NamedTemporaryFile(dir=cache,delete=False) as output:
    output.write(data);temporary=Path(output.name)
   temporary.replace(target)

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--checker',type=Path,required=True)
 p.add_argument('--cache',type=Path,default=Path.home()/'.cache/xplasma-plus/widget-sources')
 p.add_argument('--qmlformat',type=Path,required=True)
 p.add_argument('--output',type=Path)
 a=p.parse_args();manifest=Path(__file__).with_name('plasma-widget-sources.json')
 fetch_sources(json.loads(manifest.read_text()),a.cache)
 with tempfile.TemporaryDirectory(prefix='xp-widget-patches-') as temp:
  output=a.output or Path(temp)
  subprocess.run([str(a.checker),str(manifest),str(a.cache),str(output)],check=True)
  count=0
  for file in sorted(output.rglob('*.qml')):
   subprocess.run([str(a.qmlformat),'--ignore-settings',str(file)],check=True,stdout=subprocess.DEVNULL)
   count+=1
  print('Parsed',count,'patched QML files without syntax errors.',flush=True)

if __name__=='__main__':main()
