import ast
import shutil
import os
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ResolutionTests(unittest.TestCase):
    def test_wallpaper_originals_and_hires_are_independent(self):
        tree = ast.parse((ROOT / 'backend.py').read_text())
        functions = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in {'copy', 'prepare_wallpapers'}]
        backend = type('Backend', (), {})()
        ns = {'Path': Path, 'shutil': shutil, 'tempfile': tempfile, 'os': os, 'BASE': ROOT}
        exec(compile(ast.Module(body=functions, type_ignores=[]), 'backend.py', 'exec'), ns)
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp) / 'bundle'
            high = base / 'payload/assets/hires-wallpapers'
            high.mkdir(parents=True)
            (high / 'Bliss.jpg').write_bytes(b'hires')
            home = Path(temp) / 'home'
            target = home / '.local/share/xplasma/assets/wallpapers/Bliss.jpg'
            target.parent.mkdir(parents=True)
            target.write_bytes(b'original')
            ns['BASE'] = base
            if True:
                ns['prepare_wallpapers'](home, {'hidpi_icons': True, 'hires_wallpapers': False})
                self.assertEqual(target.read_bytes(), b'original')
                ns['prepare_wallpapers'](home, {'hidpi_icons': False, 'hires_wallpapers': True})
                self.assertEqual(target.read_bytes(), b'hires')
                self.assertEqual((high / 'Bliss.jpg').read_bytes(), b'hires')

    def test_every_stock_installer_preset_is_written_without_legacy_dark_assets(self):
        tree=ast.parse((ROOT/'backend.py').read_text())
        themes=next(node for node in tree.body if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='THEMES' for t in node.targets))
        function=next(node for node in tree.body if isinstance(node,ast.FunctionDef) and node.name=='presets')
        ns={'Path':Path,'THEMES':ast.literal_eval(themes.value)}
        exec(compile(ast.Module(body=[function],type_ignores=[]),'backend.py','exec'),ns)
        with tempfile.TemporaryDirectory() as home:
            folder=ns['presets'](Path(home))
            self.assertEqual(len(list(folder.glob('*.theme'))),len(ns['THEMES']))
            self.assertIn('Path=',(folder/'Zune.theme').read_text())
            self.assertNotIn('Zune Dark',ns['THEMES'])

    def test_icon_choice_persisted_and_large_sizes_registered(self):
        path = ROOT.parent / 'tools/icon-expansion/install.py'
        if not path.exists():path = ROOT / 'payload/tools/xp-icons.py'
        helper = load(path, 'resolution_icons')
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            source = base / 'source'
            (source / '128x128/places').mkdir(parents=True)
            (source / 'index.theme').write_text('[Icon Theme]\nDirectories=128x128/places\n')
            (source / 'ALIAS-PROVENANCE.json').write_text(json.dumps({'folder': {'reason': 'exact', 'context': 'places'}}))
            Image.new('RGBA', (128, 128), 'blue').save(source / '128x128/places/folder.png')
            for hidpi in (False, True):
                home = base / str(hidpi)
                primary = home / '.local/share/icons/xplasma-icons'
                (primary / '32x32/places').mkdir(parents=True)
                Image.new('RGBA', (32, 32), 'red').save(primary / '32x32/places/folder.png')
                (primary / 'index.theme').write_text('[Icon Theme]\nInherits=breeze\nDirectories=32x32/places\n')
                (primary / 'xplasma-icons.json').write_text(json.dumps({'icons': {'folder': {'file': 'shell32.dll'}}}))
                helper.copy_layer(source, home, hidpi=hidpi)
                helper.repair(home)
                self.assertEqual((primary / '128x128/places/folder.png').exists(), hidpi)
                self.assertEqual(Image.open(primary / '32x32/places/folder.png').getpixel((0, 0)), (255, 0, 0, 255))
                if hidpi:self.assertIn('[128x128/places]', (primary / 'index.theme').read_text())
                helper.copy_layer(source, home)  # OTA replacement keeps the saved choice.
                self.assertEqual(json.loads((home / '.local/share/icons/xplasma-xp-extended/RESOLUTION.json').read_text())['hidpi'], hidpi)


if __name__ == '__main__':unittest.main()
