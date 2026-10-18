// SPDX-License-Identifier: GPL-3.0-or-later
/*
    XP's month calendar (the common control's, as Date and Time Properties
    has it): the month and year on the selection colour between its arrow
    buttons, the narrow day names over a rule, six weeks with the days of
    the months either side in grey, today ringed in red, and "Today:" under
    it all. The arrows, Page Up/Down and the wheel turn the month; a grey
    day turns to its month; "Today:" comes back to this one.
*/
import QtQuick
import org.kde.kirigami as Kirigami

Rectangle {
    id: panel
    property var xpTextStyle: null
    property date today: new Date()
    // The month shown (its first day) and the day picked in it.
    property date month: new Date(today.getFullYear(), today.getMonth(), 1)
    property date selected: today
    signal closeRequested()

    readonly property int cellWidth: 23
    readonly property int cellHeight: 15
    readonly property int edge: 6
    readonly property var locale: Qt.locale()
    // Columns from the locale's first day (Locale.Sunday, 0 .. Saturday, 6).
    readonly property int firstDay: locale.firstDayOfWeek % 7
    readonly property int lead: (new Date(month.getFullYear(), month.getMonth(), 1).getDay() - firstDay + 7) % 7

    function sameDay(a, b) {
        return a.getFullYear() === b.getFullYear() && a.getMonth() === b.getMonth() && a.getDate() === b.getDate();
    }
    function dayAt(index) {
        return new Date(month.getFullYear(), month.getMonth(), 1 + index - lead);
    }
    function turn(months) {
        month = new Date(month.getFullYear(), month.getMonth() + months, 1);
        const last = new Date(month.getFullYear(), month.getMonth() + 1, 0).getDate();
        selected = new Date(month.getFullYear(), month.getMonth(), Math.min(selected.getDate(), last));
    }
    function pick(date) {
        selected = date;
        month = new Date(date.getFullYear(), date.getMonth(), 1);
    }
    function reset() {
        pick(today);
    }

    Kirigami.Theme.colorSet: Kirigami.Theme.View
    Kirigami.Theme.inherit: false
    width: implicitWidth; height: implicitHeight
    implicitWidth: 7 * cellWidth + 2 * edge + 2
    implicitHeight: footer.y + footer.height + 6
    color: Kirigami.Theme.backgroundColor
    border.width: 1; border.color: "#7f9db9"
    focus: true
    Accessible.role: Accessible.Pane
    Accessible.name: "Calendar"

    Keys.onEscapePressed: panel.closeRequested()
    Keys.onLeftPressed: pick(new Date(selected.getFullYear(), selected.getMonth(), selected.getDate() - 1))
    Keys.onRightPressed: pick(new Date(selected.getFullYear(), selected.getMonth(), selected.getDate() + 1))
    Keys.onUpPressed: pick(new Date(selected.getFullYear(), selected.getMonth(), selected.getDate() - 7))
    Keys.onDownPressed: pick(new Date(selected.getFullYear(), selected.getMonth(), selected.getDate() + 7))
    Keys.onPressed: event => {
        if (event.key === Qt.Key_PageUp) { turn(-1); event.accepted = true; }
        else if (event.key === Qt.Key_PageDown) { turn(1); event.accepted = true; }
        else if (event.key === Qt.Key_Home) { reset(); event.accepted = true; }
    }
    WheelHandler {
        onWheel: event => {
            const delta = event.angleDelta.y || -event.angleDelta.x;
            if (delta) panel.turn(delta > 0 ? -1 : 1);
        }
    }

    // The title: month and year between the arrows.
    Rectangle {
        id: title
        x: 1; y: 1
        width: parent.width - 2; height: 24
        color: Kirigami.Theme.highlightColor
        Repeater {
            model: [-1, 1]
            delegate: Rectangle {
                id: arrow
                required property int modelData
                x: modelData < 0 ? 5 : title.width - width - 5
                anchors.verticalCenter: parent.verticalCenter
                width: 18; height: 16
                Kirigami.Theme.colorSet: Kirigami.Theme.Button
                Kirigami.Theme.inherit: false
                color: Kirigami.Theme.backgroundColor
                border.color: arrowMouse.pressed ? "#808080" : "white"
                Rectangle {
                    anchors.fill: parent; anchors.margins: 1
                    color: "transparent"
                    border.color: arrowMouse.pressed ? "transparent" : "#aca899"
                }
                Canvas {
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: arrowMouse.pressed ? 1 : 0
                    anchors.verticalCenterOffset: arrowMouse.pressed ? 1 : 0
                    width: 4; height: 7
                    onPaint: {
                        const g = getContext("2d");
                        g.reset();
                        g.fillStyle = Kirigami.Theme.textColor;
                        g.beginPath();
                        if (arrow.modelData < 0) { g.moveTo(4, 0); g.lineTo(0, 3.5); g.lineTo(4, 7); }
                        else { g.moveTo(0, 0); g.lineTo(4, 3.5); g.lineTo(0, 7); }
                        g.closePath();
                        g.fill();
                    }
                }
                MouseArea {
                    id: arrowMouse
                    anchors.fill: parent
                    onClicked: panel.turn(arrow.modelData)
                }
                Accessible.role: Accessible.Button
                Accessible.name: modelData < 0 ? "Previous month" : "Next month"
            }
        }
        XpText {
            anchors.centerIn: parent
            text: panel.locale.standaloneMonthName(panel.month.getMonth(), Locale.LongFormat) + " " + panel.month.getFullYear()
            xpPoints: 8; xpBold: true
            color: Kirigami.Theme.highlightedTextColor
        }
    }

    // The day names, over a rule.
    Row {
        id: names
        x: panel.edge; y: title.y + title.height + 3
        Repeater {
            model: 7
            delegate: XpText {
                required property int index
                width: panel.cellWidth; height: panel.cellHeight
                horizontalAlignment: Text.AlignHCenter
                text: panel.locale.dayName((panel.firstDay + index) % 7, Locale.NarrowFormat)
                xpPoints: 8
                color: Kirigami.Theme.highlightColor
            }
        }
    }
    Rectangle {
        x: panel.edge; y: names.y + names.height + 1
        width: 7 * panel.cellWidth; height: 1
        color: Kirigami.Theme.disabledTextColor
    }

    Grid {
        id: days
        x: panel.edge; y: names.y + names.height + 4
        columns: 7
        Repeater {
            model: 42
            delegate: Item {
                id: cell
                required property int index
                readonly property date date: panel.dayAt(index)
                readonly property bool inMonth: date.getMonth() === panel.month.getMonth()
                readonly property bool picked: inMonth && panel.sameDay(date, panel.selected)
                width: panel.cellWidth; height: panel.cellHeight
                Rectangle {
                    anchors.centerIn: parent
                    width: panel.cellWidth - 3; height: panel.cellHeight
                    visible: cell.picked
                    color: Kirigami.Theme.highlightColor
                }
                // Today, ringed.
                Rectangle {
                    anchors.centerIn: parent
                    width: panel.cellWidth; height: panel.cellHeight + 2
                    radius: height / 2
                    visible: panel.sameDay(cell.date, panel.today) && cell.inMonth
                    color: "transparent"
                    border.color: "#e00000"
                }
                XpText {
                    anchors.fill: parent
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: cell.date.getDate()
                    xpPoints: 8
                    color: cell.picked ? Kirigami.Theme.highlightedTextColor
                         : cell.inMonth ? Kirigami.Theme.textColor : Kirigami.Theme.disabledTextColor
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: panel.pick(cell.date)
                }
            }
        }
    }

    // "Today:", which comes back to this month.
    Item {
        id: footer
        x: panel.edge; y: days.y + days.height + 4
        width: 7 * panel.cellWidth; height: 16
        Rectangle {
            id: ring
            x: 4; anchors.verticalCenter: parent.verticalCenter
            width: 20; height: 12; radius: 6
            color: "transparent"
            border.color: "#e00000"
        }
        XpText {
            anchors.left: ring.right; anchors.leftMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            // (XP's short date kept the century: 10/17/2026.)
            text: "Today: " + panel.today.toLocaleDateString(panel.locale,
                      panel.locale.dateFormat(Locale.ShortFormat).replace(/(^|[^y])yy(?!y)/, "$1yyyy"))
            xpPoints: 8; xpBold: true
            color: Kirigami.Theme.textColor
        }
        MouseArea {
            anchors.fill: parent
            onClicked: panel.reset()
        }
    }
}
