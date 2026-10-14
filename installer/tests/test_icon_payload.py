import json
import tarfile
import unittest
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


class IconPayloadTests(unittest.TestCase):
    @unittest.skipUnless((ROOT / 'payload/tools/icons.tar.gz').exists(), 'Requires complete installer payload')
    def test_audited_inventory_and_mask_safe_controls_are_bundled(self):
        with tarfile.open(ROOT / 'payload/tools/icons.tar.gz') as archive:
            prefix = 'xplasma-xp-extended/'
            report = json.load(archive.extractfile(prefix + 'AUDIT.json'))
            self.assertTrue(report['passed'])
            self.assertGreaterEqual(report['mapped_names'], 7390)
            aliases = json.load(archive.extractfile(prefix + 'ALIAS-PROVENANCE.json'))
            self.assertEqual(aliases['go-next-symbolic']['reason'], 'control-glyph')
            self.assertEqual(aliases['help-feedback']['role'], 'dialog-information')
            image = Image.open(archive.extractfile(prefix + '32x32/actions/go-next-symbolic.png')).convert('RGBA')
            self.assertEqual(image.getpixel((16, 4))[3], 0)
            self.assertGreater(image.getpixel((16, 16))[3], 0)
            self.assertTrue(archive.getmember(prefix + 'COPYING-KDE-ICONS').isfile())


if __name__ == '__main__':
    unittest.main()
