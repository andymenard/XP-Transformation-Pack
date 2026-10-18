import ast
import importlib.util
import json
import os
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('firefox_cleanup', ROOT/'firefox_cleanup.py')
f = importlib.util.module_from_spec(spec)
spec.loader.exec_module(f)


class FirefoxCleanupTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.home = Path(self.temp.name)
        self.entries = self.home/'.local/share/applications'
        self.calls = []
        self.backups = []
    def tearDown(self):
        self.temp.cleanup()
    def file(self, name, text=''):
        p = self.home/name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(text)
        return p
    def launcher(self, name, command):
        return self.file('.local/share/applications/'+name, '[Desktop Entry]\nType=Application\nName=Firefox\nExec='+command+'\n')
    def owned(self, name='userapp-Firefox-ABC.desktop'):
        return self.launcher(name, '"'+str(self.home/'.local/share/firefox-esr-ie6/firefox/firefox-bin')+'" %u')
    def cleanup(self, **kwargs):
        return f.cleanup(self.home, roots=[self.entries], execute=lambda argv:self.calls.append(argv) or True,
                         backup=lambda p:self.backups.append((p,p.read_bytes())), **kwargs)
    def test_generated_handler_and_stale_defaults_are_repaired(self):
        stale = self.owned()
        self.launcher('firefox.desktop','/usr/bin/firefox %u')
        mime = self.file('.config/mimeapps.list', '# Keep this comment\n[Default Applications]\ntext/html='+stale.name+'\nx-scheme-handler/http=firefox-esr-ie6.desktop;\nimage/png=view.desktop;\n[Added Associations]\ntext/html='+stale.name+';firefox.desktop;\n')
        report = self.cleanup()
        self.assertFalse(stale.exists())
        text = mime.read_text()
        self.assertIn('# Keep this comment',text)
        self.assertIn('text/html=firefox.desktop;',text)
        self.assertIn('x-scheme-handler/http=firefox.desktop;',text)
        self.assertIn('image/png=view.desktop;',text)
        self.assertNotIn(stale.name,text)
        self.assertEqual(len(report['removed_launchers']),1)
        self.assertEqual(len(self.backups),2)
        self.assertIn(['kbuildsycoca6','--noincremental'],self.calls)
    def test_restores_the_previous_valid_browser_before_fallback(self):
        self.owned();self.launcher('firefox.desktop','/usr/bin/firefox');self.launcher('my-browser.desktop','/usr/bin/falkon')
        self.file('.config/mimeapps.list','[Default Applications]\ntext/html=userapp-Firefox-ABC.desktop;\n')
        state=self.home/'state'
        self.file('state/backup/.config/mimeapps.list','[Default Applications]\ntext/html=my-browser.desktop;\n')
        self.cleanup(state=state)
        self.assertIn('text/html=my-browser.desktop;', (self.home/'.config/mimeapps.list').read_text())
    def test_preserves_regular_firefox_even_with_same_esr_id(self):
        real=self.launcher('firefox-esr.desktop','/usr/bin/firefox-esr %u')
        pins=self.file('.config/plasma-org.kde.plasma.desktop-appletsrc','[General]\nlaunchers=applications:firefox-esr.desktop,applications:firefox-esr-ie6.desktop,applications:dolphin.desktop,preferred://browser\n')
        self.cleanup()
        self.assertTrue(real.exists())
        self.assertIn('launchers=applications:firefox-esr.desktop,applications:dolphin.desktop,preferred://browser',pins.read_text())
    def test_pin_and_favourite_forms_are_filtered_exactly(self):
        self.owned()
        path=self.file('.config/plasma-org.kde.plasma.desktop-appletsrc', '[A]\nlauncherUrls='+ (self.entries/'userapp-Firefox-ABC.desktop').as_uri()+',file:///usr/share/applications/dolphin.desktop\nfavorites=firefox-esr-ie6.desktop,firefox-esr-ie6.desktop.extra,preferred://browser\n')
        self.cleanup()
        self.assertIn('launcherUrls=file:///usr/share/applications/dolphin.desktop',path.read_text())
        self.assertIn('favorites=firefox-esr-ie6.desktop.extra,preferred://browser',path.read_text())
    def test_leaves_working_restored_runtime_untouched(self):
        owned=self.owned();self.file('.local/share/firefox-esr-ie6/firefox/firefox','binary')
        report=self.cleanup()
        self.assertTrue(owned.exists());self.assertTrue(report['preserved_runtime']);self.assertEqual(self.calls,[])
    def test_symlinks_and_non_owned_shortcuts_are_preserved(self):
        self.owned();other=self.launcher('regular.desktop','/usr/bin/firefox --profile '+str(self.home/'.local/share/firefox-esr-ie6/profile'))
        outside=self.file('outside','[Default Applications]\ntext/html=userapp-Firefox-ABC.desktop;\n')
        (self.home/'.config').mkdir();(self.home/'.config/mimeapps.list').symlink_to(outside)
        self.cleanup()
        self.assertTrue(other.exists());self.assertIn('userapp-Firefox',outside.read_text())
    def test_unlinks_only_removed_favourites_via_dbus_not_sql_writes(self):
        database=self.file('.local/share/kactivitymanagerd/resources/database')
        with sqlite3.connect(database) as c:
            c.execute('CREATE TABLE ResourceLink (usedActivity TEXT,initiatingAgent TEXT,targettedResource TEXT)')
            rows=[(':global','org.kde.plasma.favorites.instance-3','applications:firefox-esr-ie6.desktop'),
                  (':global','org.kde.plasma.favorites.instance-3','applications:firefox.desktop'),
                  (':global','some-other-app','applications:firefox-esr-ie6.desktop')]
            c.executemany('INSERT INTO ResourceLink VALUES (?,?,?)',rows)
        before=database.read_bytes();report=self.cleanup()
        unlinks=[a for a in self.calls if a[0]=='qdbus6']
        self.assertEqual(len(unlinks),1)
        self.assertEqual(unlinks[0][-3:],['org.kde.plasma.favorites.instance-3','applications:firefox-esr-ie6.desktop',':global'])
        self.assertEqual(len(report['unlinked_favourites']),1);self.assertEqual(before,database.read_bytes())
    def test_second_cleanup_makes_no_further_file_changes(self):
        self.owned();self.launcher('firefox.desktop','/usr/bin/firefox')
        self.file('.config/mimeapps.list','[Default Applications]\ntext/html=userapp-Firefox-ABC.desktop;\n')
        self.cleanup();report=self.cleanup()
        self.assertEqual(report['changed_configs'],[]);self.assertEqual(report['removed_launchers'],[])
    def test_cache_uses_desktop_search_paths_without_importing_other_environment(self):
        reply=SimpleNamespace(returncode=0,stdout='XDG_DATA_DIRS=/usr/share:/var/lib/snapd/desktop\nXDG_MENU_PREFIX=plasma-\nUNRELATED_SECRET=never-import\n')
        with patch.dict(os.environ,{},clear=True),patch.object(f.subprocess,'run',return_value=reply):
            env=f.desktop_environment()
        self.assertEqual(env,{'XDG_DATA_DIRS':'/usr/share:/var/lib/snapd/desktop','XDG_MENU_PREFIX':'plasma-'})
    def test_uninstaller_flushes_shell_before_restore_and_refreshes_on_failure(self):
        tree=ast.parse((ROOT/'backend.py').read_text())
        node=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='uninstall')
        self.file('.local/share/kde-xp-suite/installation.json',json.dumps({'features':[]}))
        self.file('.local/share/kde-xp-suite/tracked.json','[]')
        events=[]
        class BrokenSnapshot:
            def __init__(self,*args):pass
            def restore(self):events.append('restore');raise RuntimeError('restore failed')
        ns={'Path':Path,'STATE_NAME':'kde-xp-suite','json':json,'phase':lambda _:None,
            'run':lambda args,**kw:events.append(args), 'Snapshot':BrokenSnapshot,
            'refresh':lambda *args:events.append('refresh')}
        exec(compile(ast.Module(body=[node],type_ignores=[]),'backend.py','exec'),ns)
        with patch.object(Path,'home',return_value=self.home),self.assertRaisesRegex(RuntimeError,'restore failed'):
            ns['uninstall']()
        self.assertEqual(events[-3:],[['systemctl','--user','stop','plasma-plasmashell.service'],'restore','refresh'])

if __name__=='__main__':unittest.main()
