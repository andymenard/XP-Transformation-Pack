import tempfile,unittest
from pathlib import Path
try:
 from PyQt6 import QtCore,QtGui,QtQuick,QtWidgets,QtTest
except ImportError:
 from PySide6 import QtCore,QtGui,QtQuick,QtWidgets,QtTest
ROOT=Path(__file__).resolve().parents[1]
class AccountPictureTests(unittest.TestCase):
 def test_real_account_image_and_missing_image_fallback(self):
  app=QtWidgets.QApplication.instance() or QtWidgets.QApplication([])
  component=ROOT/'payload/templates/lockscreen/AccountPicture.qml'
  if not component.exists():component=ROOT.parent/'themes/xp-lockscreen/AccountPicture.qml'
  with tempfile.TemporaryDirectory() as directory:
   folder=Path(directory)
   for name,color in [('user portrait.png','red'),('fallback.png','green'),('new portrait.png','blue')]:
    image=QtGui.QImage(48,48,QtGui.QImage.Format.Format_ARGB32);image.fill(QtGui.QColor(color));image.save(str(folder/name))
   view=QtQuick.QQuickView();view.resize(48,48);view.setSource(QtCore.QUrl.fromLocalFile(str(component)))
   self.assertNotEqual(view.status(),QtQuick.QQuickView.Status.Error,view.errors())
   root=view.rootObject();root.setProperty('fallbackSource',QtCore.QUrl.fromLocalFile(str(folder/'fallback.png')));view.show()
   for name,color in [('user portrait.png','red'),('new portrait.png','blue'),('missing.png','green'),('','green')]:
    root.setProperty('picturePath',str(folder/name) if name else '')
    QtTest.QTest.qWait(60)
    self.assertEqual(view.grabWindow().pixelColor(24,24),QtGui.QColor(color))
   view.close();view.deleteLater();app.processEvents()
