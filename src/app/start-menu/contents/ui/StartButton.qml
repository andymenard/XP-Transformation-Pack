/*
    The Start button: XPLasma's images of the look's own button -- normal,
    hot, and pressed while the menu is open -- at their size, the panel's
    height. Without a look applied, Plasma's launcher icon.

    SPDX-License-Identifier: GPL-2.0-or-later
*/
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

MouseArea {
    id: button
    required property var launcher
    readonly property var art: launcher.themeData.button || null

    hoverEnabled: true
    Accessible.name: "Start"
    Accessible.role: Accessible.Button

    Layout.preferredWidth: art ? art.width : Kirigami.Units.iconSizes.medium
    Layout.minimumWidth: Layout.preferredWidth
    Layout.maximumWidth: Layout.preferredWidth
    Layout.fillHeight: true

    property bool wasExpanded
    onPressed: wasExpanded = launcher.expanded
    onClicked: launcher.expanded = !wasExpanded

    Image {
        visible: button.art !== null
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
