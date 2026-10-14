#!/usr/bin/env python3
"""Audit installed KDE names, image visibility, aliases and actual Qt lookup."""
import argparse
import configparser
import json
import os
from pathlib import Path
from PIL import Image


def audit(theme, qt=True):
    theme = Path(theme).resolve()
    aliases = json.loads((theme / 'ALIAS-PROVENANCE.json').read_text())
    breeze = {p.stem for p in Path('/usr/share/icons/breeze').rglob('*.svg')}
    desktop_icons = set()
    for path in Path('/usr/share/applications').glob('*.desktop'):
        config = configparser.ConfigParser(interpolation=None, strict=False)
        try:
            config.read(path)
            entry = config['Desktop Entry']
            name = entry.get('Icon', '')
            if name and not name.startswith('/') and (path.name.startswith('org.kde.') or 'KDE;' in entry.get('Categories', '')):
                desktop_icons.add(name)
        except (KeyError, configparser.Error):
            continue
    errors = []
    directories = configparser.ConfigParser(interpolation=None, strict=False)
    directories.read(theme / 'index.theme')
    declared = directories.get('Icon Theme', 'Directories').split(',')
    images = set()
    for directory in declared:
        folder = theme / directory
        size = directories.getint(directory, 'Size')
        if not folder.is_dir():
            errors.append('Missing declared directory: ' + directory)
            continue
        for path in folder.glob('*.png'):
            resolved = path.resolve()
            if not resolved.is_relative_to(theme) or not resolved.is_file():
                errors.append('Invalid alias: ' + str(path.relative_to(theme)))
                continue
            if resolved in images:
                continue
            images.add(resolved)
            try:
                with Image.open(resolved) as image:
                    if image.size != (size, size) or image.convert('RGBA').getchannel('A').getbbox() is None:
                        errors.append('Wrong size or empty image: ' + str(path.relative_to(theme)))
            except OSError:
                errors.append('Unreadable image: ' + str(path.relative_to(theme)))
    missing_breeze = sorted(breeze - aliases.keys())
    missing_desktop = sorted(desktop_icons - aliases.keys())
    qt_failures = []
    if qt:
        os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
        try:
            from PySide6 import QtGui
        except ImportError:
            from PyQt6 import QtGui
        app = QtGui.QGuiApplication.instance() or QtGui.QGuiApplication([])
        QtGui.QIcon.setThemeSearchPaths([str(theme.parent)])
        QtGui.QIcon.setThemeName(theme.name)
        for name in sorted(aliases):
            icon = QtGui.QIcon.fromTheme(name)
            if icon.isNull() or icon.pixmap(16, 16).isNull() or icon.pixmap(48, 48).isNull():
                qt_failures.append(name)
    result = {'mapped_names': len(aliases), 'breeze_names': len(breeze),
              'kde_desktop_names': len(desktop_icons), 'unique_images_checked': len(images),
              'missing_breeze': missing_breeze, 'missing_kde_desktop': missing_desktop,
              'image_errors': errors, 'qt_lookup_failures': qt_failures}
    result['passed'] = not (missing_breeze or missing_desktop or errors or qt_failures)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('theme', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    result = audit(args.theme)
    text = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.write_text(text)
    print(text)
    raise SystemExit(0 if result['passed'] else 1)
