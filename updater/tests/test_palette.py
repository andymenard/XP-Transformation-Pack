# SPDX-License-Identifier: GPL-3.0-or-later
import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import gui

class PaletteTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):cls.app=gui.QtWidgets.QApplication.instance() or gui.QtWidgets.QApplication([])
 def test_light_xp_backgrounds_never_inherit_dark_foregrounds(self):
  QPalette=gui.QtGui.QPalette
  inherited=self.app.palette()
  for role in [QPalette.ColorRole.WindowText,QPalette.ColorRole.ButtonText,QPalette.ColorRole.Text]:
   inherited.setColor(role,gui.QtGui.QColor('white'))
  self.app.setPalette(inherited);gui.setup_appearance(self.app);palette=self.app.palette()
  for group in [QPalette.ColorGroup.Active,QPalette.ColorGroup.Inactive,QPalette.ColorGroup.Disabled]:
   for background,foreground in [('Window','WindowText'),('Button','ButtonText'),('Base','Text'),('ToolTipBase','ToolTipText')]:
    bg=palette.color(group,getattr(QPalette.ColorRole,background));fg=palette.color(group,getattr(QPalette.ColorRole,foreground))
    def lum(c):
     values=[v/12.92 if v<=0.04045 else ((v+0.055)/1.055)**2.4 for v in [c.redF(),c.greenF(),c.blueF()]]
     return sum(a*b for a,b in zip(values,[.2126,.7152,.0722]))
    self.assertGreaterEqual((lum(bg)+.05)/(lum(fg)+.05),3.0,(group,background))
