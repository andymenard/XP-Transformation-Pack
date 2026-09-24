// XPLasma: a push button in the look's own art (widgets/xp-notify), 75px
// wide at least and 23 tall, as XP's dialogs have them.
import QtQuick

import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg

Item {
    id: button
    property string text
    signal clicked()

    implicitWidth: Math.max(75, label.implicitWidth + 16)
    // (the desktop's colours, not the panel's, as dialogs have them)
    SystemPalette {
        id: palette
    }
    implicitHeight: 23
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: text
    Accessible.onPressAction: button.clicked()
    Keys.onSpacePressed: button.clicked()
    Keys.onReturnPressed: button.clicked()

    KSvg.FrameSvgItem {
        anchors.fill: parent
        imagePath: "widgets/xp-notify"
        prefix: mouse.pressed && mouse.containsMouse ? "button-pressed"
              : mouse.containsMouse || button.activeFocus ? "button-hover"
              : "button-normal"
    }
    Text {
        id: label
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: mouse.pressed && mouse.containsMouse ? 1 : 0
        anchors.verticalCenterOffset: mouse.pressed && mouse.containsMouse ? 1 : 0
        text: button.text
        font: Kirigami.Theme.defaultFont
        color: button.enabled ? palette.buttonText : disabledPalette.buttonText
        renderType: Text.NativeRendering
    }
    SystemPalette {
        id: disabledPalette
        colorGroup: SystemPalette.Disabled
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: button.clicked()
    }
}
