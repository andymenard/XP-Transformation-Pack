
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
        anchors.topMargin: -3
        anchors.bottomMargin: -3
        anchors.leftMargin: -8
        anchors.rightMargin: -8
        imagePath: "widgets/xp-tray"
        prefix: "clock"
    }
