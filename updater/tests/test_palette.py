# SPDX-License-Identifier: GPL-3.0-or-later
import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import gui

class PaletteTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.app=gui.QtWidgets.QApplication.instance() or gui.QtWidgets.QApplication([])
 def test_light_and_dark_desktop_palettes_are_preserved(self):
  QPalette=gui.QtGui.QPalette
  for background,foreground in [('#ece9d8','#000000'),('#20242b','#eeeeee')]:
   palette=QPalette()
   for bg,fg in [('Window','WindowText'),('Button','ButtonText'),('Base','Text')]:
    palette.setColor(getattr(QPalette.ColorRole,bg),gui.QtGui.QColor(background))
    palette.setColor(getattr(QPalette.ColorRole,fg),gui.QtGui.QColor(foreground))
   self.app.setPalette(palette);gui.setup_appearance(self.app)
   label=gui.QtWidgets.QLabel('Desktop colors');button=gui.QtWidgets.QPushButton('Update')
   self.assertEqual(label.palette().color(QPalette.ColorRole.WindowText).name(),foreground)
   self.assertEqual(button.palette().color(QPalette.ColorRole.Button).name(),background)
   self.assertEqual(button.palette().color(QPalette.ColorRole.ButtonText).name(),foreground)
   # An already-open window must also respond to the next desktop change.
   changed=QPalette(palette);changed.setColor(QPalette.ColorRole.WindowText,gui.QtGui.QColor('#abcdef'))
   self.app.setPalette(changed);self.app.processEvents()
   self.assertEqual(label.palette().color(QPalette.ColorRole.WindowText).name(),'#abcdef')
 def test_wayland_identity_matches_launcher_and_has_an_icon(self):
  gui.setup_appearance(self.app)
  self.assertEqual(self.app.desktopFileName(),'xplasma-update')
  self.assertEqual(self.app.applicationName(),'xplasma-update')
  self.assertFalse(self.app.windowIcon().isNull())
