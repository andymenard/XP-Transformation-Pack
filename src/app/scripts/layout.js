// The bottom panel as XP's taskbar:
//
//   [Start] [Quick Launch] [labelled task buttons ...] [tray] [clock]
//
// Idempotent. The Start button is XPLasma's plasmoid, given the look's data;
// it takes over the favourites and settings of whatever launcher had the
// slot (Kickoff, or xp2plasma's), which goes. Launchers pinned to an
// icons-only task manager move to Quick Launch; the task manager is the
// labelled one, never grouping; the pager and show-desktop go (XP had
// neither); the clock shows the time only. The order it wants is printed;
// Plasma's scripting can't reorder, so the caller writes it.
var START = ${START};
var THEME = ${THEME};
var LAUNCHER_KEYS = ['favorites', 'systemFavorites', 'primaryActions', 'favoritesPortedToKAstats',
                     'systemApplications', 'paneSwap', 'favoritesDisplay', 'applicationsDisplay',
                     'alphaSort', 'pin', 'showActionButtonCaptions', 'compactMode'];
var log = [];
var result = [];
panels().forEach(function (p) {
    if (p.location !== 'bottom')
        return;
    var byType = {};
    p.widgets().forEach(function (w) { (byType[w.type] = byType[w.type] || []).push(w); });
    var removed = {};
    function drop(w, what) { if (w) { removed[w.id] = true; w.remove(); log.push(what + ' removed'); } }

    // 1. The Start button.
    var start = (byType[START] || [])[0];
    var previous = (byType['org.xp2plasma.start'] || [])[0] || (byType['org.kde.plasma.kickoff'] || [])[0];
    if (!start) {
        start = p.addWidget(START);
        if (!start) { log.push('could not add ' + START + ' (Plasma hasn\'t found the package yet?)'); return; }
        log.push('added the Start button');
        if (previous) {
            previous.currentConfigGroup = ['General'];
            start.currentConfigGroup = ['General'];
            LAUNCHER_KEYS.forEach(function (k) {
                var v = previous.readConfig(k, null);
                if (v !== null && v !== '') start.writeConfig(k, v);
            });
            log.push('favourites and settings carried over from ' + previous.type);
        }
    }
    (byType['org.xp2plasma.start'] || []).forEach(function (w) { drop(w, 'xp2plasma\'s Start'); });
    (byType['org.kde.plasma.kickoff'] || []).forEach(function (w) { drop(w, 'Kickoff'); });
    start.currentConfigGroup = ['General'];
    start.writeConfig('theme', JSON.stringify(THEME));
    start.writeConfig('icon', '');   // (the Kickoff fork's; ours ignores it)

    // 2. Launchers pinned to an icons-only task manager -> Quick Launch.
    var icontasks = (byType['org.kde.plasma.icontasks'] || [])[0];
    var launchers = [];
    if (icontasks) {
        icontasks.currentConfigGroup = ['General'];
        var l = icontasks.readConfig('launchers', '');
        launchers = (typeof l === 'string') ? (l.length ? l.split(',') : []) : l;
    }
    var quick = (byType['org.kde.plasma.quicklaunch'] || [])[0];
    if (!quick) {
        quick = p.addWidget('org.kde.plasma.quicklaunch');
        quick.currentConfigGroup = ['General'];
        quick.writeConfig('launcherUrls', launchers);
        quick.writeConfig('maxSectionCount', 1);
        quick.writeConfig('showLauncherNames', false);
        quick.writeConfig('enablePopup', false);
        log.push('Quick Launch added with ' + launchers.length + ' launcher(s)');
    }

    // 3. Labelled task buttons, never grouped, no pinned launchers.
    var tasks = (byType['org.kde.plasma.taskmanager'] || [])[0];
    if (!tasks) {
        tasks = p.addWidget('org.kde.plasma.taskmanager');
        tasks.currentConfigGroup = ['General'];
        tasks.writeConfig('launchers', []);
        log.push('labelled task manager added');
    }
    tasks.currentConfigGroup = ['General'];
    tasks.writeConfig('groupingStrategy', 0);
    tasks.writeConfig('maxStripes', 1);
    tasks.writeConfig('taskMaxWidth', 0);
    tasks.writeConfig('showToolTips', true);
    drop(icontasks, 'the icons-only task manager');

    // 4. XP had no pager and no show-desktop button.
    ['org.kde.plasma.pager', 'org.kde.plasma.showdesktop'].forEach(function (t) {
        (byType[t] || []).forEach(function (w) { drop(w, t); });
    });

    // 5. The clock: the time only, in XP's clock font.
    (byType['org.kde.plasma.digitalclock'] || []).forEach(function (w) {
        w.currentConfigGroup = ['Appearance'];
        w.writeConfig('showDate', false);
        w.writeConfig('autoFontAndSize', false);
        w.writeConfig('fontFamily', ${CLOCK_FAMILY});
        w.writeConfig('fontSize', ${CLOCK_POINTS});
        w.writeConfig('fontWeight', ${CLOCK_WEIGHT});
        w.writeConfig('boldText', ${CLOCK_WEIGHT} >= 600);
        w.writeConfig('italicText', false);
        w.writeConfig('fontStyleName', '');
    });

    // 6. The order: Start, a pixel's gap, Quick Launch, the tasks, then the rest.
    var spacer = (byType['org.kde.plasma.panelspacer'] || [])[0] || p.addWidget('org.kde.plasma.panelspacer');
    spacer.currentConfigGroup = ['General'];
    spacer.writeConfig('expanding', false);
    spacer.writeConfig('length', 1);
    var head = [start.id, spacer.id, quick.id, tasks.id];
    var rest = p.widgets().map(function (w) { return w.id; })
        .filter(function (id) { return head.indexOf(id) < 0 && !removed[id]; });
    result.push({panel: p.id, order: head.concat(rest)});
});
print(JSON.stringify({log: log, panels: result}));
