
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
        anchors.rightMargin: -8
        imagePath: "widgets/xp-tray"
        readonly property real panelInset: root.Window.window
            ? Math.max(0, Math.floor((root.Window.height - root.height) / 2)) : 3
        prefix: "clock"
    }
