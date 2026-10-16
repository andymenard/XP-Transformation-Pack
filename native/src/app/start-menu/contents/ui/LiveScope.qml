// Retired compatibility component: use baked theme artwork instead.
import QtQuick
Item {
    property Item scene
    property string mode: "panel"
    property bool allowed: false
    readonly property int surfaceCount: 0
    readonly property bool active: false
    readonly property var colors: ({})
    visible: false
}
