import importlib.util,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('login_dependencies',Path(__file__).resolve().parents[1]/'dependencies.py');deps=importlib.util.module_from_spec(spec);spec.loader.exec_module(deps)
class LoginDependencies(unittest.TestCase):
 def test_prebuilt_installs_still_include_welcome_status_bindings(self):
  for cached in (True,False):
   packages=deps.packages('apt',{'desktop','login'},native_ready=cached)
   self.assertIn('python3-pyside6.qtcore',packages)
   self.assertIn('python3-pyside6.qtdbus',packages)
 def test_login_dependency_not_required_for_desktop_only(self):
  self.assertNotIn('python3-pyside6.qtdbus',deps.packages('apt',{'desktop'},native_ready=True))
if __name__=='__main__':unittest.main()
