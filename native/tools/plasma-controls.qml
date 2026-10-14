// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * plasma-controls.qml: Plasma's popup controls (Plasma Components 3), drawn
 * by the Plasma theme in use, on a popup background -- to see XPlasma's
 * theme without hunting for a popup that happens to have each control.
 *
 *   qml6 tools/plasma-controls.qml                  a window
 *   qml6 tools/plasma-controls.qml -- out.png       saved to out.png, then quits
 *
 * A developer's tool.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import org.kde.ksvg as KSvg
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.components as PC3
import org.kde.kirigami as Kirigami

Window {
    id: win
    width: 520
    height: 380
    visible: true
    title: "Plasma controls"
    color: "transparent"

    KSvg.FrameSvgItem {
        id: popup
        anchors.fill: parent
        imagePath: "dialogs/background"

        GridLayout {
            anchors.fill: parent
            anchors.leftMargin: popup.margins.left + 8
            anchors.topMargin: popup.margins.top + 8
            anchors.rightMargin: popup.margins.right + 8
            anchors.bottomMargin: popup.margins.bottom + 8
            columns: 2
            columnSpacing: 16

            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                PC3.CheckBox { text: "Unchecked" }
                PC3.CheckBox { text: "Checked"; checked: true }
                PC3.CheckBox { text: "Partly"; tristate: true; checkState: Qt.PartiallyChecked }
                PC3.CheckBox { text: "Disabled"; enabled: false; checked: true }
                PC3.RadioButton { text: "Radio"; checked: true }
                PC3.Switch { text: "Switch on"; checked: true }
                PC3.Switch { text: "Switch off" }
                PC3.SpinBox { value: 42; to: 100 }
                PC3.SpinBox { value: 7; editable: false }
                PC3.Button { text: "Button" }
            }
            ColumnLayout {
                Layout.alignment: Qt.AlignTop
                Layout.fillWidth: true
                PC3.TextField { text: "Text field"; Layout.fillWidth: true }
                PC3.ComboBox { model: ["Combo box", "Two"]; Layout.fillWidth: true }
                PC3.Slider { value: 0.4; Layout.fillWidth: true }
                PC3.ProgressBar { value: 0.6; Layout.fillWidth: true }
                PC3.ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 110
                    PC3.ScrollBar.vertical.policy: PC3.ScrollBar.AlwaysOn
                    ListView {
                        model: 20
                        delegate: PC3.ItemDelegate {
                            width: ListView.view.width
                            text: "Row " + (index + 1)
                        }
                    }
                }
            }
        }
    }

    Timer {
        running: win.shotPath.length > 0
        interval: 1500
        onTriggered: popup.grabToImage(result => {
            result.saveToFile(win.shotPath);
            Qt.quit();
        })
    }
    // The argument after "--", if any.
    readonly property string shotPath: {
        const args = Qt.application.arguments;
        const i = args.indexOf("--");
        return i >= 0 && i + 1 < args.length ? args[i + 1] : "";
    }
}
