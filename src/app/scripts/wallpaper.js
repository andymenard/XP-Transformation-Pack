// SPDX-License-Identifier: GPL-3.0-or-later
// Every desktop's wallpaper: an image (${IMAGE}, positioned by ${FILL} when
// not null, over ${COLOUR}), or -- no image -- the solid colour.
desktops().forEach(function (d) {
    if (${IMAGE} === null) {
        d.wallpaperPlugin = 'org.kde.color';
        d.currentConfigGroup = ['Wallpaper', 'org.kde.color', 'General'];
        d.writeConfig('Color', ${COLOUR});
        return;
    }
    d.wallpaperPlugin = 'org.kde.image';
    d.currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General'];
    d.writeConfig('Image', ${IMAGE});
    if (${FILL} !== null)
        d.writeConfig('FillMode', ${FILL});
    d.writeConfig('Color', ${COLOUR});
});
print('ok');
