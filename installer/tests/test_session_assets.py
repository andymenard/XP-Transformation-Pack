import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class SessionArtworkTests(unittest.TestCase):
    @unittest.skipUnless((ROOT / 'payload/templates/session').exists(), 'Requires complete private installer payload')
    def test_every_referenced_dialog_sprite_is_bundled(self):
        folder = ROOT / 'payload/templates/session/contents/logout'
        names = set()
        for qml in folder.glob('*.qml'):
            names.update(re.findall(r'"(assets/[^"\n]+)"', qml.read_text()))
        self.assertIn('assets/14356.png', names)
        self.assertIn('assets/20150.png', names)
        for name in names:
            self.assertTrue((folder / name).is_file(), 'Missing artwork: ' + name)
            self.assertGreater((folder / name).stat().st_size, 0)

    @unittest.skipUnless((ROOT / 'payload/templates/session').exists(), 'Requires complete private installer payload')
    def test_log_off_hot_frame_keeps_its_outline(self):
        from PIL import Image
        image = Image.open(ROOT / 'payload/templates/session/contents/logout/assets/14356.png').convert('RGBA')
        normal = [image.getpixel((x, 3 * 32 + 31))[3] for x in range(32)]
        hot = [image.getpixel((x, 5 * 32 + 31))[3] for x in range(32)]
        self.assertEqual(hot, normal, 'Log Off hover frame is misaligned')


if __name__ == '__main__':
    unittest.main()
