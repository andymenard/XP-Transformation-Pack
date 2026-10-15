import importlib.util,sys,tempfile,unittest
from unittest.mock import patch
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(BASE))
spec=importlib.util.spec_from_file_location('suite_gui',BASE/'gui.py');g=importlib.util.module_from_spec(spec);sys.modules['suite_gui']=g;spec.loader.exec_module(g)
class GuiTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.app=g.QtWidgets.QApplication.instance() or g.QtWidgets.QApplication([])
 def setUp(self):
  self.mock=patch.object(g,'capabilities',return_value={f['id']:'' for f in g.FEATURES});self.mock.start()
  self.window=g.Installer(test_mode=True)
 def tearDown(self):self.mock.stop();self.window.close();self.window.deleteLater();self.app.processEvents()
 def test_all_components_are_selectable(self):
  self.window.select_all(True);self.assertEqual(set(self.window.selected()),{f['id'] for f in g.FEATURES})
  self.window.select_all(False);self.assertEqual(self.window.selected(),[]);self.assertFalse(self.window.install.isEnabled())
 def test_unsupported_feature_is_unchecked_and_cannot_select_all(self):
  self.mock.stop()
  with patch.object(g,'capabilities',return_value={f['id']:('Unsupported series' if f['id']=='taskbar' else '') for f in g.FEATURES}):window=g.Installer(test_mode=True)
  window.select_all(True);self.assertNotIn('taskbar',window.selected());self.assertIn('Unsupported series',window.items['taskbar'].toolTip(0));window.close()
 def test_browser_can_be_selected_without_desktop(self):
  self.window.select_all(False);self.window.items['firefox'].setCheckState(0,g.QtCore.Qt.CheckState.Checked)
  self.assertEqual(self.window.selected(),['firefox']);self.assertIn('No login or boot',self.window.summary.text())
 def test_theme_variants_and_system_change_notice(self):
  self.assertEqual(self.window.theme.count(),len(g.THEMES));self.assertGreaterEqual(self.window.theme.findText('Zune'),0)
  self.window.select_all(False);self.window.items['boot'].setCheckState(0,g.QtCore.Qt.CheckState.Checked)
  self.assertIn('authentication',self.window.summary.text())
 def test_preview_mode_cannot_execute_installation(self):
  self.window.start('install');self.assertIsNone(self.window.process)
 def test_live_output_is_visible_beside_progress_page(self):
  self.window.pages.setCurrentIndex(1);self.window.show();self.app.processEvents()
  self.window.append_log('Building component 1 of 4')
  self.assertTrue(self.window.terminal_log.isVisible())
  self.assertFalse(self.window.pages.isAncestorOf(self.window.terminal_log))
  self.assertIn('Building component 1 of 4',self.window.terminal_log.toPlainText())
  self.assertTrue(self.window.terminal_log.isReadOnly())
if __name__=='__main__':unittest.main()
