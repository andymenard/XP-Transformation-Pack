"""Rearrange the bottom panel into the Windows XP taskbar layout:

    [start] [quick launch] [labelled task buttons ...] [tray] [clock]

Uses plasmashell's scripting API and is idempotent: launchers pinned in an
icons-only task manager migrate to a Quick Launch strip, the task manager
becomes the labelled one with no pinned launchers, pager / show-desktop go
away (XP had neither), the clock shows time only. Nothing is touched if
the panel is already in this shape.
"""
from __future__ import annotations

import json
import os
import time

import hostexec as host

START_SPACER_PX = 1


def apply_clock_font(family: str = "Tahoma", point_size: int = 8, bold: bool = False) -> str:
    """Set a fixed XP clock font without disturbing its timezone settings."""
    return _script(f"""
var ps = panels();
for (var i = 0; i < ps.length; i++) {{
  if (ps[i].location !== 'bottom') continue;
  ps[i].widgets('org.kde.plasma.digitalclock').forEach(function(w) {{
    w.currentConfigGroup = ['Appearance'];
    w.writeConfig('autoFontAndSize', false);
    w.writeConfig('fontFamily', {json.dumps(family)});
    w.writeConfig('fontSize', {int(point_size)});
    w.writeConfig('fontWeight', {700 if bold else 400});
    w.writeConfig('boldText', {'true' if bold else 'false'});
    w.writeConfig('italicText', false);
    w.writeConfig('fontStyleName', '');
    w.writeConfig('showDate', false);
  }});
}}
print('Clock font updated');
""")

# The system-wide entries here belong to the machine, not to this app, so they
# are probed through host.exists rather than os.path -- one place that records
# the distinction, in case this code ever runs from somewhere that does not see
# the real filesystem.
_APP_DIRS = [
    os.path.expanduser("~/.local/share/applications"),
    os.path.expanduser("~/.local/share/flatpak/exports/share/applications"),
    "/var/lib/flatpak/exports/share/applications",
    "/usr/local/share/applications",
    "/usr/share/applications",
]

# Resolved once per process. An apply asks about every launcher URL against
# every directory above, so without the cache that is dozens of stats per URL.
_desktop_file_cache: dict[str, str | None] = {}


def _script(js: str) -> str:
    return host.run(
        [host.resolve("qdbus6", "qdbus"),
         "org.kde.plasmashell", "/PlasmaShell", "org.kde.PlasmaShell.evaluateScript", js],
        check=True, capture=True,
    ).stdout.strip()


def _find_desktop_file(desktop_id: str) -> str | None:
    if not desktop_id:
        return None
    if desktop_id not in _desktop_file_cache:
        _desktop_file_cache[desktop_id] = next(
            (os.path.join(d, desktop_id) for d in _APP_DIRS
             if host.exists(os.path.join(d, desktop_id))), None)
    return _desktop_file_cache[desktop_id]


def resolve_launcher_url(url: str) -> str:
    """Task-manager launcher URLs (applications:foo.desktop, preferred://x)
    -> file:// paths that the Quick Launch widget can actually load."""
    if url.startswith("applications:"):
        path = _find_desktop_file(url[len("applications:"):].lstrip("/"))
        return "file://" + path if path else url
    if url.startswith("preferred://"):
        kind = url[len("preferred://"):]
        try:
            if kind == "browser":
                desktop_id = host.run(["xdg-settings", "get", "default-web-browser"],
                                      capture=True, check=True).stdout.strip()
            else:
                mime = {"filemanager": "inode/directory", "mailer": "x-scheme-handler/mailto"}.get(kind, "")
                desktop_id = host.run(["xdg-mime", "query", "default", mime],
                                      capture=True, check=True).stdout.strip() if mime else ""
        except host.HostCommandError:
            desktop_id = ""
        path = _find_desktop_file(desktop_id) if desktop_id else None
        return "file://" + path if path else url
    return url


def refresh_quicklaunch_urls() -> str:
    """Rewrite every Quick Launch widget's launcherUrls through resolve_launcher_url()."""
    raw = _script("""
var out = [];
var ps = panels();
for (var i = 0; i < ps.length; i++) {
  var ws = ps[i].widgets('org.kde.plasma.quicklaunch');
  for (var j = 0; j < ws.length; j++) {
    ws[j].currentConfigGroup = ['General'];
    var l = ws[j].readConfig('launcherUrls', '');
    out.push({ panel: ps[i].id, widget: ws[j].id, urls: (typeof l === 'string') ? (l.length ? l.split(',') : []) : l });
  }
}
print(JSON.stringify(out));
""")
    entries = json.loads(raw or "[]")
    log = []
    for e in entries:
        resolved = [resolve_launcher_url(u) for u in e["urls"]]
        if resolved != e["urls"]:
            _script(f"""
var ws = panelById({e['panel']}).widgets('org.kde.plasma.quicklaunch');
for (var j = 0; j < ws.length; j++) {{
  if (ws[j].id !== {e['widget']}) continue;
  ws[j].currentConfigGroup = ['General'];
  ws[j].writeConfig('launcherUrls', {json.dumps(resolved)});
}}
""")
            log.append(f"quick launch {e['widget']}: resolved {sum(1 for a, b in zip(e['urls'], resolved) if a != b)} launcher URL(s)")
    return "\n".join(log)


def set_start_icon(start_icon: str, start_applet: str = "org.kde.plasma.kickoff") -> str:
    """Point the existing Start launcher at a new pill image, without touching
    anything else in the panel. Every regenerated pill has a new content-hashed
    file name, so this has to run on every apply -- including the ones that
    skip the full XP layout -- or the panel keeps showing the previous image."""
    import time
    icon_js = json.dumps("file://" + os.path.abspath(start_icon))
    js = f"""
var n = 0;
panels().forEach(function (p) {{
  p.widgets().forEach(function (w) {{
    if (w.type === {json.dumps(start_applet)} || w.type === 'org.kde.plasma.kickoff') {{
      w.currentConfigGroup = ['General'];
      var cur = w.readConfig('icon', '');
      if (String(cur).indexOf('/xp2plasma/start-') !== -1 || w.type === {json.dumps(start_applet)}) {{
        w.writeConfig('icon', {icon_js}); n++;
      }}
    }}
  }});
}});
print(n + ' start icon(s) updated');
"""
    for _attempt in range(20):   # plasmashell may still be coming back up after a theme restart
        try:
            return _script(js)
        except host.HostCommandError:
            time.sleep(0.5)
    return "plasmashell not reachable; start icon not updated"


def apply_xp_layout(start_icon: str | None, start_applet: str = "org.kde.plasma.kickoff") -> str:
    # Kickoff only renders a non-square image at its real aspect ratio when
    # the icon setting is a file:// URL (its imageFallback branch).
    icon_js = json.dumps("file://" + os.path.abspath(start_icon)) if start_icon else "null"
    js = f"""
var log = [];
var result = [];
var START = {json.dumps(start_applet)};
var KICKOFF_KEYS = ['favorites', 'systemFavorites', 'primaryActions', 'favoritesPortedToKAstats',
                    'systemApplications', 'paneSwap', 'favoritesDisplay', 'applicationsDisplay',
                    'alphaSort', 'pin', 'showActionButtonCaptions', 'compactMode'];
var ps = panels();
for (var i = 0; i < ps.length; i++) {{
  var p = ps[i];
  if (p.location !== 'bottom') continue;
  var ws = p.widgets();
  var byType = {{}};
  for (var j = 0; j < ws.length; j++) {{ (byType[ws[j].type] = byType[ws[j].type] || []).push(ws[j]); }}

  // 1. Start button: the launcher applet (stock Kickoff, or our forked
  //    org.xp2plasma.start with hover/pressed pill states) with the XP pill
  //    as its icon. When switching to the fork, carry Kickoff's favorites
  //    and settings over and drop the stock one.
  var kickoff = (byType[START] || [])[0];
  var stock = (byType['org.kde.plasma.kickoff'] || [])[0];
  if (!kickoff) {{
    kickoff = p.addWidget(START);
    if (!kickoff) {{ log.push('could not add ' + START + ' (package not discovered yet?)'); continue; }}
    log.push('added ' + START);
    if (stock && START !== 'org.kde.plasma.kickoff') {{
      stock.currentConfigGroup = ['General'];
      kickoff.currentConfigGroup = ['General'];
      KICKOFF_KEYS.forEach(function (k) {{ var v = stock.readConfig(k, null); if (v !== null && v !== '') kickoff.writeConfig(k, v); }});
      log.push('copied Kickoff settings/favorites');
    }}
  }}
  // Widget ids stop being readable once a widget is removed, so collect the
  // ids to drop from the order *before* anything is removed.
  var removed = {{}};
  [icontasks, (START !== 'org.kde.plasma.kickoff') ? stock : null]
    .concat(byType['org.kde.plasma.pager'] || [], byType['org.kde.plasma.showdesktop'] || [])
    .forEach(function (w) {{ if (w) removed[w.id] = true; }});
  if (stock && START !== 'org.kde.plasma.kickoff') {{ stock.remove(); log.push('stock Kickoff removed'); }}
  kickoff.currentConfigGroup = ['General'];
  if ({icon_js} !== null) {{ kickoff.writeConfig('icon', {icon_js}); kickoff.writeConfig('menuLabel', ''); log.push('start icon set'); }}

  // 2. Launchers pinned in the icons-only task manager -> Quick Launch.
  var launchers = [];
  var icontasks = (byType['org.kde.plasma.icontasks'] || [])[0];
  if (icontasks) {{
    icontasks.currentConfigGroup = ['General'];
    var l = icontasks.readConfig('launchers', '');
    if (typeof l === 'string') {{ launchers = l.length ? l.split(',') : []; }} else {{ launchers = l; }}
  }}
  var quick = (byType['org.kde.plasma.quicklaunch'] || [])[0];
  if (!quick) {{
    quick = p.addWidget('org.kde.plasma.quicklaunch');
    quick.currentConfigGroup = ['General'];
    quick.writeConfig('launcherUrls', launchers);
    quick.writeConfig('maxSectionCount', 1);
    quick.writeConfig('showLauncherNames', false);
    quick.writeConfig('enablePopup', false);
    log.push('quick launch added with ' + launchers.length + ' launchers');
  }}

  // 3. Labelled task manager, no pinned launchers, no grouping (XP style).
  var tasks = (byType['org.kde.plasma.taskmanager'] || [])[0];
  if (!tasks) {{
    tasks = p.addWidget('org.kde.plasma.taskmanager');
    tasks.currentConfigGroup = ['General'];
    tasks.writeConfig('launchers', []);
    log.push('labelled task manager added');
  }}
  tasks.currentConfigGroup = ['General'];
  tasks.writeConfig('groupingStrategy', 0);   // XP never groups
  tasks.writeConfig('maxStripes', 1);
  tasks.writeConfig('taskMaxWidth', 0);       // Narrow: closest to XP's ~160px buttons
  tasks.writeConfig('showToolTips', true);
  if (icontasks) {{ icontasks.remove(); log.push('icons-only task manager removed'); }}

  // 4. XP has no pager and no show-desktop button.
  ['org.kde.plasma.pager', 'org.kde.plasma.showdesktop'].forEach(function (t) {{
    (byType[t] || []).forEach(function (w) {{ w.remove(); log.push('removed ' + t); }});
  }});

  // 5. Clock: time only.
  (byType['org.kde.plasma.digitalclock'] || []).forEach(function (w) {{
    w.currentConfigGroup = ['Appearance'];
    w.writeConfig('showDate', false);
  }});

  // 6. Desired order: start, spacer, quick launch, tasks, then whatever
  //    else is left (separator, tray, clock). Widget.index is a no-op in
  //    this Plasma, so the order is written to AppletOrder afterwards.
  var spacer = (byType['org.kde.plasma.panelspacer'] || [])[0];
  if (!spacer) {{
    spacer = p.addWidget('org.kde.plasma.panelspacer');
  }}
  spacer.currentConfigGroup = ['General'];
  spacer.writeConfig('expanding', false);
  spacer.writeConfig('length', {START_SPACER_PX});
  var head = [kickoff.id, spacer.id, quick.id, tasks.id];
  var rest = p.widgets().map(function (w) {{ return w.id; }})
    .filter(function (id) {{ return head.indexOf(id) < 0 && !removed[id]; }});
  result.push({{ panel: p.id, order: head.concat(rest) }});
}}
print(JSON.stringify({{ log: log, panels: result }}));
"""
    out = json.loads(_script(js))
    changes = []
    for entry in out["panels"]:
        wanted = ";".join(str(i) for i in entry["order"])
        current = host.run(
            ["kreadconfig6", "--file", "plasma-org.kde.plasma.desktop-appletsrc",
             "--group", "Containments", "--group", str(entry["panel"]), "--group", "General",
             "--key", "AppletOrder"],
            capture=True,
        ).stdout.strip()
        if current != wanted:
            changes.append((entry["panel"], wanted))
    if changes:
        # plasmashell writes its in-memory layout back to disk when it exits,
        # so the order must be written while it is stopped or it gets clobbered.
        host.run(["systemctl", "--user", "stop", "plasma-plasmashell.service"], check=False)
        for panel, order in changes:
            host.run(
                ["kwriteconfig6", "--file", "plasma-org.kde.plasma.desktop-appletsrc",
                 "--group", "Containments", "--group", str(panel), "--group", "General",
                 "--key", "AppletOrder", order],
                check=True,
            )
            out["log"].append(f"AppletOrder for panel {panel}: {order}")
        host.run(["systemctl", "--user", "start", "plasma-plasmashell.service"], check=False)
        out["log"].append("plasmashell restarted to apply the widget order")
        time.sleep(6)
    fixed = refresh_quicklaunch_urls()
    if fixed:
        out["log"].append(fixed)
    return "\n".join(out["log"])
