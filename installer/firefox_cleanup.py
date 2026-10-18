#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Remove references to an uninstalled IE6 browser, preserving other browsers."""
import configparser
import os
import re
import shlex
import sqlite3
import subprocess
import tempfile
from pathlib import Path
from urllib.parse import unquote, urlparse

KNOWN_IDS = {'firefox-esr-ie6.desktop', 'firefox-esr.desktop'}
BROWSER_MIMES = {'text/html', 'application/xhtml+xml', 'x-scheme-handler/http',
                 'x-scheme-handler/https', 'x-scheme-handler/chrome'}
PIN_KEYS = {'launchers', 'launcherUrls', 'favorites', 'systemFavorites', 'systemApplications'}


def desktop_environment():
    # SSH lacks the graphical session's Snap/Flatpak search paths and menu
    # prefix. Rebuilding KSycoca without these can hide the regular browser.
    env = dict(os.environ)
    keys = {'XDG_DATA_DIRS', 'XDG_CONFIG_DIRS', 'XDG_MENU_PREFIX', 'XDG_CURRENT_DESKTOP'}
    try:
        result = subprocess.run(['systemctl', '--user', 'show-environment'],
                                capture_output=True, text=True, timeout=5, check=False)
        if result.returncode == 0:
            for line in result.stdout.splitlines():
                key, sep, value = line.partition('=')
                if sep and key in keys and not env.get(key):
                    env[key] = value
    except (OSError, subprocess.TimeoutExpired):
        pass
    return env


def read_ini(path):
    parser = configparser.RawConfigParser(strict=False, delimiters=('=',))
    parser.optionxform = str
    parser.read(path)
    return parser


def owned_launcher(path, browser):
    try:
        command = read_ini(path).get('Desktop Entry', 'Exec', fallback='')
        words = shlex.split(command)
        if words and Path(words[0]).name == 'env':
            words = words[1:]
            while words and '=' in words[0] and not words[0].startswith('/'):
                words.pop(0)
        return bool(words and Path(words[0]).is_absolute() and Path(words[0]).resolve().is_relative_to(browser.resolve()))
    except (OSError, ValueError, configparser.Error):
        return False


def cleanup(home, state=None, roots=None, execute=None, backup=None):
    home = Path(home)
    browser = home/'.local/share/firefox-esr-ie6'
    report = {'removed_launchers': [], 'changed_configs': [], 'unlinked_favourites': [], 'errors': []}
    # A restored pre-existing browser, or a preserved modified runtime, is live.
    if any((browser/'firefox'/name).is_file() for name in ['firefox', 'firefox-bin']):
        report['preserved_runtime'] = True
        return report
    entries = home/'.local/share/applications'
    roots = list(roots) if roots is not None else [entries, Path('/usr/local/share/applications'),
        Path('/usr/share/applications'), Path('/var/lib/snapd/desktop/applications'),
        home/'.local/share/flatpak/exports/share/applications', Path('/var/lib/flatpak/exports/share/applications')]
    environment = desktop_environment() if execute is None else None

    def safe(path):
        return not path.is_symlink() and path.resolve().is_relative_to(home.resolve())

    def run(argv):
        try:
            if execute is not None:
                return execute(argv)
            return subprocess.run(argv, capture_output=True, timeout=30, check=False, env=environment).returncode == 0
        except (OSError, subprocess.TimeoutExpired) as error:
            report['errors'].append(str(error))
            return False

    candidates = set(KNOWN_IDS)
    for path in sorted(entries.glob('*.desktop')):
        if safe(path) and owned_launcher(path, browser):
            candidates.add(path.name)
            if backup:
                backup(path)
            path.unlink()
            report['removed_launchers'].append(str(path))

    def valid(name):
        if Path(name).name != name or not name.endswith('.desktop'):
            return False
        return any((root/name).is_file() and not owned_launcher(root/name, browser) for root in roots)

    stale = {name for name in candidates if not valid(name)}

    def removed_reference(value):
        value = unquote(value.strip())
        if value.startswith('applications:'):
            value = value[len('applications:'):]
        if value in stale:
            return True
        if value.startswith('file://'):
            value = urlparse(value).path
        path = Path(value)
        return (path.is_absolute() and (path.is_relative_to(browser) or
                (path.parent == entries and path.name in stale)))

    preferred = next((name for name in ['firefox.desktop', 'org.mozilla.firefox.desktop',
        'firefox_firefox.desktop', 'firefox-esr.desktop', 'org.mozilla.firefox-esr.desktop',
        'chromium.desktop', 'chromium-browser.desktop', 'org.kde.falkon.desktop'] if valid(name)), None)

    def write(path, transform):
        if not path.is_file() or not safe(path):
            return
        old = path.read_text()
        new = transform(old)
        if old != new:
            if backup:
                backup(path)
            # Keep comments, KConfig group syntax, unrelated keys and file mode.
            with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(new)
            try:
                temporary.chmod(path.stat().st_mode & 0o777)
                os.replace(temporary, path)
            finally:
                temporary.unlink(missing_ok=True)
            report['changed_configs'].append(str(path))

    # Qt/KDE and GLib can use different MIME-association files. Firefox also
    # creates random userapp-Firefox-*.desktop handlers outside our manifest.
    mime_paths = set((home/'.config').glob('*mimeapps.list')) | set(entries.glob('*mimeapps.list'))
    for path in sorted(mime_paths):
        previous = read_ini(Path(state)/'backup'/path.relative_to(home)) if state else read_ini('/nonexistent')
        def transform(text):
            group = ''
            lines = []
            for line in text.splitlines(keepends=True):
                if line.strip().startswith('['):
                    group = line.strip().strip('[]')
                match = re.match(r'^([^#;=\s][^=]*?)(\s*=\s*)([^\r\n]*)(\r?\n)?$', line)
                if match and group in {'Default Applications', 'Added Associations', 'Removed Associations'}:
                    key, sep, value, ending = match.groups()
                    ids = value.split(';')
                    kept = [item for item in ids if item and item not in stale]
                    if any(item in stale for item in ids):
                        if group == 'Default Applications' and not any(valid(item) for item in kept):
                            old_ids = previous.get(group, key, fallback='').split(';')
                            fallback = next((item for item in old_ids if item and valid(item)), None)
                            if not fallback and (key in BROWSER_MIMES or key.startswith('application/x-extension-')):
                                fallback = preferred
                            if fallback:
                                kept.insert(0, fallback)
                        line = key + sep + ';'.join(kept) + (';' if kept else '') + (ending or '')
                lines.append(line)
            return ''.join(lines)
        write(path, transform)

    def pins(text):
        lines = []
        for line in text.splitlines(keepends=True):
            match = re.match(r'^(\w+)(\s*=\s*)([^\r\n]*)(\r?\n)?$', line)
            if match:
                key, sep, value, ending = match.groups()
                if key in PIN_KEYS:
                    values = value.split(',')
                    kept = [item for item in values if not removed_reference(item)]
                    if kept != values:
                        line = key + sep + ','.join(kept) + (ending or '')
                elif key == 'BrowserApplication' and removed_reference(value):
                    line = key + sep + (preferred or '') + (ending or '')
            lines.append(line)
        return ''.join(lines)
    for name in ['plasma-org.kde.plasma.desktop-appletsrc', 'plasmashellrc', 'kdeglobals', 'kickoffrc']:
        write(home/'.config'/name, pins)

    # Use KDE's public linking service so favourite models receive the change.
    # Read only the links table; never edit the live SQLite database directly.
    database = home/'.local/share/kactivitymanagerd/resources/database'
    if database.is_file():
        try:
            with sqlite3.connect(database.as_uri()+'?mode=ro', uri=True) as connection:
                rows = list(connection.execute('SELECT initiatingAgent,targettedResource,usedActivity FROM ResourceLink'))
            for agent, resource, activity in rows:
                if removed_reference(resource) and any(word in agent.lower() for word in ['favorites', 'kickoff', 'xplasma.start']):
                    if run(['qdbus6', 'org.kde.ActivityManager', '/ActivityManager/Resources/Linking',
                        'org.kde.ActivityManager.ResourcesLinking.UnlinkResourceFromActivity', agent, resource, activity]):
                        report['unlinked_favourites'].append(resource)
                    else:
                        report['errors'].append('Could not unlink favourite: '+resource)
        except sqlite3.DatabaseError as error:
            report['errors'].append(str(error))
    run(['update-desktop-database', str(entries)])
    run(['kbuildsycoca6', '--noincremental'])
    return report
