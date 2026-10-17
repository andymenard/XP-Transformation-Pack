# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a signed release from an explicit reviewed file specification."""
import argparse,hashlib,io,json,subprocess,tarfile
from pathlib import Path
from core import version,repo_name,setup_descriptor

def build(specification,key,output):
 spec=json.loads(Path(specification).read_text());version(spec['version']);out=Path(output);out.mkdir(parents=True,exist_ok=True);files=[]
 with tarfile.open(out/'payload.tar.gz','w:gz') as tar:
  for i,item in enumerate(spec['files']):
   p=Path(item['source']);data=p.read_bytes();member='files/'+str(i);info=tarfile.TarInfo(member);info.size=len(data);info.mode=item.get('mode',0o644);tar.addfile(info,io.BytesIO(data))
   files.append({**{k:v for k,v in item.items() if k!='source'},'member':member,'sha256':hashlib.sha256(data).hexdigest(),'size':len(data),'mode':info.mode})
 archive=out/'payload.tar.gz';manifest={'schema':1,'version':spec['version'],'notes':spec.get('notes',''),'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'archive_size':archive.stat().st_size,'files':files}
 if 'native_setup' in spec:manifest['native_setup']=spec['native_setup']
 setup_descriptor(manifest)
 (out/'release.json').write_text(json.dumps(manifest,indent=2)+'\n')
 subprocess.run(['openssl','dgst','-sha256','-sign',str(key),'-out',str(out/'release.sig'),str(out/'release.json')],check=True)
 return out
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('spec');p.add_argument('--key',required=True);p.add_argument('--output',required=True);p.add_argument('--publish',metavar='OWNER/REPO');a=p.parse_args();out=build(a.spec,a.key,a.output)
 if a.publish:
  data=json.loads((out/'release.json').read_text());notes=out/'notes.md';notes.write_text(data['notes'])
  subprocess.run(['gh','release','create','v'+data['version'],'--repo',repo_name(a.publish),'--title','XPlasma '+data['version'],'--notes-file',str(notes),str(out/'release.json'),str(out/'release.sig'),str(out/'payload.tar.gz')],check=True)
 print(out)
