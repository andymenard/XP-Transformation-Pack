// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library
function text(color, mode, fallback) {
    if (mode === 1) return "#ffffff";
    if (mode === 2) return "#000000";
    // Readable text for the chosen surface, independently of original skin text.
    return (color.r * 0.299 + color.g * 0.587 + color.b * 0.114) < 0.5 ? "#ffffff" : "#000000";
}
function theme(base, live) {
    if (!live || !live.enabled) return base;
    const copy = Object.assign({}, base);
    copy.liveColors = live;
    const palette = Object.assign({}, base.palette);
    const color = Qt.color(live.menu);
    const label = text(color, live.startMenuText === undefined ? live.menuText : live.startMenuText, palette.programText);
    ["programText", "secondaryText", "placesText", "userText", "footerText", "submenuText"].forEach(function(key) { palette[key] = label; });
    palette.userShadow = label === "#ffffff" ? "#000000" : "#ffffff";
    palette.highlight = live.menu;
    palette.highlightText = label;
    palette.submenuHighlightText = label;
    copy.palette = palette;
    return copy;
}
