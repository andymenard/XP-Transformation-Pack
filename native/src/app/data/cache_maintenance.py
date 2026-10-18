#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bound disposable XPlasma data without touching theme sources or uninstall records."""
import argparse, configparser, fcntl, json, os, re, shutil, tempfile, time
from pathlib import Path

MIB = 1024 * 1024
LIMITS = {'looks': (16, 256*MIB), 'dark': (8, 128*MIB), 'backups': (5, 256*MIB)}
MARKER = '.xplasma-retention.json'
LOOK = re.compile(r'xplasma-v2-[A-Za-z0-9_-]+')
STAMP = re.compile(r'.+-\d{8}-\d{6}$')
HASH = re.compile(r'[a-f0-9]{64}$')


def safe(path, home):
    path = Path(path)
    return path.is_relative_to(home) and path.resolve().is_relative_to(home) and not any(p.is_symlink() for p in [path, *path.parents] if p.is_relative_to(home))


def read_json(path, home):
    try:
        if safe(path, home) and path.is_file():
            return json.loads(path.read_text())
    except (OSError, ValueError):
        pass
    return {}


def note_snapshot(home, folder, complete):
    home, folder = Path(home).resolve(), Path(folder).absolute()
    if folder.parent != home/'.local/share/xplasma/maintenance' or not safe(folder,home):
        return False
    marker = {'schema':1,'kind':'maintenance','complete':bool(complete)}
    if complete:
        marker['completed_at'] = time.time()
    with tempfile.NamedTemporaryFile(mode='w',dir=folder,prefix='.retention-',delete=False) as temp:
        json.dump(marker,temp)
        name = temp.name
    os.replace(name,folder/MARKER)
    return True


def footprint(path):
    if path.is_symlink():
        return 0
    if path.is_file():
        return path.stat().st_size
    size = 0
    for root, dirs, files in os.walk(path, followlinks=False):
        dirs[:] = [d for d in dirs if not (Path(root)/d).is_symlink()]
        for name in files:
            file = Path(root)/name
            if not file.is_symlink():
                try:
                    size += file.stat().st_size
                except FileNotFoundError:
                    pass
    return size


def entry(key, paths, protected=False):
    return {'key': key, 'paths': paths, 'bytes': sum(footprint(p) for p in paths),
            'used': max((p.stat().st_mtime for p in paths), default=0), 'protected': protected}


def trim(entries, limits, apply, home):
    count, size = len(entries), sum(e['bytes'] for e in entries)
    report = {'before_count': count, 'before_bytes': size, 'removed': [], 'errors': []}
    for item in sorted(entries, key=lambda e: (e['used'], e['key'])):
        if count <= limits[0] and size <= limits[1]:
            break
        if item['protected']:
            continue
        if any(not safe(p, home) for p in item['paths']):
            report['errors'].append('Unsafe path retained: '+item['key'])
            continue
        try:
            if apply:
                for path in item['paths']:
                    if path.is_dir():
                        shutil.rmtree(path)
                    elif path.exists():
                        path.unlink()
            count -= 1
            size -= item['bytes']
            report['removed'].append(item['key'])
        except OSError as error:
            report['errors'].append(str(error))
    report.update(after_count=count, after_bytes=size, limit_count=limits[0], limit_bytes=limits[1])
    return report


def collect_looks(home, state, keep):
    share = home/'.local/share'
    roots = [('aurorae/themes', ''), ('plasma/desktoptheme', '-taskbar'), ('color-schemes', '.colors'), ('themes', '')]
    entries, protected = [], []
    sources = state.get('settings', {}).get('generatedLooks', {})
    for ident in sources:
        if not LOOK.fullmatch(ident):
            continue
        paths = [share/root/(('XPlasma-' if root == 'themes' else '')+ident+suffix) for root, suffix in roots]
        paths += [share/'themes'/('XPlasma-'+ident+'-dark-applications'), share/'xplasma/start'/ident]
        paths += [home/'.cache'/('plasma_theme_'+ident+'-taskbar.kcache')]
        paths = [p for p in paths if p.exists() and safe(p, home)]
        if not paths:
            continue
        if ident in keep:
            protected.append(ident)
        else:
            entries.append(entry(ident, paths))
    return entries, protected


def collect_dark(home, keep):
    root = home/'.cache/xplasma/dark-mode'
    entries, protected = [], []
    if not safe(root, home) or not root.is_dir():
        return entries, protected
    for path in root.iterdir():
        # Unknown entries and linked folders are never cleanup candidates.
        if not HASH.fullmatch(path.name) or not path.is_dir() or not safe(path, home):
            continue
        if path.resolve() in keep:
            protected.append(path.name)
            continue
        if not read_json(path/'source.json', home).get('source'):
            continue
        entries.append(entry(path.name, [path]))
    return entries, protected


def valid_json(path, kind, home):
    try:
        return safe(path,home) and isinstance(json.loads(path.read_text()),kind)
    except (OSError,ValueError):
        return False


def recovery_references(home, root):
    protected = set()
    def scan(value):
        if isinstance(value,str):
            path = Path(value)
            if path.is_absolute() and path.is_relative_to(root) and path != root:
                protected.add(path.relative_to(root).parts[0])
        elif isinstance(value,dict):
            for item in value.values():scan(item)
        elif isinstance(value,list):
            for item in value:scan(item)
    for name in ['fedora-xp-installer','kde-xp-suite','kubuntu-xp-suite','xp-desktop-shell','xp-accessory-suite','xplasma-updater']:
        for file in ['installation.json','installed.json','rollback.json','config.json']:
            scan(read_json(home/'.local/share'/name/file,home))
    return protected


def collect_backups(home, adopt_legacy, keep_backups):
    root = home/'.local/share/xplasma/maintenance'
    entries, protected = [], []
    if not safe(root, home) or not root.is_dir():
        return entries, protected
    now = time.time()
    referenced = recovery_references(home,root)
    for path in root.iterdir():
        if not path.is_dir() or not safe(path, home):
            continue
        marker = read_json(path/MARKER, home)
        incomplete = (path/'before.json').exists() and not valid_json(path/'after.json',dict,home)
        recovery = path.name in referenced or any((path/name).exists() for name in ['.keep', 'installation.json', 'rollback.json'])
        structured = STAMP.fullmatch(path.name) and all(valid_json(path/name,kind,home) for name,kind in [('before.json',dict),('after.json',dict),('tracked.json',list)])
        legacy = adopt_legacy and STAMP.fullmatch(path.name) and now-path.stat().st_mtime > 900
        complete = marker.get('kind') == 'maintenance' and marker.get('complete') is True
        if recovery or incomplete or (marker and not complete) or not (complete or structured or legacy):
            protected.append(path.name)
            continue
        # Keep ongoing/recent transactions safe, including a pending rollback.
        held = str(path) in keep_backups or now-path.stat().st_mtime < 600
        entries.append(entry(path.name, [path], held))
    if entries:
        max(entries, key=lambda e:(e['used'],e['key']))['protected'] = True
    return entries, protected


def current_references(home, state, keep_looks, keep_dark):
    keep_looks = set(keep_looks)
    settings = state.get('settings', {})
    keep_looks.update(filter(None, [settings.get('appliedPackageId'), settings.get('startButtonLook')]))
    keep_dark = {Path(p).resolve() for p in keep_dark}
    configs = ['.config/plasmarc','.config/kdeglobals','.config/kwinrc',
               '.config/plasma-org.kde.plasma.desktop-appletsrc','.config/gtk-3.0/settings.ini',
               '.config/gtk-4.0/settings.ini','.config/xplasmarc','.local/share/xplasma/active/xplasmarc']
    for name in configs:
        path = home/name
        if not safe(path, home) or not path.is_file():
            continue
        text = path.read_text(errors='replace')
        for match in LOOK.findall(text):
            keep_looks.add(match.removesuffix('-dark-applications').removesuffix('-taskbar').removesuffix('-taskbar.kcache'))
        if name.endswith('xplasmarc'):
            config = configparser.ConfigParser(interpolation=None, strict=False)
            try:
                config.read_string(text)
                source = config.get('Theme','Source',fallback='')
                if source:
                    keep_dark.add(Path(source).parent.resolve())
            except configparser.Error:
                pass
    return keep_looks, keep_dark


def cleanup(home, apply=False, adopt_legacy=False, keep_looks=(), keep_dark=(), keep_backups=(), limits=None):
    home = Path(home).resolve()
    state = read_json(home/'.local/share/xplasma/state.json', home)
    keep_looks, keep_dark = current_references(home, state, keep_looks, keep_dark)
    limits = limits or LIMITS
    collections = {'looks': collect_looks(home,state,keep_looks), 'dark': collect_dark(home,keep_dark),
                   'backups': collect_backups(home,adopt_legacy,set(map(str,keep_backups)))}
    report = {'dry_run': not apply}
    for name,(entries,protected) in collections.items():
        report[name] = trim(entries,limits[name],apply,home)
        report[name]['protected'] = protected+[e['key'] for e in entries if e['protected']]
    return report


def locked_cleanup(home, **options):
    home = Path(home).resolve()
    lock_root = home/'.cache/xplasma'
    if not safe(lock_root, home):
        raise ValueError('Unsafe cache root')
    lock_root.mkdir(parents=True,exist_ok=True)
    fd = os.open(lock_root/'retention.lock',os.O_CREAT|os.O_RDWR|os.O_NOFOLLOW,0o600)
    with os.fdopen(fd,'w') as lock:
        try:
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:
            return {'busy': True}
        return cleanup(home, **options)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--home',type=Path,default=Path.home())
    parser.add_argument('--apply',action='store_true')
    parser.add_argument('--adopt-legacy',action='store_true',help='Include old timestamped development backups; incomplete/pinned backups remain protected')
    parser.add_argument('--keep-look',action='append',default=[])
    parser.add_argument('--keep-dark',action='append',default=[])
    args = parser.parse_args()
    report = locked_cleanup(args.home, apply=args.apply, adopt_legacy=args.adopt_legacy,
                            keep_looks=args.keep_look, keep_dark=args.keep_dark)
    print(json.dumps(report,indent=2))

if __name__ == '__main__':
    main()
