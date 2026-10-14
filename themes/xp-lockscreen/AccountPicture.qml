// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
Item {
    id: root
    property string picturePath: ""
    property url fallbackSource
    implicitWidth: 48
    implicitHeight: 48
    function pictureUrl(path) {
        if (!path) return "";
        if (path.startsWith("file:")) return path;
        return "file://" + path.split("/").map(encodeURIComponent).join("/");
    }
    Image {
        anchors.fill: parent
        source: root.fallbackSource
        visible: !root.picturePath || portrait.status === Image.Error
        smooth: false
        fillMode: Image.PreserveAspectCrop
    }
    Image {
        id: portrait
        objectName: "accountPortrait"
        anchors.fill: parent
        source: root.pictureUrl(root.picturePath)
        cache: false
        smooth: true
        fillMode: Image.PreserveAspectCrop
    }
}
