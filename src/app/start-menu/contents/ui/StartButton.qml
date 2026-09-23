/*
    The Start button: XPLasma's images of the look's own button -- normal,
    hot, and pressed while the menu is open -- at their size, the panel's
    height. Without a look applied, Plasma's launcher icon.

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.kirigami as Kirigami

Item {
    id: button
    required property var launcher
    readonly property var art: launcher.themeData.button || null
    readonly property alias pressed: mouse.pressed
    readonly property alias containsMouse: mouse.containsMouse

    // XP's button sits in the screen's corner, and the corner clicks it.
    // Plasma's panel keeps a few pixels before its first applet: when that's
    // all there is between this button and the panel's edge, the button
    // takes them over (drawn and clickable) instead of leaving a gap.
    property real edgeGap: 0
    function measureEdge() {
        if (Plasmoid.formFactor !== PlasmaCore.Types.Horizontal || !button.Window.window) {
            edgeGap = 0;
            return;
        }
        const x = Math.round(button.mapToItem(null, 0, 0).x);
        edgeGap = x > 0 && x <= Kirigami.Units.smallSpacing * 2 ? x : 0;
    }
    Component.onCompleted: Qt.callLater(measureEdge)
    Connections {
        target: button.launcher
        function onXChanged() { Qt.callLater(button.measureEdge) }
    }
    Connections {
        target: button.launcher.parent
        function onXChanged() { Qt.callLater(button.measureEdge) }
    }
    Connections {
        target: button.Window.window
        function onWidthChanged() { Qt.callLater(button.measureEdge) }
    }
    onXChanged: Qt.callLater(measureEdge)

    Accessible.name: "Start"
    Accessible.role: Accessible.Button

    Layout.preferredWidth: art ? Math.max(1, art.width - edgeGap) : Kirigami.Units.iconSizes.medium
    Layout.minimumWidth: Layout.preferredWidth
    Layout.maximumWidth: Layout.preferredWidth
    Layout.fillHeight: true

    MouseArea {
        id: mouse
        anchors.fill: parent
        anchors.leftMargin: -button.edgeGap
        hoverEnabled: true
        property bool wasExpanded
        onPressed: wasExpanded = button.launcher.expanded
        onClicked: button.launcher.expanded = !wasExpanded
    }

    Image {
        visible: button.art !== null
        x: -button.edgeGap
        anchors.verticalCenter: parent.verticalCenter
        source: !button.art ? ""
              : (button.launcher.expanded || button.pressed) ? button.art.pressed
              : button.containsMouse ? button.art.hover
              : button.art.normal
        smooth: false
        cache: false
    }
    Kirigami.Icon {
        visible: button.art === null
        anchors.fill: parent
        source: "start-here-kde"
        active: button.containsMouse
    }
}
