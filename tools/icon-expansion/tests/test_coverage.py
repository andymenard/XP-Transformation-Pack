import importlib.util,json,tempfile,unittest
from pathlib import Path
from PIL import Image
root=Path(__file__).resolve().parents[1]
def load(name,file):
 spec=importlib.util.spec_from_file_location(name,root/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
b=load('coverage_build','build.py');i=load('coverage_install','install.py')
class Coverage(unittest.TestCase):
 def test_network_places_is_not_wireless_connection(self):
  role,reason=b.role('network-workgroup')
  self.assertEqual(b.ROLES[role],'My Network Places')
  self.assertEqual(reason,'exact')

 def test_default_tray_roles_cover_both_symbolic_and_state_names(self):
  for name,value in [('camera-on-symbolic','camera-web'),('media-playback-paused-symbolic','media-playback-pause'),('input-caps-on-symbolic','input-keyboard'),('flightmode-on','network-disconnected'),('plasma-vault-unlocked','changes-allow'),('printer-paused','printer')]:self.assertEqual(b.role(name)[0],value)
  self.assertTrue(b.tray_variant('camera-off-symbolic')[0]);self.assertTrue(b.tray_variant('camera-on-symbolic')[1]);self.assertEqual(b.tray_variant('input-caps-on-symbolic')[2],'A')
 def test_tray_primary_aliases_refresh_but_original_xp_art_is_preserved(self):
  with tempfile.TemporaryDirectory() as d:
   home=Path(d);icons=home/'.local/share/icons';layer=icons/i.LAYER;main=icons/'xplasma-icons'
   for root in [layer,main]:(root/'16x16/status').mkdir(parents=True);(root/'index.theme').write_text('[Icon Theme]\nInherits=breeze\n')
   (layer/'ALIAS-PROVENANCE.json').write_text(json.dumps({'camera-on-symbolic':{'reason':'tray-state'},'update-none':{'reason':'tray-state'}}));(main/'xplasma-icons.json').write_text(json.dumps({'icons':{'update-none':{}},'pinned':{}}))
   for name in ['camera-on-symbolic','update-none']:
    Image.new('RGBA',(16,16),'red').save(layer/'16x16/status'/f'{name}.png');Image.new('RGBA',(16,16),'green').save(main/'16x16/status'/f'{name}.png')
   i.repair(home);self.assertEqual(Image.open(main/'16x16/status/camera-on-symbolic.png').getpixel((0,0)),(255,0,0,255));self.assertEqual(Image.open(main/'16x16/status/update-none.png').getpixel((0,0)),(0,128,0,255))
 def test_repeat_layer_install_replaces_symlink_aliases(self):
  with tempfile.TemporaryDirectory() as d:
   root=Path(d);source=root/'source';source.mkdir();(source/'a.png').write_bytes(b'a');(source/'b.png').write_bytes(b'b');(source/'name.png').symlink_to('a.png');home=root/'home'
   i.copy_layer(source,home);(source/'name.png').unlink();(source/'name.png').symlink_to('b.png');i.copy_layer(source,home)
   self.assertEqual((home/'.local/share/icons'/i.LAYER/'name.png').read_bytes(),b'b')
 def test_kde_roles_and_type_families(self):
  for name,role in [('org.kde.kate','accessories-text-editor'),('org.kde.konsole','utilities-terminal'),('org.kde.spectacle','accessories-screenshot-tool'),('org.kde.kdeconnect.app','phone'),('kcm_networkmanagement','preferences-system-network'),('media-playback-pause-symbolic','media-playback-pause')]:self.assertEqual(b.role(name)[0],role)
  self.assertEqual(b.role('application-vnd.oasis.opendocument.spreadsheet','mimetypes')[0],'application-spreadsheet')
  self.assertEqual(b.role('help-support')[0],'help-contents')
  self.assertEqual(b.role('CVnamespace')[0],None)
 def test_repair_survives_regeneration_and_preserves_uncovered_icons(self):
  with tempfile.TemporaryDirectory() as d:
   home=Path(d);icons=home/'.local/share/icons';layer=icons/i.LAYER;layer.mkdir(parents=True);(layer/'index.theme').write_text('[Icon Theme]');(layer/'ALIAS-PROVENANCE.json').write_text(json.dumps({'known':{}}))
   main=icons/'xplasma-icons';(main/'16x16/status').mkdir(parents=True);index=main/'index.theme';original='[Icon Theme]\nInherits=breeze,hicolor\n';index.write_text(original)
   for _ in range(3):
    index.write_text(original);(main/'16x16/status/known.svg').write_text('Breeze');(main/'16x16/status/unknown.svg').write_text('Meaningful unsupported state');i.repair(home)
    self.assertEqual(index.read_text().count(i.LAYER),1);self.assertFalse((main/'16x16/status/known.svg').exists());self.assertTrue((main/'16x16/status/unknown.svg').exists())
   self.assertEqual((home/'.local/share/xplasma/maintenance/icon-coverage/index.theme.before').read_text(),original)
 def test_battery_profile_preserves_native_capacity_and_charging_art(self):
  with tempfile.TemporaryDirectory() as d:
   home=Path(d);icons=home/'.local/share/icons';layer=icons/i.LAYER;alias='battery-050-charging-profile-performance-symbolic';(layer/'16x16/status').mkdir(parents=True);(layer/'index.theme').write_text('[Icon Theme]');(layer/'ALIAS-PROVENANCE.json').write_text(json.dumps({alias:{}}));Image.new('RGBA',(16,16),'red').save(layer/'16x16/status'/f'{alias}.png')
   main=icons/'xplasma-icons';(main/'16x16/status').mkdir(parents=True);(main/'index.theme').write_text('[Icon Theme]\nInherits=breeze\n');Image.new('RGBA',(16,16),'green').save(main/'16x16/status/battery-050-charging.png');i.repair(home)
   self.assertEqual(Image.open(layer/'16x16/status'/f'{alias}.png').getpixel((4,4)),(0,128,0,255))
if __name__=='__main__':unittest.main()

class ControlGlyphs(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  import os
  os.environ.setdefault('QT_QPA_PLATFORM','offscreen')
  from PySide6 import QtGui
  cls.application=QtGui.QGuiApplication.instance() or QtGui.QGuiApplication([])
 def test_submenu_alpha_is_triangle_not_solid_disc_and_rtl_mirrors(self):
  right=b.glyphs.render(b.glyphs.control_svg(b.glyphs.control('go-next-symbolic')),32,True)
  left=b.glyphs.render(b.glyphs.control_svg(b.glyphs.control('go-next-symbolic-rtl')),32,True)
  self.assertEqual(right.getpixel((16,4))[3],0)
  self.assertGreater(right.getpixel((16,16))[3],0)
  from PIL import ImageOps
  self.assertEqual(left.getchannel('A').tobytes(),ImageOps.mirror(right.getchannel('A')).tobytes())
 def test_specific_about_and_discover_categories_do_not_match_generic_back(self):
  for name,role in [('help-feedback','dialog-information'),('applications-science','applications-science'),('package-installed','software-installed'),('applications-accessibility','applications-accessibility')]:self.assertEqual(b.role(name)[0],role)
 def test_changed_mapping_replaces_previously_converted_primary_png(self):
  with tempfile.TemporaryDirectory() as d:
   home=Path(d);icons=home/'.local/share/icons';layer=icons/i.LAYER;main=icons/'xplasma-icons'
   for folder in [layer,main]:(folder/'16x16/actions').mkdir(parents=True);(folder/'index.theme').write_text('[Icon Theme]\nInherits=breeze\n')
   (layer/'ALIAS-PROVENANCE.json').write_text(json.dumps({'go-next-symbolic':{'reason':'control-glyph','control':b.glyphs.control('go-next-symbolic')}}))
   Image.new('RGBA',(16,16),'red').save(layer/'16x16/actions/go-next-symbolic.png')
   Image.new('RGBA',(16,16),'green').save(main/'16x16/actions/go-next-symbolic.png')
   i.repair(home)
   self.assertFalse((main/'16x16/actions/go-next-symbolic.png').exists())
   svg=(main/'16x16/actions/go-next-symbolic.svg').read_bytes()
   self.assertEqual(svg,b.glyphs.control_svg(b.glyphs.control('go-next-symbolic')))
   self.assertEqual(svg,(layer/'16x16/actions/go-next-symbolic.svg').read_bytes())
   self.assertIn(b'fill="currentColor"',svg)
   for theme in [main,layer]:self.assertIn('FollowsColorScheme=true',(theme/'index.theme').read_text())
   i.repair(home)
   self.assertEqual(svg,(main/'16x16/actions/go-next-symbolic.svg').read_bytes())

class LedgerTests(unittest.TestCase):
 def test_layer_update_tracks_links_and_preserves_unrelated_themes(self):
  with tempfile.TemporaryDirectory() as temp:
   home=Path(temp)/'home';source=Path(temp)/'source';source.mkdir()
   (source/'index.theme').write_text('[Icon Theme]');(source/'pixel.png').write_bytes(b'icon');(source/'alias.png').symlink_to('pixel.png')
   ledger=home/'.local/share/kde-xp-suite/after.json';ledger.parent.mkdir(parents=True)
   unrelated='.local/share/icons/user-custom/index.theme'
   ledger.write_text(json.dumps({'.local/share/icons/xplasma-icons/index.theme':{'kind':'file','value':'original'},unrelated:{'kind':'file','value':'keep'}}))
   i.copy_layer(source,home);data=json.loads(ledger.read_text())
   self.assertEqual(data['.local/share/icons/'+i.LAYER+'/alias.png'],{'kind':'link','value':'pixel.png'})
   self.assertEqual(data[unrelated]['value'],'keep')
