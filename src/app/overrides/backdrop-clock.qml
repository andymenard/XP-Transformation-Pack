
    KSvg.FrameSvg {
        id: xpTraySvg
        imagePath: "widgets/xp-tray"
    }
    readonly property bool xpTrayAvailable: xpTraySvg.hasElementPrefix("tray")
    KSvg.FrameSvgItem {
        id: xpTrayBackground
        visible: root.xpTrayAvailable && Plasmoid.formFactor === PlasmaCore.Types.Horizontal
        z: -1
        anchors.fill: parent
        // Over the panel's whole height: Plasma keeps the same margin above
        // and below every applet that can't fill the panel.
        anchors.topMargin: -xpTrayBackground.panelInset
        anchors.bottomMargin: -xpTrayBackground.panelInset
        anchors.leftMargin: -8
        // To the panel's right edge exactly, where XP's tray art ends (the
        // last applet sits a few pixels in from it).
        anchors.rightMargin: -rightInset
        imagePath: "widgets/xp-tray"
        readonly property real panelInset: root.Window.window
            ? Math.max(0, Math.floor((root.Window.height - root.height) / 2)) : 3
        property real rightInset: 8
        function measure() {
            if (!root.Window.window)
                return;
            const gap = Math.round(root.Window.width - root.mapToItem(null, root.width, 0).x);
            if (gap >= 0 && gap <= 16)
                rightInset = gap;
        }
        Component.onCompleted: Qt.callLater(measure)
        Connections {
            target: root
            function onWidthChanged() { Qt.callLater(xpTrayBackground.measure) }
            function onXChanged() { Qt.callLater(xpTrayBackground.measure) }
        }
        Connections {
            target: root.parent
            function onXChanged() { Qt.callLater(xpTrayBackground.measure) }
        }
        Connections {
            target: root.Window.window
            function onWidthChanged() { Qt.callLater(xpTrayBackground.measure) }
        }
        prefix: "clock"
    }
