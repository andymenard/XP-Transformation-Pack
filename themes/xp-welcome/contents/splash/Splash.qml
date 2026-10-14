// SPDX-License-Identifier: GPL-3.0-or-later
// Independent QML implementation; colors/layout referenced against XP logonui.exe.
import QtQuick
Rectangle {
    id: root
    property int stage: 0
    // ISO logonui.exe: UIFILE 1000 uses rcstr(1)=Arial, rcint(44)=36pt,
    // bold italic; rcstr(7)="welcome". 36pt is 48 logical pixels at 96 DPI.
    FontLoader { id: welcomeFont; source: "fonts/arialbi.ttf" }
    readonly property string welcomeFamily: welcomeFont.status === FontLoader.Ready ? welcomeFont.name : "Arial"
    color: "#5a7edc"
    Rectangle {
        width: parent.width; height: parent.height * 0.11
        color: "#00309c"
        Rectangle {anchors.bottom:parent.bottom;width:parent.width;height:2;color:"#8cabea"}
    }
    Rectangle {
        y: parent.height * 0.89;width:parent.width;height:parent.height-y
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop {position:0;color:"#3934ad"}
            GradientStop {position:1;color:"#00309c"}
        }
        Rectangle {width:parent.width;height:2;color:"#ee9f3b"}
    }
    Text {
        anchors.centerIn:parent;anchors.horizontalCenterOffset:2;anchors.verticalCenterOffset:3
        text:qsTr("welcome");font.family:root.welcomeFamily;font.bold:true;font.italic:true;font.pixelSize:48;color:"#3151b5"
    }
    Text {
        anchors.centerIn:parent;text:qsTr("welcome");font.family:root.welcomeFamily;font.bold:true;font.italic:true
        font.pixelSize:48;color:"#eff7ff"
        Accessible.name:qsTr("Welcome. Starting your Plasma desktop.")
    }
}
