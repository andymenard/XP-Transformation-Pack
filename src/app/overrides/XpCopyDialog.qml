// XPlasma: a file transfer as XP's "Copying..." dialog -- measured on XP's
// own (Luna, 96 dpi): shell32's animation (paper flying between folders:
// 161 copying, 160 moving, 162 deleting) across the top, the file and
// "From 'here' to 'there'" under it, the progress bar with Cancel beside it,
// and the time left. A window of its own, with the look's title bar and a
// taskbar button, as XP's was; closing it cancels, as XP's did.
import QtQuick
import QtQuick.Window

import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg

Window {
    id: dialog
    required property var popup

    readonly property var details: popup.jobDetails
    readonly property string kind: /mov/i.test(popup.summary) ? "move" : /delet|trash/i.test(popup.summary) ? "delete" : "copy"

    title: (popup.summary || "Copying") + "..."
    flags: Qt.Dialog | Qt.CustomizeWindowHint | Qt.WindowTitleHint | Qt.WindowCloseButtonHint   // (Close only, as XP's)
    width: 375
    height: 140
    minimumWidth: width
    maximumWidth: width
    minimumHeight: height
    maximumHeight: height
    visible: true
    // (the desktop's colours, not the panel's)
    SystemPalette {
        id: palette
    }
    color: palette.window

    onClosing: close => {
        close.accepted = false;
        if (popup.killable)
            popup.killRequested();
    }

    function baseName(path) {
        const s = (path || "").toString().replace(/\/+$/, "");
        return decodeURIComponent(s.substring(s.lastIndexOf("/") + 1));
    }
    function parentName(path) {
        const s = (path || "").toString().replace(/\/+$/, "");
        return baseName(s.substring(0, s.lastIndexOf("/")));
    }
    readonly property string source: details ? (details.descriptionValue1 || "") : ""
    readonly property string destination: details ? (details.descriptionValue2 || (details.effectiveDestUrl || "").toString()) : ""

    // The animation: its frames from the look's xp-notify art, 10 a second.
    KSvg.Svg {
        id: art
        imagePath: "widgets/xp-notify"
    }
    readonly property int frameCount: {
        let n = 0;
        while (n < 200 && art.hasElement("anim-" + kind + "-" + n))
            ++n;
        return n;
    }
    property int frame: 0
    Timer {
        interval: 100
        repeat: true
        running: dialog.visible && dialog.frameCount > 1
        onTriggered: dialog.frame = (dialog.frame + 1) % dialog.frameCount
    }
    KSvg.SvgItem {
        x: 7
        y: 2
        width: 272
        height: 60
        svg: art
        elementId: dialog.frameCount > 0 ? "anim-" + dialog.kind + "-" + dialog.frame : ""
    }

    Text {
        x: 10
        y: 64
        width: dialog.width - 20
        elide: Text.ElideMiddle
        text: dialog.baseName(dialog.source)
        font: Kirigami.Theme.defaultFont
        color: palette.windowText
        renderType: Text.NativeRendering
    }
    Text {
        x: 10
        y: 79
        width: dialog.width - 20
        elide: Text.ElideMiddle
        visible: dialog.source.length > 0 && dialog.destination.length > 0
        text: "From '" + dialog.parentName(dialog.source) + "' to '" + dialog.baseName(dialog.destination) + "'"
        font: Kirigami.Theme.defaultFont
        color: palette.windowText
        renderType: Text.NativeRendering
    }

    // The progress bar: the look's track, and its chunks up to the percentage.
    KSvg.FrameSvgItem {
        id: track
        x: 10
        y: 103
        width: cancel.x - 10 - 8
        height: 14
        imagePath: "widgets/xp-notify"
        prefix: "progress-track"
        KSvg.SvgItem {
            id: chunkArt
            visible: false
            svg: art
            elementId: "progress-chunk"
        }
        Item {
            id: chunks
            x: track.margins.left
            y: track.margins.top
            width: track.width - track.margins.left - track.margins.right
            height: track.height - track.margins.top - track.margins.bottom
            clip: true
            readonly property int chunkWidth: Math.max(1, chunkArt.naturalSize.width)
            readonly property int space: art.hasElement("hint-progress-space") ? art.elementSize("hint-progress-space").width : 2
            Row {
                spacing: chunks.space
                Repeater {
                    model: Math.ceil(chunks.width * Math.max(0, Math.min(100, dialog.popup.percentage)) / 100 / (chunks.chunkWidth + chunks.space))
                    KSvg.SvgItem {
                        width: chunks.chunkWidth
                        height: chunks.height
                        svg: art
                        elementId: "progress-chunk"
                    }
                }
            }
        }
    }
    XpButton {
        id: cancel
        x: dialog.width - width - 10
        y: 99
        text: "Cancel"
        enabled: dialog.popup.killable
        onClicked: dialog.popup.killRequested()
    }

    Text {
        x: 10
        y: 122
        text: {
            if (!dialog.details || !(dialog.details.speed > 0) || !(dialog.details.totalBytes > 0))
                return "";
            const seconds = Math.ceil((dialog.details.totalBytes - dialog.details.processedBytes) / dialog.details.speed);
            if (seconds < 60)
                return seconds + " Seconds Remaining";
            return Math.ceil(seconds / 60) + " Minutes Remaining";
        }
        font: Kirigami.Theme.defaultFont
        color: palette.windowText
        renderType: Text.NativeRendering
    }
}
