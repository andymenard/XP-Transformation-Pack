    // Versioned themes carry their shadow specification in the id. Changing
    // colors no longer recompiles the shared Aurorae QML engine.
    function xpShadow(name) {
        const spec = /^xplasma-v2-[0-9a-f]+-s([0-9a-f]{8})_([0-9a-f]{8})_(-?[0-9]+)_(-?[0-9]+)$/.exec(name);
        return spec ? {active: "#" + spec[1], inactive: "#" + spec[2], x: Number(spec[3]), y: Number(spec[4])}
                    : (root.xpTitleShadows || {})[name] || {};
    }
    // XP: a window with neither a minimize nor a maximize box -- a dialog --
    // shows neither button and no icon, only Close (and Help); with either,
    // both buttons show, the other disabled.
    function xpButtons(list) {
        if ((xpThemes.indexOf(auroraeTheme.themeName) < 0 && !auroraeTheme.themeName.startsWith("xplasma-v2-")) || decoration.client.minimizeable || decoration.client.maximizeable)
            return list;
        const out = [];
        for (let i = 0; i < list.length; ++i)
            if (list[i] !== DecorationOptions.DecorationButtonMinimize && list[i] !== DecorationOptions.DecorationButtonMaximizeRestore
                    && list[i] !== DecorationOptions.DecorationButtonMenu)
                out.push(list[i]);
        return out;
    }
