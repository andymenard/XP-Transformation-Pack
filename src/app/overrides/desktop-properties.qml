    // XPlasma: XP's desktop menu ends with Properties -- XPlasma Properties.
    // Started as its own systemd unit, not the shell's child: applying a
    // look restarts the shell, which would take a child with it.
    P5Support.DataSource {
        id: xpLauncher
        engine: "executable"
        onNewData: (source, data) => disconnectSource(source)
    }
    PlasmaCore.Action {
        id: xpPropertiesAction
        text: "XPlasma Properties"
        icon.name: "preferences-desktop-theme"
        onTriggered: xpLauncher.connectSource(@LAUNCH@)
    }
