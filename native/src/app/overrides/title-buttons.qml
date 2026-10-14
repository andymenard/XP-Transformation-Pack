    // XP: a window with neither a minimize nor a maximize box -- a dialog --
    // shows neither button and no icon, only Close (and Help); with either,
    // both buttons show, the other disabled.
    function xpButtons(list) {
        if (xpThemes.indexOf(auroraeTheme.themeName) < 0 || decoration.client.minimizeable || decoration.client.maximizeable)
            return list;
        const out = [];
        for (let i = 0; i < list.length; ++i)
            if (list[i] !== DecorationOptions.DecorationButtonMinimize && list[i] !== DecorationOptions.DecorationButtonMaximizeRestore
                    && list[i] !== DecorationOptions.DecorationButtonMenu)
                out.push(list[i]);
        return out;
    }
