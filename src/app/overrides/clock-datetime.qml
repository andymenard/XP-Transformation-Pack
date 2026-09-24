    // Date and Time Properties, started as its own systemd unit (not the
    // shell's child: applying a look restarts the shell).
    P5Support.DataSource {
        id: xpDateTimeLauncher
        engine: "executable"
        onNewData: (source, data) => disconnectSource(source)
    }
    function xpOpenDateTime() {
        xpDateTimeLauncher.connectSource(@LAUNCH_DATETIME@);
    }
