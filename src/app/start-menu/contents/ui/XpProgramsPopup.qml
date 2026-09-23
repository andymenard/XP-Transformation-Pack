import QtQuick
import org.kde.kirigami as Kirigami
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.private.kicker as Kicker
import org.kde.kitemmodels as KItemModels

// Each flyout uses native Kicker screen-edge positioning and an actual KDE
// category model. Leaf actions retain the original model and stable identity.
Kicker.SubMenu {
    id: popup
    required property var themeData
    property var sourceModel: null
    property string title: "All Programs"
    property bool categoriesOnly: false
    property int menuOpenDelay: 400
    property var ownerPopup: null
    property var childPopup: null
    property var rows: []
    property int openIndex: -1
    property bool closing: false
    property bool ready: false
    property bool anchorHovered: false
    property bool pointerMode: false
    readonly property bool containsPointer: visible && (popupHover.hovered
        || (childPopup && childPopup.containsPointer))
    onContainsPointerChanged: if (containsPointer) pointerMode = true
    onAnchorHoveredChanged: if (visible && anchorHovered) pointerMode = true
    signal activated(var action)
    signal dismissRequested()
    signal contextRequested(var action, var item, real x, real y)
    // The right-click menu: while it's up, this losing focus to it closes nothing.
    property var contextMenu: null
    visible: false
    location: PlasmaCore.Types.Floating
    backgroundHints: PlasmaCore.Dialog.NoBackground
    // Treat the Start menu and its flyouts as one focus family. Returning to
    // an ancestor closes only the branch, not the entire launcher.
    hideOnWindowDeactivate: false
    offset: 0

    function role(row, name) {
        const id = sourceModel.KItemModels.KRoleNames.role(name);
        return id >= 0 ? sourceModel.data(sourceModel.index(row, 0), id) : null;
    }
    function rebuild() {
        if (!ready) return;
        closeChild();
        let result = [];
        if (sourceModel) {
            for (let row = 0; row < sourceModel.count; ++row) {
                const index = sourceModel.index(row, 0);
                const label = String(sourceModel.data(index, Qt.DisplayRole) || "");
                if (!label || role(row, "isSeparator") === true) continue;
                const child = sourceModel.modelForRow(row);
                // Kickoff's synthetic All Applications / Favorites entries are
                // not categories. Never filter on translated display labels.
                if (categoriesOnly && child && String(child.description).startsWith("KICKER_")) continue;
                const identity = String(role(row, "favoriteId") || role(row, "url") || label);
                result.push({label: label, icon: sourceModel.data(index, Qt.DecorationRole),
                    sourceRow: row, identity: identity, childModel: child,
                    action: {model: sourceModel, row: row, identity: identity}});
            }
        }
        rows = result;
        entries.currentIndex = -1;
    }
    function closeChild() {
        hoverTimer.stop();
        const old = childPopup;
        childPopup = null;
        openIndex = -1;
        if (old) {
            old.closing = true;
            old.visible = false;
            old.destroy();
        }
    }
    function openChild(index, keyboard) {
        const item = rows[index];
        if (!item || !item.childModel) return;
        if (openIndex === index && childPopup && childPopup.visible) {
            if (keyboard) childPopup.mainItem.forceActiveFocus();
            return;
        }
        closeChild();
        entries.positionViewAtIndex(index, ListView.Contain);
        const anchor = entries.itemAtIndex(index);
        if (!anchor) return;
        // Runtime creation avoids recursively instantiating this QML type.
        const component = Qt.createComponent(Qt.resolvedUrl("XpProgramsPopup.qml"));
        if (component.status !== Component.Ready) { console.warn(component.errorString()); return; }
        childPopup = component.createObject(popup, {
            themeData: themeData, sourceModel: item.childModel, title: item.label,
            ownerPopup: popup, visualParent: anchor, menuOpenDelay: menuOpenDelay, contextMenu: contextMenu
        });
        if (!childPopup) return;
        childPopup.activated.connect(action => popup.activated(action));
        childPopup.dismissRequested.connect(() => popup.dismissRequested());
        childPopup.contextRequested.connect((action, item, x, y) => popup.contextRequested(action, item, x, y));
        openIndex = index;
        childPopup.visible = true;
        if (keyboard) childPopup.mainItem.forceActiveFocus();
    }
    function activateRow(index, keyboard) {
        if (index < 0 || index >= rows.length) return;
        hoverTimer.stop();
        if (rows[index].childModel) openChild(index, keyboard);
        else activated(rows[index].action);
    }
    function back(keyboard = true) {
        closing = true;
        visible = false;
        if (ownerPopup) {
            ownerPopup.requestActivate();
            ownerPopup.mainItem.forceActiveFocus();
        } else if (visualParent) {
            const window = visualParent.Window.window;
            if (window) window.requestActivate();
            if (keyboard) visualParent.forceActiveFocus(Qt.BacktabFocusReason);
            else if (visualParent.parent) visualParent.parent.forceActiveFocus(Qt.MouseFocusReason);
        }
        closing = false;
    }
    function handleDeactivation() {
        if (closing || !visible || (childPopup && childPopup.visible) || (contextMenu && contextMenu.opened)) return;
        let ancestor = ownerPopup;
        let top = popup;
        while (ancestor) {
            if (ancestor.active) { ancestor.closeChild(); return; }
            top = ancestor;
            ancestor = ancestor.ownerPopup;
        }
        const startWindow = top.visualParent ? top.visualParent.Window.window : null;
        if (startWindow && startWindow.active) { top.back(false); return; }
        dismissRequested();
    }
    // Wait for Qt to mark the newly activated window before choosing what to close.
    onWindowDeactivated: Qt.callLater(handleDeactivation)
    onVisibleChanged: {
        if (visible) { closing = false; pointerMode = anchorHovered; rebuild(); }
        else closeChild();
    }
    onSourceModelChanged: rebuild()
    onCategoriesOnlyChanged: rebuild()
    mainItem: Item {
        id: content
        width: 250
        height: Math.min(420, Math.max(54, entries.count * 24 + 30))
        focus: true
        HoverHandler { id: popupHover }
        Timer {
            // Count all descendant flyouts as inside. A grace period allows
            // crossing gaps between windows; keyboard-only menus stay open.
            interval: popup.menuOpenDelay
            running: popup.visible && !popup.ownerPopup
                && popup.pointerMode && !popup.anchorHovered && !popup.containsPointer
                && !(popup.contextMenu && popup.contextMenu.opened)
            onTriggered: popup.back(false)
        }
        Connections {
            target: popup.sourceModel
            ignoreUnknownSignals: true
            function onModelReset() { popup.rebuild(); }
            function onRowsInserted() { popup.rebuild(); }
            function onRowsRemoved() { popup.rebuild(); }
            function onRowsMoved() { popup.rebuild(); }
            function onDataChanged() { popup.rebuild(); }
            function onLayoutChanged() { popup.rebuild(); }
        }
        Timer {
            id: hoverTimer
            property int row: -1
            interval: popup.menuOpenDelay
            onTriggered: {
                if (!popup.visible || row !== entries.currentIndex) return;
                if (popup.rows[row]?.childModel) popup.openChild(row, false);
                else popup.closeChild();
            }
        }
        BorderImage {
            anchors.fill: parent
            source: popup.themeData.parts.submenu ? popup.themeData.parts.submenu.url : ""
            border.left: popup.themeData.parts.submenu ? popup.themeData.parts.submenu.sizing[0] : 0
            border.right: popup.themeData.parts.submenu ? popup.themeData.parts.submenu.sizing[1] : 0
            border.top: popup.themeData.parts.submenu ? popup.themeData.parts.submenu.sizing[2] : 0
            border.bottom: popup.themeData.parts.submenu ? popup.themeData.parts.submenu.sizing[3] : 0
            smooth: false
            Rectangle { anchors.fill: parent; z: -1; color: "white"; border.color: "#1665cb" }
        }
        Text {
            x: 10; y: 6; width: parent.width - 20
            text: popup.title
            font.family: popup.themeData.font.family
            font.pixelSize: popup.themeData.font.pixels
            font.bold: true
            color: popup.themeData.palette.submenuText
            elide: Text.ElideRight
        }
        ListView {
            id: entries
            objectName: "programEntries"
            x: 6; y: 26; width: parent.width - 12; height: parent.height - y - 4
            clip: true
            model: popup.rows
            currentIndex: -1
            boundsBehavior: Flickable.StopAtBounds
            onMovementStarted: popup.closeChild()
            delegate: Rectangle {
                id: row
                objectName: "programRow-" + index
                required property int index
                required property var modelData
                readonly property bool selected: mouse.containsMouse || ListView.isCurrentItem
                    || (popup.openIndex === index && popup.childPopup && popup.childPopup.visible)
                width: entries.width
                height: 24
                color: selected ? popup.themeData.palette.highlight : "transparent"
                Accessible.role: modelData.childModel ? Accessible.ButtonMenu : Accessible.Button
                Accessible.name: modelData.label
                Accessible.onPressAction: popup.activateRow(index, true)
                Kirigami.Icon {
                    x: 3; anchors.verticalCenter: parent.verticalCenter
                    width: 16; height: 16
                    source: row.modelData.childModel ? "folder-programs" : row.modelData.icon
                    smooth: false
                }
                Text {
                    objectName: "programLabel"
                    x: 25; width: parent.width - x - 18
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.modelData.label
                    color: row.selected ? popup.themeData.palette.submenuHighlightText : popup.themeData.palette.submenuText
                    font.family: popup.themeData.font.family
                    font.pixelSize: popup.themeData.font.pixels
                    renderType: Text.NativeRendering
                    elide: Text.ElideRight
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 5
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !!row.modelData.childModel
                    text: "\u25b6"
                    font.pixelSize: 8
                    color: row.selected ? popup.themeData.palette.submenuHighlightText : popup.themeData.palette.submenuText
                }
                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: {
                        entries.currentIndex = row.index;
                        hoverTimer.row = row.index;
                        hoverTimer.restart();
                    }
                    onExited: if (hoverTimer.row === row.index) hoverTimer.stop()
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: mouse => {
                        if (mouse.button !== Qt.RightButton)
                            popup.activateRow(row.index, false);
                        else if (!row.modelData.childModel)
                            popup.contextRequested(row.modelData.action, row, mouse.x, mouse.y);
                    }
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: entries.count === 0
            text: "No items yet"
            color: popup.themeData.palette.submenuText
            font.family: popup.themeData.font.family
            font.pixelSize: popup.themeData.font.pixels
        }
        Keys.onEscapePressed: popup.back()
        Keys.onLeftPressed: popup.back()
        Keys.onRightPressed: popup.openChild(entries.currentIndex, true)
        Keys.onDownPressed: {
            popup.closeChild(); entries.incrementCurrentIndex();
            entries.positionViewAtIndex(entries.currentIndex, ListView.Contain);
        }
        Keys.onUpPressed: {
            popup.closeChild(); entries.decrementCurrentIndex();
            entries.positionViewAtIndex(entries.currentIndex, ListView.Contain);
        }
        Keys.onReturnPressed: popup.activateRow(entries.currentIndex, true)
        Keys.onMenuPressed: {
            const item = entries.itemAtIndex(entries.currentIndex);
            if (item && !popup.rows[entries.currentIndex].childModel)
                popup.contextRequested(popup.rows[entries.currentIndex].action, item, 0, item.height);
        }
        Keys.onSpacePressed: popup.activateRow(entries.currentIndex, true)
    }
    Component.onCompleted: { ready = true; rebuild(); }
}
