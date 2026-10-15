#!/usr/bin/env python3
"""Apply or restore the XP desktop behavior profile using KDE's config tools."""
import configparser
import json
from pathlib import Path
import subprocess
import sys
import shutil
DBUS = shutil.which("qdbus6") or shutil.which("qdbus-qt6") or "/usr/lib/qt6/bin/qdbus"

ROOT = Path(__file__).resolve().parents[2]
BACKUP = ROOT / 'behavior-backup'
HOME_DIR = Path.home()
# Keep KDE's gesture effects (overview, windowview, slide, windowaperture)
# available independently of XP's visual animation profile.
DISABLED = '''wobblywindows shakecursor startupfeedback squash scale maximize
fullscreen slidingpopups slidingnotifications blur translucency screenedge logout sessionquit
login fade fadedesktop eyeonscreen magiclamp glide fallapart sheet
blendchanges screentransform dimscreen dialogparent
tileseditor highlightwindow slideback mouseclick mousemark touchpoints trackmouse
diminactive'''.split()


def run(*args, capture=False):
    return subprocess.run(args, check=True, text=True,
                          stdout=subprocess.PIPE if capture else None)


def read_config(path):
    parser = configparser.RawConfigParser(strict=False, delimiters=('=',))
    parser.optionxform = str
    parser.read(path)
    return parser


def write(item, value):
    args = ['kwriteconfig6', '--file', item['file']]
    for group in item['groups']:
        args += ['--group', group]
    args += ['--key', item['key']]
    args += ['--delete'] if value is None else [str(value)]
    run(*args)


def reload_taskbar():
    # Re-read persisted settings in the running task manager without restarting it.
    run(DBUS, 'org.kde.plasmashell', '/PlasmaShell',
        'org.kde.PlasmaShell.evaluateScript',
        'panels().forEach(function(p) { p.widgets().forEach(function(w) {'
        'if (w.type === "org.kde.plasma.taskmanager") w.reloadConfig(); }); });')


def main():
    restoring = len(sys.argv) > 1 and sys.argv[1] == 'restore'
    manifest = BACKUP / 'settings.json'
    if restoring:
        items = json.loads(manifest.read_text())
        for item in items:
            write(item, item['previous'])
        run(DBUS, 'org.kde.KWin', '/KWin', 'reconfigure')
        for effect in (BACKUP / 'loaded-effects.txt').read_text().splitlines():
            run(DBUS, 'org.kde.KWin', '/Effects',
                'org.kde.kwin.Effects.loadEffect', effect)
        reload_taskbar()
        print('Previous desktop behavior restored; the current XP theme is retained.')
        return

    items = []

    def setting(file, groups, key, value):
        items.append(dict(file=file, groups=groups, key=key, desired=value))

    for effect in DISABLED:
        setting('kwinrc', ['Plugins'], effect + 'Enabled', 'false')
    setting('kwinrc', ['Plugins'], 'fadingpopupsEnabled', 'true')
    for key in ('AutoRaise',):
        setting('kwinrc', ['Windows'], key, 'false')
    for key in ('ElectricBorderMaximize', 'ElectricBorderTiling'):
        setting('kwinrc', ['Windows'], key, 'true')
    for key, value in [('BorderSnapZone', '30'), ('WindowSnapZone', '10'), ('CenterSnapZone', '0')]:
        setting('kwinrc', ['Windows'], key, value)
    setting('kwinrc', ['Windows'], 'FocusPolicy', 'ClickToFocus')
    for edge in ('Top', 'TopRight', 'Right', 'BottomRight', 'Bottom',
                 'BottomLeft', 'Left', 'TopLeft'):
        setting('kwinrc', ['ElectricBorders'], edge, 'None')
    for key, value in [('LayoutName', 'compact'), ('HighlightWindows', 'false'),
                       ('ShowOutline', 'false')]:
        setting('kwinrc', ['TabBox'], key, value)
    for key in ('BusyCursor', 'TaskbarButton'):
        setting('klaunchrc', ['FeedbackStyle'], key, 'false')
    for key in ('Bouncing', 'Blinking'):
        setting('klaunchrc', ['BusyCursorSettings'], key, 'false')

    config = read_config(HOME_DIR / '.config/plasma-org.kde.plasma.desktop-appletsrc')
    for section in config.sections():
        if config.get(section, 'plugin', fallback='') == 'org.kde.plasma.taskmanager':
            groups = section.split('][') + ['Configuration', 'General']
            for key in ('showToolTips', 'highlightWindows'):
                setting('plasma-org.kde.plasma.desktop-appletsrc', groups, key, 'false')

    saved = json.loads(manifest.read_text()) if manifest.exists() else []
    def identity(item):
        return item['file'], tuple(item['groups']), item['key']
    known = {identity(item): item for item in saved}
    for item in items:
        key = identity(item)
        if key in known:
            item['previous'] = known[key]['previous']
        else:
            original = read_config(BACKUP / item['file'])
            section = ']['.join(item['groups'])
            item['previous'] = original.get(section, item['key'], fallback=None)
            saved.append(item)
            known[key] = item
    manifest.write_text(json.dumps(saved, indent=2) + '\n')
    for item in items:
        write(item, item['desired'])
    run(DBUS, 'org.kde.KWin', '/KWin', 'reconfigure')
    for effect in DISABLED:
        run(DBUS, 'org.kde.KWin', '/Effects',
            'org.kde.kwin.Effects.unloadEffect', effect)
    run(DBUS, 'org.kde.KWin', '/Effects',
        'org.kde.kwin.Effects.loadEffect', 'fadingpopups')
    reload_taskbar()
    print('XP desktop behavior applied.')


if __name__ == '__main__':
    main()
