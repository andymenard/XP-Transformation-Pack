// Every XPlasma Start button, given the look's data.
var n = 0;
panels().forEach(function (p) {
    p.widgets(${START}).forEach(function (w) {
        w.currentConfigGroup = ['General'];
        w.writeConfig('theme', JSON.stringify(${THEME}));
        ++n;
    });
});
print(n);
