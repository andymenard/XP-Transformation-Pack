// SPDX-License-Identifier: GPL-3.0-or-later
/*
    XPlasma's taskbar clock: XP's -- the time alone, in the look's clock font
    at XP's 96 dpi and its clock colour, on the look's tray art (xp-tray's
    "clock" frame, which carries on from the notification area to the
    panel's right edge). The long date as its tooltip. A click opens XP's
    month calendar over it; a double-click, or "Adjust Date/Time" on its
    menu, opens Date and Time Properties.
*/
import QtQuick
import QtQuick.Layouts

import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasma5support as P5Support
import org.kde.kirigami as Kirigami
import org.kde.ksvg as KSvg

PlasmoidItem {
    id: root
    XpTextStyle { id: pixelTextStyle }

    preferredRepresentation: compactRepresentation
    // (Plasma builds the compact representation only for a plasmoid that
    // has a full one to switch from.)
    fullRepresentation: Item {}
    switchWidth: Kirigami.Units.gridUnit * 10
    switchHeight: Kirigami.Units.gridUnit * 10
    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground

    // The time, to the minute, changing on the minute.
    property date now: new Date()
    Timer {
        id: tick
        repeat: false
        running: true
        interval: 60000 - (Date.now() % 60000) + 50
        onTriggered: {
            root.now = new Date();
            interval = 60000 - (Date.now() % 60000) + 50;
            start();
        }
    }
    // The locale's short time, never with seconds (XP's clock showed none;
    // some locales' short format has them -- C's: 10:33:20).
    readonly property string timeText: Qt.formatTime(now, Qt.locale().timeFormat(Locale.ShortFormat).replace(/[.:]ss?/, ""))

    toolTipMainText: Qt.formatDate(now, Qt.locale().dateFormat(Locale.LongFormat))
    toolTipSubText: ""

    function openDateTime() {
        calendar.visible = false;
        launcher.connectSource(Plasmoid.configuration.dateTimeCommand);
    }
    // (A click on the clock while the calendar is up closes it, as it takes
    // the focus away: that click mustn't open it again.)
    property real calendarClosed: 0
    function toggleCalendar() {
        if (!calendar.visible && Date.now() - calendarClosed < 300)
            return;
        root.now = new Date();
        calendar.visible = !calendar.visible;
    }
    Plasmoid.onActivated: toggleCalendar()
    // The calendar is the clock's own popup, not Plasma's expanded view.
    onExpandedChanged: if (root.expanded) root.expanded = false
    P5Support.DataSource {
        id: launcher
        engine: "executable"
        onNewData: (source, data) => disconnectSource(source)
    }
    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: "Adjust Date/Time"
            icon.name: "preferences-system-time"
            onTriggered: root.openDateTime()
        }
    ]

    compactRepresentation: MouseArea {
        id: clock
        readonly property bool horizontal: Plasmoid.formFactor !== PlasmaCore.Types.Vertical
        Layout.minimumWidth: horizontal ? label.implicitWidth + Kirigami.Units.largeSpacing : 0
        Layout.maximumWidth: Layout.minimumWidth
        Layout.minimumHeight: horizontal ? 0 : label.implicitHeight + Kirigami.Units.smallSpacing
        acceptedButtons: Qt.LeftButton
        onClicked: root.toggleCalendar()
        onDoubleClicked: root.openDateTime()

        // XP's tray art behind the clock: over the panel's whole height, from
        // just left of the clock to the panel's right edge.
        KSvg.FrameSvg {
            id: traySvg
            imagePath: "widgets/xp-tray"
        }
        KSvg.FrameSvgItem {
            id: backdrop
            visible: traySvg.hasElementPrefix("clock") && clock.horizontal
            z: -1
            anchors.fill: parent
            // (Plasma keeps the same margin above and below an applet that
            // can't fill the panel.)
            anchors.topMargin: -panelInset
            anchors.bottomMargin: -panelInset
            anchors.leftMargin: -8
            anchors.rightMargin: -rightInset
            imagePath: "widgets/xp-tray"
            prefix: "clock"
            readonly property real panelInset: clock.Window.window
                ? Math.max(0, Math.floor((clock.Window.height - clock.height) / 2)) : 3
            // To the panel's right edge exactly, where XP's tray art ends.
            property real rightInset: 8
            function measure() {
                if (!clock.Window.window)
                    return;
                const gap = Math.round(clock.Window.width - clock.mapToItem(null, clock.width, 0).x);
                if (gap >= 0 && gap <= 16)
                    rightInset = gap;
            }
            Component.onCompleted: Qt.callLater(measure)
            Connections {
                target: clock
                function onWidthChanged() { Qt.callLater(backdrop.measure) }
                function onXChanged() { Qt.callLater(backdrop.measure) }
            }
            Connections {
                target: clock.parent
                function onXChanged() { Qt.callLater(backdrop.measure) }
            }
            Connections {
                target: clock.Window.window
                function onWidthChanged() { Qt.callLater(backdrop.measure) }
            }
        }

        XpText {
            id: label
            xpStyle: pixelTextStyle
            anchors.centerIn: parent
            text: root.timeText
            // XP's clock font at XP's 96 dpi, whatever the screen's.
            xpFamily: Plasmoid.configuration.fontFamily
            xpPixels: Math.round(Plasmoid.configuration.fontPoints * 96 / 72)
            xpWeight: Plasmoid.configuration.fontWeight
            color: Plasmoid.configuration.textColor.length > 0 ? Plasmoid.configuration.textColor : Kirigami.Theme.textColor
            renderType: Text.NativeRendering
        }
    }

    PlasmaCore.Dialog {
        id: calendar
        type: PlasmaCore.Dialog.PopupMenu
        flags: Qt.Popup | Qt.FramelessWindowHint
        backgroundHints: PlasmaCore.Dialog.NoBackground
        location: Plasmoid.location
        visualParent: root.compactRepresentationItem
        hideOnWindowDeactivate: true
        onVisibleChanged: {
            if (visible) {
                monthCalendar.reset();
                monthCalendar.forceActiveFocus();
            } else {
                root.calendarClosed = Date.now();
            }
        }
        mainItem: CalendarPanel {
            id: monthCalendar
            xpTextStyle: pixelTextStyle
            today: root.now
            onCloseRequested: calendar.visible = false
        }
    }
}
