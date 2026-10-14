#!/usr/bin/python3
"""Build only the four XP widget plugins against this system's libraries."""
import hashlib,os,shutil,subprocess,sys,tarfile
from pathlib import Path
from system_profile import detect

def run(args):subprocess.run(list(map(str,args)),check=True,env={**os.environ,'QT_QPA_PLATFORM':'offscreen'})
def main():
 suite=Path(__file__).resolve().parent;build=Path(sys.argv[1]);jobs=sys.argv[2]
 series='.'.join(map(str,detect()['plasma'][:2]));cli=build/'runtime/bin/xplasma'
 info=subprocess.check_output([str(cli),'fork-info',series],text=True,env={**os.environ,'QT_QPA_PLATFORM':'offscreen'}).splitlines()
 root=build/'plasma'/series;pristine=root/'pristine';patched=root/'patched'
 pristine.mkdir(parents=True,exist_ok=True)
 sources={};targets={}
 for line in info:
  parts=line.split()
  if parts[0]=='source':
   _,project,version,expected=parts;sources[project]=version
   archive=suite/'source'/(project+'-'+version+'.tar.xz')
   if hashlib.sha256(archive.read_bytes()).hexdigest()!=expected:raise RuntimeError('Plasma source checksum mismatch: '+archive.name)
   if not (pristine/(project+'-'+version)).exists():
    with tarfile.open(archive) as t:t.extractall(pristine,filter='data')
  elif parts[0]=='target':targets.setdefault(parts[1],[]).append(parts[2])
 run([cli,'fork-sources',series,pristine,patched])
 out=build/'runtime/lib/xplasma/forks'/series/'plasma/applets';out.mkdir(parents=True,exist_ok=True)
 for project,names in targets.items():
  native=root/'build'/project
  run(['cmake','-S',patched/(project+'-'+sources[project]),'-B',native,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF'])
  run(['cmake','--build',native,'--parallel',jobs,'--target',*names])
  for name in names:
   plugin=native/'bin/plasma/applets'/(name+'.so')
   symbols=subprocess.check_output(['objdump','-T',str(plugin)],text=True)
   if 'PRIVATE_API' in symbols:raise RuntimeError('Widget unexpectedly uses Qt private API: '+name)
   shutil.copy2(plugin,out/plugin.name)
if __name__=='__main__':main()
