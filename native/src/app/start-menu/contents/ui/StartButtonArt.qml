// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import "LiveColors.js" as LiveColors
Item {
    id: button
    required property var art
    property var live: ({})
    property string stateName: "normal"
    readonly property bool tinted: live.enabled === true && art && art["normal-background"] !== undefined
    implicitWidth: art ? art.width : 0
    implicitHeight: art ? art.height : 0
    Image {
        anchors.fill: parent
        source: !button.art ? "" : button.tinted ? button.art[button.stateName + "-background"] : button.art[button.stateName]
        smooth: false
        layer.enabled: button.tinted
        layer.effect: TintEffect {
            tint: button.live.start || "white"
            tone: button.art ? button.art.tone || 0.5 : 0.5
            edgeTint: button.live.menu || tint
            edgeWidth: button.art && button.art.blueTaskbarEdge ? 9.0 / button.art.width : 0.0
        }
    }
    Image {
        anchors.fill: parent
        visible: button.tinted && button.live.start && LiveColors.text(Qt.color(button.live.start),button.live.startText,"white") === "#ffffff"
        source: button.tinted ? button.art[button.stateName + "-shadowMask"] || "" : ""
        smooth: false
        layer.enabled: button.tinted
        layer.effect: TintEffect {
            glyph: 1.0
            tint: "#73000000"
        }
    }
    Image {
        anchors.fill: parent
        visible: button.tinted
        source: button.tinted ? button.art[button.stateName + "-foreground"] : ""
        smooth: false
    }
    Image {
        anchors.fill: parent
        visible: button.tinted
        source: button.tinted ? button.art[button.stateName + "-textMask"] : ""
        smooth: false
        layer.enabled: button.tinted
        layer.effect: TintEffect {
            glyph: 1.0
            tint: button.live.start ? LiveColors.text(Qt.color(button.live.start), button.live.startText, "white") : "white"
        }
    }
}
