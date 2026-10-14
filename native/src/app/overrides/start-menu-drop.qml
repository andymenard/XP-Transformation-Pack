    // XPlasma: something dragged out of XPlasma's Start menu lands as a
    // shortcut, as XP's did -- a link to its launcher (Plasma shows it with
    // the shortcut arrow; it runs as the installed one does, and follows it)
    // -- without KIO's Move/Copy/Link menu (Ctrl+Shift at the drop means
    // link to KIO).
    readonly property Component xpShortcutEvent: Component {
        QtObject {
            property QtObject mimeData
            property int x
            property int y
            property int buttons
            property int modifiers: Qt.ControlModifier | Qt.ShiftModifier
            property int proposedAction: Qt.LinkAction
            property int possibleActions: Qt.LinkAction
        }
    }
    function xpShortcutDrop(event) {
        const formats = event.mimeData ? event.mimeData.formats || [] : [];
        if (formats.indexOf("application/x-xplasma-favorite") < 0 || formats.indexOf("text/uri-list") < 0)
            return event;
        const shortcut = xpShortcutEvent.createObject(null, {mimeData: event.mimeData, x: event.x, y: event.y, buttons: event.buttons});
        Qt.callLater(() => shortcut.destroy());
        return shortcut;
    }
