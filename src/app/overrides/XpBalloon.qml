// XPlasma: a notification as XP's balloon -- measured on XP's own (Luna,
// Security Center): the tooltip colours, a 1px frame round a box with 7px
// corners, a stem from its bottom edge (a 2px slant down to the tip, a
// straight right side) to the sender's icon in the tray; XP's severity icon
// at (13, 7), the title in bold at 37, the text from 29 in 13px lines, the
// close box 2px in at the top right. Under the text, what XP didn't have:
// the sender's picture, a file, buttons, a reply box.
import QtQuick
import QtQuick.Controls as QQC2

import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg
import org.kde.kquickcontrolsaddons as KQCAddons
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.private.notifications as NotificationsApplet

import "global"

PlasmaCore.Dialog {
    id: balloon
    required property var popup

    type: PlasmaCore.Dialog.Notification
    location: PlasmaCore.Types.Floating
    backgroundHints: PlasmaCore.Dialog.NoBackground
    flags: replyField.activeFocus ? Qt.Tool : Qt.WindowDoesNotAcceptFocus
    visible: popup.shown && popup.wantsBalloon
    onVisibleChanged: if (visible) Qt.callLater(place)

    readonly property int stemHeight: 18
    readonly property int stemWidth: 16
    readonly property int maxTextWidth: 300
    property int stemX: body.width - 13   // the stem's right side, from the box's left
    property bool stemUp: false           // (a panel at the top: the stem goes up)

    // The tip at the sender's tray icon; the box as far right as the
    // screen allows, like XP's, and the stem wherever that leaves the tip.
    function tipAt() {
        const icon = Globals.xpIcons[popup.notificationId] || null;
        const screen = Globals.screenRect;
        if (icon && icon.Window.window) {
            const g = icon.mapToGlobal(icon.width / 2, 0);
            const up = g.y < screen.y + screen.height / 2;
            const tip = up ? icon.mapToGlobal(icon.width / 2, icon.height) : g;
            return {x: Math.round(tip.x), y: Math.round(tip.y), up: up};
        }
        return {x: screen.x + screen.width - 40, y: screen.y + screen.height, up: false};
    }
    property var placedTip: null
    function place() {
        const screen = Globals.screenRect;
        const tip = tipAt();
        placedTip = tip;
        stemUp = tip.up;
        const w = content.width, h = content.height;
        let x = tip.x - (body.width - 13);
        x = Math.max(screen.x + 2, Math.min(x, screen.x + screen.width - w - 2));
        stemX = Math.max(stemWidth + 8, Math.min(tip.x - x, body.width - 8));
        balloon.x = Math.round(x);
        balloon.y = Math.round(stemUp ? tip.y : tip.y - h);
    }
    mainItem: Item {
        id: content
        width: body.width + 1
        height: body.height + balloon.stemHeight + 1

        Connections {
            target: Globals
            function onXpIconsChanged() { if (balloon.visible) Qt.callLater(balloon.place) }
        }
        // ...and after it, wherever the tray moves it (icons coming and
        // going beside it, the panel laying out again).
        Timer {
            interval: 100
            repeat: true
            running: balloon.visible
            onTriggered: {
                const tip = balloon.tipAt();
                const was = balloon.placedTip;
                if (!was || tip.x !== was.x || tip.y !== was.y || tip.up !== was.up)
                    balloon.place();
            }
        }

        // How long it stays, as Plasma's popups: Plasma's default, the
        // notification's own time, or (0) until closed.
        Timer {
            id: timeout
            interval: popup.timeout === -1 ? Globals.notificationSettings.popupTimeout : popup.timeout
            running: balloon.visible && interval > 0 && !hovering.hovered && !replyField.activeFocus && replyField.text.length === 0
            onTriggered: popup.expireRequested()
        }

        HoverHandler {
            id: hovering
        }
        MouseArea {
            // The balloon itself: its default action, or just read and gone.
            id: hover
            anchors.fill: body
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.MiddleButton
            cursorShape: popup.hasDefaultAction ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: mouse => {
                popup.readRequested();
                if (mouse.button === Qt.LeftButton && popup.hasDefaultAction)
                    popup.defaultActionRequested();
                else
                    popup.closeRequested();
            }
        }

        // The desktop's tooltip colours (XP's InfoWindow, InfoText), from the
        // application's palette -- not the panel's, which Kirigami's are.
        Rectangle {
            id: body
            y: balloon.stemUp ? balloon.stemHeight : 0
            width: Math.max(titleRow.width + 60, column.width + 24)
            height: column.y + column.height + 8
            radius: 7
            antialiasing: false
            color: balloon.palette.toolTipBase
            border.color: "black"
            border.width: 1

            KSvg.SvgItem {
                id: severityIcon
                x: 13
                y: 7
                width: 16
                height: 16
                svg: KSvg.Svg { imagePath: "widgets/xp-notify" }
                elementId: "icon-" + popup.severity
            }
            Text {
                id: titleRow
                x: 37
                y: 8
                text: popup.summary
                width: Math.min(implicitWidth, balloon.maxTextWidth - 40)
                elide: Text.ElideRight
                textFormat: Text.PlainText
                font.family: Kirigami.Theme.defaultFont.family
                font.pointSize: Kirigami.Theme.defaultFont.pointSize
                font.bold: true
                color: balloon.palette.toolTipText
                renderType: Text.NativeRendering
            }
            KSvg.SvgItem {
                id: closeBox
                anchors.right: parent.right
                anchors.rightMargin: 2
                y: 2
                width: naturalSize.width
                height: naturalSize.height
                svg: KSvg.Svg { imagePath: "widgets/xp-notify" }
                elementId: closeMouse.pressed ? "close-pressed" : closeMouse.containsMouse ? "close-hover" : "close-normal"
                MouseArea {
                    id: closeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        popup.readRequested();
                        popup.closeRequested();
                    }
                }
            }

            Column {
                id: column
                x: 12
                y: 29
                spacing: 6
                width: Math.max(bodyText.visible ? bodyText.width : 0, picture.visible ? picture.width : 0,
                                file.visible ? file.implicitWidth : 0, buttons.visible ? buttonRow.implicitWidth : 0,
                                reply.visible ? reply.implicitWidth : 0)

                Text {
                    id: bodyText
                    visible: text.length > 0
                    width: Math.min(implicitWidth, balloon.maxTextWidth)
                    text: popup.body
                    textFormat: Text.StyledText
                    wrapMode: Text.Wrap
                    maximumLineCount: 8
                    elide: Text.ElideRight
                    lineHeightMode: Text.FixedHeight
                    lineHeight: 13
                    font: Kirigami.Theme.defaultFont
                    color: balloon.palette.toolTipText
                    linkColor: "#0000ff"
                    renderType: Text.NativeRendering
                    onLinkActivated: link => {
                        popup.readRequested();
                        Qt.openUrlExternally(link);
                    }
                }

                // The sender's picture, framed.
                Rectangle {
                    id: picture
                    visible: popup.image !== null && popup.image !== undefined
                    width: pictureIcon.width + 2
                    height: pictureIcon.height + 2
                    color: "black"
                    Kirigami.Icon {
                        id: pictureIcon
                        x: 1
                        y: 1
                        width: 96
                        height: 72
                        source: picture.visible ? popup.image : ""
                    }
                }

                // A file: its thumbnail, Open and Open Folder.
                Row {
                    id: file
                    visible: (popup.urls || []).length > 0
                    spacing: 12
                    NotificationsApplet.Thumbnailer {
                        id: thumbnailer
                        url: file.visible ? popup.urls[0] : ""
                        size: Qt.size(96, 96)
                    }
                    Rectangle {
                        width: 98
                        height: 74
                        color: "black"
                        KQCAddons.QPixmapItem {
                            x: 1
                            y: 1
                            width: 96
                            height: 72
                            pixmap: thumbnailer.pixmap
                            smooth: true
                            fillMode: KQCAddons.QPixmapItem.PreserveAspectFit
                            visible: thumbnailer.hasPreview
                        }
                        Kirigami.Icon {
                            anchors.centerIn: parent
                            width: 48
                            height: 48
                            source: thumbnailer.iconName
                            visible: !thumbnailer.hasPreview
                        }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6
                        XpButton {
                            text: i18nd("plasma_applet_org.kde.plasma.notifications", "Open")
                            onClicked: {
                                popup.readRequested();
                                Qt.openUrlExternally(popup.urls[0]);
                                popup.closeRequested();
                            }
                        }
                        XpButton {
                            text: "Open Folder"
                            onClicked: {
                                popup.readRequested();
                                const url = popup.urls[0].toString();
                                Qt.openUrlExternally(url.substring(0, url.lastIndexOf("/") + 1));
                                popup.closeRequested();
                            }
                        }
                    }
                }

                // The sender's buttons, right-aligned.
                Item {
                    id: buttons
                    readonly property var labels: {
                        const out = [];
                        for (let i = 0; i < (popup.actionNames || []).length; ++i)
                            if (popup.actionNames[i] !== "default")
                                out.push({name: popup.actionNames[i], label: popup.actionLabels[i]});
                        return out;
                    }
                    visible: labels.length > 0
                    width: column.width
                    height: buttonRow.implicitHeight
                    Row {
                        id: buttonRow
                        anchors.right: parent.right
                        spacing: 6
                        Repeater {
                            model: buttons.labels
                            XpButton {
                                required property var modelData
                                text: modelData.label
                                onClicked: {
                                    popup.readRequested();
                                    popup.actionRequested(modelData.name);
                                }
                            }
                        }
                    }
                }

                // A reply, when the sender takes one.
                Row {
                    id: reply
                    visible: popup.hasReplyAction
                    spacing: 6
                    QQC2.TextField {
                        id: replyField
                        width: 214
                        height: 23
                        placeholderText: popup.replyPlaceholderText || "Reply…"
                        font: Kirigami.Theme.defaultFont
                        color: "black"
                        leftPadding: 4
                        background: KSvg.FrameSvgItem {
                            imagePath: "widgets/xp-notify"
                            prefix: "edit"
                        }
                        onAccepted: send.clicked()
                        onPressed: balloon.requestActivate()
                    }
                    XpButton {
                        id: send
                        text: popup.replySubmitButtonText || "Send"
                        onClicked: {
                            if (replyField.text.length === 0)
                                return;
                            popup.readRequested();
                            popup.replyRequested(replyField.text);
                        }
                    }
                }
            }
        }

        // The stem, row by row: a 2px slant down to the tip on the left, a
        // straight side on the right, the balloon's colour between.
        Repeater {
            model: balloon.stemHeight
            Item {
                required property int index
                readonly property int start: balloon.stemX - balloon.stemWidth + Math.round(index * balloon.stemWidth / balloon.stemHeight)
                y: balloon.stemUp ? balloon.stemHeight - 1 - index : body.height - 1 + index
                x: start
                width: balloon.stemX - start + 1
                height: 1
                Rectangle { x: 0; width: 2; height: 1; color: "black" }
                Rectangle { x: 2; width: Math.max(0, parent.width - 3); height: 1; color: body.color; visible: index > 0 }
                Rectangle { x: parent.width - 1; width: 1; height: 1; color: "black" }
            }
        }
        // (the box's edge where the stem leaves it)
        Rectangle {
            x: balloon.stemX - balloon.stemWidth + 2
            y: balloon.stemUp ? balloon.stemHeight : body.height - 1
            width: balloon.stemWidth - 2
            height: 1
            color: body.color
        }
    }
}
