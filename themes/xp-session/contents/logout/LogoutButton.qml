// SPDX-License-Identifier: GPL-2.0-or-later
import QtQuick
import QtQuick.Controls
import "timer.js" as AutoTriggerTimer

// One icon button of XP's Turn Off / Log Off dialog: the 32px icon on top, its label
// 39px below (dialog units 49 and 73), both centred in a 75px (50 dlu) column.
AbstractButton {
    id: button
    width: 75; height: Math.max(52, 39 + label.implicitHeight)
    font.family: "Tahoma OS"
    font.pixelSize: 11
    // Sprites hold three 32px states per button: normal, pressed, hot.
    // 20150 (msgina): Turn Off 0, Stand By 3, Restart 6, disabled 9. 14356 (shell32): Switch User 0, Log Off 3.
    property url sprite: "assets/20150.png"
    property int spriteRow: icon.name.includes("suspend") ? 3 : icon.name.includes("reboot") ? 6 : 0
    Keys.onPressed: AutoTriggerTimer.cancelAutoTrigger()
    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()
    padding: 0
    leftInset: 0; rightInset: 0; topInset: 0; bottomInset: 0
    background: Item {}
    contentItem: Item {
        Item {
            x: Math.round((button.width - 32) / 2); width: 32; height: 32; clip: true
            Image {
                source: button.sprite
                width: 32
                y: -32 * (!button.enabled ? 9 : button.spriteRow + (button.down ? 1 : button.hovered || button.visualFocus ? 2 : 0))
                smooth: false
            }
        }
        Text {
            id: label
            wrapMode: Text.WordWrap
            y: 39; width: button.width
            text: button.text.replace(/&/g, "")
            font: button.font; color: "white"
            horizontalAlignment: Text.AlignHCenter
            textFormat: Text.PlainText
        }
    }
}
