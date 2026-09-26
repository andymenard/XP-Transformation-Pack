/*
    The Start menu's right-click menu: an item's actions, as Plasma's
    application models offer them, in a real (QMenu) menu -- so it's drawn by
    the widget style, XPlasma's included. XpBackend decides what's in it.

    SPDX-License-Identifier: GPL-3.0-or-later
*/
import QtQuick
import org.kde.plasma.extras as PlasmaExtras

Item {
    id: root
    visible: false
    readonly property bool opened: menu.status === PlasmaExtras.Menu.Open
    signal triggered(var entry)

    PlasmaExtras.Menu {
        id: menu
        visualParent: null
        placement: PlasmaExtras.Menu.BottomPosedLeftAlignedPopup
    }

    // `entries`: {text, icon, actionId, actionArgument, type: "separator" |
    // "title", enabled, checkable, checked, subActions}.
    function show(entries, item, x, y) {
        menu.close();
        // (It deletes the items it had -- and with them their submenus.)
        menu.clearMenuItems();
        if (entries.length === 0) return;
        for (const e of entries) menu.addMenuItem(make(e, root));
        menu.visualParent = item;
        menu.open(x, y);
    }
    function make(entry, owner) {
        const item = itemComponent.createObject(owner, {entry: entry});
        if (entry.subActions && entry.subActions.length > 0) {
            const sub = menuComponent.createObject(item, {visualParent: item.action});
            for (const e of entry.subActions) sub.addMenuItem(make(e, sub));
        }
        return item;
    }

    Component {
        id: menuComponent
        PlasmaExtras.Menu {}
    }
    Component {
        id: itemComponent
        PlasmaExtras.MenuItem {
            required property var entry
            text: entry.text ?? ""
            icon: entry.icon ?? null
            separator: entry.type === "separator"
            section: entry.type === "title"
            enabled: entry.type !== "title" && (entry.enabled ?? true)
            checkable: entry.checkable ?? false
            checked: entry.checked ?? false
            onClicked: if (!entry.subActions || entry.subActions.length === 0) root.triggered(entry)
        }
    }
}
