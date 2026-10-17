// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import "LiveColors.js" as LiveColors
Item {
    id: button
    required property var art
    property var live: ({})
    property string stateName: "normal"
    readonly property bool tinted: live.enabled === true && art && art["normal-background"] !== undefined
    // XPlasma draws the art at 1x, 2x and 3x ("normal@2x", ...): the screen's
    // scale, or the next one up smoothed down to it.
    readonly property real ratio: Screen.devicePixelRatio
    readonly property int scale: Math.max(1, Math.min(art && art.scales ? art.scales : 1, Math.ceil(ratio - 0.01)))
    readonly property bool smoothed: scale !== ratio
    function image(key) {
        return !art ? "" : (scale > 1 && art[key + "@" + scale + "x"]) || art[key] || ""
    }
    implicitWidth: art ? art.width : 0
    implicitHeight: art ? art.height : 0
    Image {
        anchors.fill: parent
        source: button.image(button.tinted ? button.stateName + "-background" : button.stateName)
        smooth: button.smoothed
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
        source: button.tinted ? button.image(button.stateName + "-shadowMask") : ""
        smooth: button.smoothed
        layer.enabled: button.tinted
        layer.effect: TintEffect {
            glyph: 1.0
            tint: "#73000000"
        }
    }
    Image {
        anchors.fill: parent
        visible: button.tinted
        source: button.tinted ? button.image(button.stateName + "-foreground") : ""
        smooth: button.smoothed
    }
    Image {
        anchors.fill: parent
        visible: button.tinted
        source: button.tinted ? button.image(button.stateName + "-textMask") : ""
        smooth: button.smoothed
        layer.enabled: button.tinted
        layer.effect: TintEffect {
            glyph: 1.0
            tint: button.live.start ? LiveColors.text(Qt.color(button.live.start), button.live.startText, "white") : "white"
        }
    }
}
