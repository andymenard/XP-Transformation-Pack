// SPDX-License-Identifier: GPL-3.0-or-later
// Every bottom panel as XP's taskbar: not floating, opaque, the full width,
// ${HEIGHT} pixels. Prints what each was.
var out = [];
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
