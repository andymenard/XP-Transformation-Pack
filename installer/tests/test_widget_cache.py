# SPDX-License-Identifier: GPL-3.0-or-later
import importlib.util,hashlib,json,os,sys,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
BASE=Path(__file__).resolve().parents[1];sys.path.insert(0,str(BASE))
spec=importlib.util.spec_from_file_location('widget_builder',BASE/'build-widgets.py');driver=importlib.util.module_from_spec(spec);spec.loader.exec_module(driver)

class WidgetCacheFlowTests(unittest.TestCase):
 def test_one_changed_widget_builds_alone_then_all_four_are_reused(self):
  with tempfile.TemporaryDirectory() as temporary:
   root=Path(temporary);suite=root/'suite';build=root/'build';cache=root/'cache';series='6.6'
   profile={'arch':'amd64','qt':(6,10,1),'plasma':(6,6,3)}
   recipes=[{'id':'org.kde.plasma.'+name,'project':project,'dir':directory} for name,project,directory in [('notifications','plasma-workspace','applets/notifications'),('systemtray','plasma-workspace','applets/systemtray'),('taskmanager','plasma-desktop','applets/taskmanager'),('panel','plasma-desktop','containments/panel')]]
   info=[];elf=bytearray(64);elf[:6]=b'\x7fELF\x02\x01';elf[18:20]=(62).to_bytes(2,'little');binary=root/'original.so';binary.write_bytes(elf)
   for project in ['plasma-workspace','plasma-desktop']:
    archive=suite/'source'/(project+'-6.6.4.tar.xz');archive.parent.mkdir(parents=True,exist_ok=True);archive.write_bytes(b'verified archive')
    info.append('source '+project+' 6.6.4 '+hashlib.sha256(archive.read_bytes()).hexdigest())
    (build/'plasma'/series/'pristine'/(project+'-6.6.4')).mkdir(parents=True)
    for widget in [w for w in recipes if w['project']==project]:
     info.append('target '+project+' '+widget['id'])
     source=build/'plasma'/series/'patched'/(project+'-6.6.4');file=source/widget['dir']/'main.qml';file.parent.mkdir(parents=True,exist_ok=True);file.write_text('original')
   config=build/'source/xplasma/src/app/overrides/forks.json';config.parent.mkdir(parents=True);config.write_text(json.dumps({series:{'widgets':recipes}}))
   calls=[]
   def run(args):
    args=list(map(str,args));calls.append(args)
    if args[:2]==['cmake','--build']:
     for name in args[args.index('--target')+1:]:
      output=Path(args[2])/'bin/plasma/applets'/(name+'.so');output.parent.mkdir(parents=True,exist_ok=True);output.write_bytes(elf)
   def output(args,**kwargs):return '\n'.join(info) if args[1]=='fork-info' else 'public symbols'
   with patch.dict(os.environ,{'XP_BUILD_CACHE':str(cache)}),patch.object(driver,'detect',return_value=profile),patch.object(driver.build_cache,'detect',return_value=profile),patch.object(driver.build_cache,'usable',return_value=True),patch.object(driver,'__file__',str(suite/'build-widgets.py')),patch.object(sys,'argv',['build-widgets.py',str(build),'2']),patch.object(driver,'run',side_effect=run),patch.object(driver.subprocess,'check_output',side_effect=output):
    for widget in recipes:
     source=build/'plasma'/series/'patched'/(widget['project']+'-6.6.4')
     expected=driver.build_cache.widget_identity(source,widget['dir'])
     relative='lib/xplasma/forks/'+series+'/plasma/applets/'+widget['id']+'.so'
     driver.build_cache.store_component(binary,relative,expected)
    changed=build/'plasma'/series/'patched/plasma-workspace-6.6.4/applets/notifications/main.qml';changed.write_text('contrast fix')
    driver.main()
    compiled=[c[c.index('--target')+1:] for c in calls if c[:2]==['cmake','--build']]
    self.assertEqual(compiled,[['org.kde.plasma.notifications']])
    calls.clear();driver.main()
    self.assertFalse(any(c[0]=='cmake' for c in calls))
    self.assertEqual(len(json.loads((build/'widget-components.json').read_text())),4)
