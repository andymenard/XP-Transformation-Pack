// Desktops showing a wallpaper that was renamed (old URL -> new URL, in
// ${RENAMES}) show it by its new name.
var renames = ${RENAMES};
desktops().forEach(function (d) {
    if (d.wallpaperPlugin !== 'org.kde.image')
        return;
    d.currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General'];
    var image = d.readConfig('Image', '');
    if (renames[image] !== undefined)
        d.writeConfig('Image', renames[image]);
});
print('ok');
