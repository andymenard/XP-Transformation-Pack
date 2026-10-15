import ast,io,json,shutil,tarfile,tempfile,unittest
from pathlib import Path
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
REPO=ROOT.parent
class FirefoxInstallTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.base=Path(self.temp.name);self.home=self.base/'home';self.home.mkdir()
  payload=self.base/'payload/firefox';profile=payload/'profile';(profile/'chrome/ie6-icons').mkdir(parents=True)
  (profile/'chrome/ie6-icons/application-ie.png').write_bytes(b'test-icon')
  (profile/'user.js').write_text('user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);\nuser_pref("browser.uiCustomization.state", "reset");\n')
  (profile/'initial-prefs.js').write_text('user_pref("browser.uiCustomization.state", "initial-toolbar");\n')
  (payload/'runtime-overrides').mkdir(parents=True)
  (payload/'runtime-overrides/ie6-ui.cfg').write_text('// current theme runtime\nfunction readAppearance() {}\n')
  icons=payload/'runtime-overrides/browser/chrome/icons/default';icons.mkdir(parents=True)
  for size in [16,32,48,64,128,256]:(icons/('default'+str(size)+'.png')).write_bytes(b'icon'+str(size).encode())
  helper=REPO/'browser/ie6/install-icon.py'
  if not helper.exists():helper=ROOT/'payload/firefox/install-icon.py'
  shutil.copy2(helper,payload/'install-icon.py')
  with tarfile.open(payload/'firefox-linux-arm64.tar.xz','w:xz') as archive:
   for name,data in [('firefox/firefox',b'not-run'),('firefox/ie6-ui.cfg',b'// obsolete runtime'),('firefox/application.ini',b'[App]\nRemotingName=firefox-esr\n')]:
    info=tarfile.TarInfo(name);info.size=len(data);archive.addfile(info,io.BytesIO(data))
  tree=ast.parse((ROOT/'backend.py').read_text());functions=[node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name in ['copy','firefox','firefox_profile_defaults']]
  self.ns={'BASE':self.base,'Path':Path,'tarfile':tarfile,'shutil':shutil,'tempfile':tempfile,'os':__import__('os'),'detect':lambda:{'arch':'arm64'}}
  exec(compile(ast.Module(body=functions,type_ignores=[]),'backend.py','exec'),self.ns)
 def tearDown(self):self.temp.cleanup()
 def install(self):
  with patch('shutil.which',return_value=None):self.ns['firefox'](self.home)
 def test_fresh_install_registers_real_application_identity_and_icon(self):
  self.install();entries=self.home/'.local/share/applications';primary=(entries/'firefox-esr-ie6.desktop').read_text();alias=(entries/'firefox-esr.desktop').read_text()
  self.assertIn('StartupWMClass=firefox-esr',primary);self.assertIn('application-ie.png',primary);self.assertNotIn('Icon=firefox\n',primary);self.assertIn('NoDisplay=true',alias)
  runtime=self.home/'.local/share/firefox-esr-ie6/firefox/ie6-ui.cfg'
  self.assertIn('readAppearance',runtime.read_text());self.assertNotIn('obsolete',runtime.read_text())
  profile=self.home/'.local/share/firefox-esr-ie6/profile'
  self.assertIn('initial-toolbar',(profile/'prefs.js').read_text());self.assertNotIn('browser.uiCustomization.state',(profile/'user.js').read_text())
 def test_existing_customization_survives_installation(self):
  profile=self.home/'.local/share/firefox-esr-ie6/profile';profile.mkdir(parents=True);saved='user_pref("browser.uiCustomization.state", "customized-toolbar");\n'
  (profile/'prefs.js').write_text(saved);self.install();self.assertEqual((profile/'prefs.js').read_text(),saved)
if __name__=='__main__':unittest.main()

class BundledBrowserTests(unittest.TestCase):
 def test_complete_installer_contains_live_dark_mode_configuration(self):
  payload=ROOT/'payload/firefox/runtime-overrides'
  if not payload.is_dir():self.skipTest('Source checkout without packaged payload')
  cfg=(payload/'ie6-ui.cfg').read_text()
  self.assertIn('function readAppearance()',cfg)
  self.assertIn("setInterval(()=>setDarkMode(win)",cfg)
  self.assertIn('general.config.sandbox_enabled',(payload/'defaults/pref/ie6-ui.js').read_text())
