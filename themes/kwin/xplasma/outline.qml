// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Window

Window {
    id: window
    flags: Qt.BypassWindowManagerHint | Qt.FramelessWindowHint | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    x: outline.geometry.x
    y: outline.geometry.y
    width: outline.geometry.width
    height: outline.geometry.height
    visible: outline.active

    Canvas {
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const ctx = getContext("2d");
            ctx.reset();
            if (width < 1 || height < 1) return;
            ctx.lineWidth = 1;
            ctx.strokeStyle = "white";
            ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
            ctx.setLineDash([2, 2]);
            ctx.strokeStyle = "black";
            ctx.strokeRect(0.5, 0.5, width - 1, height - 1);
        }
    }
}
