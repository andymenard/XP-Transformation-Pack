// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtCore
import Qt.labs.folderlistmodel
import org.kde.plasma.plasma5support as P5Support
Item {
    id: style
    visible: false
    width: 0; height: 0
    property bool crispEnabled: false
    property string settingsDirectory: decodeURIComponent(String(StandardPaths.writableLocation(StandardPaths.GenericDataLocation)).replace(/^file:\/\//, "")) + "/xplasma"
    readonly property var families: Qt.fontFamilies()
    function supports(family) {
        return families.indexOf("XPlasma Tahoma Pixel") >= 0
            && (family === "Tahoma" || family === "MS Sans Serif" || family.indexOf("XPlasma Tahoma Pixel") === 0);
    }
    function physical(pixels, dpr) { return Math.max(1, Math.round(Math.max(1, Math.round(pixels)) * dpr)); }
    function family(original, pixels, dpr) {
        if (!crispEnabled || !supports(original)) return original;
        const optical = "XPlasma Tahoma Pixel " + physical(pixels, dpr);
        return families.indexOf(optical) >= 0 ? optical : "XPlasma Tahoma Pixel";
    }
    function read() { debounce.restart(); }
    Component.onCompleted: read()
    onSettingsDirectoryChanged: read()
    // Atomic replacement of appearance.json emits model changes. No polling.
    FolderListModel {
        id: files
        folder: "file://" + style.settingsDirectory
        nameFilters: ["appearance.json"]
        showDirs: false
        onCountChanged: style.read()
        onStatusChanged: if (status === FolderListModel.Ready) style.read()
    }
    Connections {
        target: files
        function onDataChanged() { style.read(); }
        function onModelReset() { style.read(); }
    }
    Timer {
        id: debounce
        interval: 30
        onTriggered: {
            const path = style.settingsDirectory + "/appearance.json";
            // Quote the user data path as one shell argument; it is never code.
            const command = "cat -- '" + path.replace(/'/g, "'\\''") + "'";
            if (reader.connectedSources.length) { restart(); return; }
            reader.connectSource(command);
        }
    }
    P5Support.DataSource {
        id: reader
        engine: "executable"
        onNewData: (source, data) => {
            disconnectSource(source);
            try { style.crispEnabled = JSON.parse(String(data.stdout || "{}")).crispText === true; }
            catch (error) { style.crispEnabled = false; }
        }
    }
}
