// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
ShaderEffect {
    property variant source
    property color tint: "white"
    property real tone: 0.5
    property real glyph: 0.0
    property color edgeTint: tint
    property real edgeWidth: 0.0
    property real edgeTone: 0.354402745
    fragmentShader: Qt.resolvedUrl("shaders/tint.frag.qsb")
}
