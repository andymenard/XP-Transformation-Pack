// SPDX-License-Identifier: GPL-3.0-or-later
// Every bottom panel as XP's taskbar: not floating, opaque, the full width,
// ${HEIGHT} pixels. Prints what each was.
var out = [];
// (None at the bottom -- removed, or moved to another edge: Plasma's own
// default panel there, for the layout to make XP's taskbar of.)
if (!panels().some(function (p) { return p.location === 'bottom'; })) {
    loadTemplate('org.kde.plasma.desktop.defaultPanel');
    var made = panels()[panels().length - 1];
    if (made && made.location !== 'bottom')
        made.location = 'bottom';
    out.push('added a bottom panel (there was none)');
}
panels().forEach(function (p) {
    if (p.location !== 'bottom')
        return;
    out.push('panel ' + p.id + ': floating=' + p.floating + ' height=' + p.height + ' opacity=' + p.opacity
             + ' alignment=' + p.alignment + ' lengthMode=' + p.lengthMode);
    p.floating = false;
    p.height = ${HEIGHT};
    p.opacity = 'opaque';
    p.alignment = 'left';
    p.lengthMode = 'fill';
});
print(out.join('\n'));
