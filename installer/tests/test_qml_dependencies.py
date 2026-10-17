import importlib.util,unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location('qml_dependencies',Path(__file__).resolve().parents[1]/'dependencies.py');deps=importlib.util.module_from_spec(spec);spec.loader.exec_module(deps)
class QmlDependencies(unittest.TestCase):
 def test_every_widget_feature_brings_qtcore_qml(self):
  # The Start menu, clock, tray and session screens' QML import QtCore,
  # a package of its own on Debian and Ubuntu.
  for feature in ('taskbar','tray','lock','session'):
   for cached in (True,False):
    self.assertIn('qml6-module-qtcore',deps.packages('apt',{feature},native_ready=cached),feature)
if __name__=='__main__':unittest.main()
