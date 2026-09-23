import QtQuick

// Pure presentation shared by the Plasma launcher and the isolated preview.
FocusScope {
    id: menu
    required property var themeData
    property string userName: "User"
    property url userPicture
    property var pinnedItems: []
    property var recentItems: []
    property var placeGroups: []
    property Component iconDelegate: null
    property bool canLogout: true
    property bool canShutdown: true
    property alias allProgramsItem: more
    property bool allProgramsOpen: false
    readonly property bool allProgramsHovered: moreMouse.containsMouse
    property Item recentFilesItem: null
    property bool recentFilesOpen: false
    readonly property bool recentFilesHovered: recentFilesItem ? recentFilesItem.hovered : false
    property int menuOpenDelay: 400
    signal requested(var action)
    // A right-click (or the Menu key) on an entry, at (x, y) in `item`.
    signal contextRequested(var action, Item item, real x, real y)
    signal closeProgramsRequested()
    readonly property var parts: themeData.parts
    readonly property var metrics: themeData.layout
    readonly property var provisional: themeData.provisional
    readonly property var xpColors: themeData.palette
    // A frame round everything (Windows Classic's raised edge), when the look
    // has one: its content margins inset the rest.
    readonly property var frameInset: parts.frame ? parts.frame.content : [0, 0, 0, 0]

    // XP's height: the menu is as tall as its taller column needs (never
    // shorter than it was measured at), and when that won't fit the screen
    // above the taskbar, recent programs go from the bottom of the list
    // until it does.
    property real maximumHeight: 100000
    readonly property real chromeHeight: metrics.headerHeight + metrics.footerHeight + frameInset[2] + frameInset[3]
    readonly property real separatorHeight: parts.proglistseparator.height + provisional.separatorGap
    readonly property real programsFixed: parts.proglist.content[2] + parts.proglist.content[3] + metrics.moreHeight
        + pinnedItems.length * provisional.pinnedRowHeight
    readonly property int recentShown: {
        const room = maximumHeight - chromeHeight - programsFixed - (pinnedItems.length > 0 ? separatorHeight : 0);
        return Math.max(0, Math.min(recentItems.length, Math.floor(room / provisional.recentRowHeight)));
    }
    readonly property var shownRecent: recentItems.slice(0, recentShown)
    readonly property real programsNeeded: programsFixed + recentShown * provisional.recentRowHeight
        + (pinnedItems.length > 0 && recentShown > 0 ? separatorHeight : 0)
    readonly property real placesNeeded: placesColumn.height + parts.placeslist.content[2] + parts.placeslist.content[3]
    readonly property int bodyHeight: Math.ceil(Math.max(metrics.height - chromeHeight,
        Math.min(maximumHeight - chromeHeight, Math.max(programsNeeded, placesNeeded))))
    width: metrics.width
    height: chromeHeight + bodyHeight
    focus: true
    Keys.onEscapePressed: {
        if (allProgramsOpen || recentFilesOpen) closeProgramsRequested();
        else requested("close");
    }
    function resetScroll() { programScroll.contentY = 0; }
    onAllProgramsOpenChanged: if (!allProgramsOpen) closeProgramsTimer.stop()
    Timer {
        id: closeProgramsTimer
        interval: menu.menuOpenDelay
        onTriggered: if (menu.allProgramsOpen) menu.closeProgramsRequested()
    }

    component Art: BorderImage {
        required property var spec
        source: spec.url
        border.left: spec.sizing[0]
        border.right: spec.sizing[1]
        border.top: spec.sizing[2]
        border.bottom: spec.sizing[3]
        horizontalTileMode: BorderImage.Stretch
        verticalTileMode: BorderImage.Stretch
        smooth: false
    }

    component Label: Text {
        font.family: menu.themeData.font.family
        font.pixelSize: menu.themeData.font.pixels
        renderType: Text.NativeRendering
        elide: Text.ElideRight
    }

    component Entry: Item {
        id: entry
        required property var entryData
        property int iconSize: 24
        property color textColor: menu.xpColors.programText
        property bool bold: false
        readonly property bool hovered: hover.containsMouse
        readonly property bool isRecentFiles: entryData.action === "recent-documents"
        readonly property bool highlighted: hovered || activeFocus || (isRecentFiles && menu.recentFilesOpen)
        Component.onCompleted: if (isRecentFiles) menu.recentFilesItem = entry
        Component.onDestruction: if (menu.recentFilesItem === entry) menu.recentFilesItem = null
        signal activated()
        function activate() {
            recentOpenTimer.stop();
            if (isRecentFiles && menu.recentFilesOpen) menu.closeProgramsRequested();
            else activated();
        }
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: entryData.label
        Accessible.onPressAction: activate()
        Keys.onReturnPressed: activate()
        Keys.onSpacePressed: activate()
        Keys.onMenuPressed: menu.contextRequested(entryData.action, entry, 0, entry.height)
        Keys.onDownPressed: nextItemInFocusChain().forceActiveFocus(Qt.TabFocusReason)
        Keys.onUpPressed: nextItemInFocusChain(false).forceActiveFocus(Qt.BacktabFocusReason)
        Rectangle {
            anchors.fill: parent
            visible: entry.highlighted
            color: menu.xpColors.highlight
        }
        Loader {
            id: icon
            x: 2
            anchors.verticalCenter: parent.verticalCenter
            width: entry.iconSize
            height: width
            property var iconSource: entry.entryData.icon || ""
            sourceComponent: menu.iconDelegate || bitmapIcon
            Component {
                id: bitmapIcon
                Image {
                    source: parent.iconSource
                    fillMode: Image.PreserveAspectFit
                    smooth: false
                }
            }
        }
        Column {
            x: icon.x + icon.width + 6
            width: Math.max(0, parent.width - x - 4)
            anchors.verticalCenter: parent.verticalCenter
            Label {
                width: parent.width
                text: entry.entryData.label
                font.bold: entry.bold
                color: entry.highlighted ? menu.xpColors.highlightText : entry.textColor
            }
            Label {
                width: parent.width
                visible: text.length > 0
                text: entry.entryData.description || ""
                color: entry.highlighted ? menu.xpColors.highlightText : menu.xpColors.secondaryText
            }
        }
        MouseArea {
            id: hover
            anchors.fill: parent
            hoverEnabled: true
            onEntered: {
                if (entry.isRecentFiles && !menu.recentFilesOpen) recentOpenTimer.restart();
                else if (menu.allProgramsOpen) closeProgramsTimer.restart();
            }
            onExited: { closeProgramsTimer.stop(); recentOpenTimer.stop(); }
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: mouse => {
                if (mouse.button === Qt.RightButton)
                    menu.contextRequested(entry.entryData.action, entry, mouse.x, mouse.y);
                else
                    entry.activate();
            }
        }
        Timer {
            id: recentOpenTimer
            interval: menu.menuOpenDelay
            onTriggered: if (entry.hovered && menu.visible && !menu.recentFilesOpen) entry.activated()
        }
    }

    Loader {
        active: menu.parts.frame !== undefined
        anchors.fill: parent
        sourceComponent: Art { spec: menu.parts.frame }
    }

    Art {
        id: header
        objectName: "header"
        x: menu.frameInset[0]
        y: menu.frameInset[2]
        width: parent.width - menu.frameInset[0] - menu.frameInset[1]
        height: menu.metrics.headerHeight
        spec: menu.parts.userpane
        Image {
            id: pictureFrame
            x: menu.provisional.userPictureX
            y: menu.provisional.userPictureY
            width: menu.parts.userpicture.width
            height: menu.parts.userpicture.height
            source: menu.parts.userpicture.url
            smooth: false
            Image {
                x: menu.parts.userpicture.content[0]
                y: menu.parts.userpicture.content[2]
                width: parent.width - x - menu.parts.userpicture.content[1]
                height: parent.height - y - menu.parts.userpicture.content[3]
                source: menu.userPicture
                fillMode: Image.PreserveAspectCrop
                smooth: false
            }
        }
        Label {
            x: userLabel.x + menu.themeData.userShadowOffset[0]
            y: userLabel.y + menu.themeData.userShadowOffset[1]
            width: userLabel.width
            text: userLabel.text
            font: userLabel.font
            color: menu.xpColors.userShadow
        }
        Label {
            id: userLabel
            x: pictureFrame.x + pictureFrame.width + menu.provisional.userNameGap
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - x - 10
            text: menu.userName
            font.family: menu.themeData.userFont.family
            font.pixelSize: menu.themeData.userFont.pixels
            font.bold: menu.themeData.userFont.bold
            font.italic: menu.themeData.userFont.italic
            color: menu.xpColors.userText
        }
    }

    Art {
        id: programs
        objectName: "programs"
        x: menu.frameInset[0]
        y: header.y + header.height
        width: menu.metrics.leftWidth
        height: menu.bodyHeight - menu.metrics.moreHeight
        spec: menu.parts.proglist
        Flickable {
            id: programScroll
            objectName: "programScroll"
            anchors.fill: parent
            anchors.leftMargin: programs.spec.content[0]
            anchors.rightMargin: programs.spec.content[1]
            anchors.topMargin: programs.spec.content[2]
            anchors.bottomMargin: programs.spec.content[3]
            clip: true
            contentWidth: width
            contentHeight: programEntries.height
            flickableDirection: Flickable.VerticalFlick
            boundsBehavior: Flickable.StopAtBounds
            function reveal(item) {
                const y = item.mapToItem(programEntries, 0, 0).y;
                if (y < contentY) contentY = y;
                else if (y + item.height > contentY + height)
                    contentY = Math.max(0, Math.min(contentHeight - height, y + item.height - height));
            }
            Column {
                id: programEntries
                width: parent.width
                Repeater {
                    model: menu.pinnedItems
                    delegate: Entry {
                        required property var modelData
                        width: programEntries.width
                        height: menu.provisional.pinnedRowHeight
                        iconSize: 32
                        bold: true
                        entryData: modelData
                        onActivated: menu.requested(modelData.action)
                        onActiveFocusChanged: if (activeFocus) programScroll.reveal(this)
                    }
                }
                Item {
                    width: parent.width
                    visible: menu.pinnedItems.length > 0 && menu.recentShown > 0
                    height: visible ? menu.parts.proglistseparator.height + menu.provisional.separatorGap : 0
                    Image {
                        objectName: "pinnedSeparator"
                        // Center in the column, not its asymmetric content inset.
                        x: (programs.width - width) / 2 - programs.spec.content[0]
                        anchors.verticalCenter: parent.verticalCenter
                        source: menu.parts.proglistseparator.url
                        smooth: false
                    }
                }
                Repeater {
                    model: menu.shownRecent
                    delegate: Entry {
                        required property var modelData
                        width: programEntries.width
                        height: menu.provisional.recentRowHeight
                        iconSize: 32
                        entryData: modelData
                        onActivated: menu.requested(modelData.action)
                        onActiveFocusChanged: if (activeFocus) programScroll.reveal(this)
                    }
                }
            }
        }
        Rectangle {
            // Only appears when favorites/recent apps exceed the fixed pane.
            visible: programScroll.contentHeight > programScroll.height
            width: 2
            x: parent.width - programs.spec.content[1] - width
            height: Math.max(12, programScroll.height * programScroll.height / Math.max(1, programScroll.contentHeight))
            y: programScroll.y + (programScroll.height - height) * programScroll.contentY
                / Math.max(1, programScroll.contentHeight - programScroll.height)
            color: menu.xpColors.programText
            opacity: 0.35
        }
    }

    Art {
        id: more
        objectName: "allPrograms"
        x: programs.x
        y: programs.y + programs.height
        width: programs.width
        height: menu.metrics.moreHeight
        spec: menu.parts.moreprograms
        readonly property bool highlighted: moreMouse.containsMouse || more.activeFocus || menu.allProgramsOpen
        activeFocusOnTab: true
        Accessible.role: Accessible.ButtonMenu
        Accessible.name: "All Programs"
        function open() {
            openTimer.stop();
            if (menu.allProgramsOpen) menu.closeProgramsRequested();
            else menu.requested("all-programs");
        }
        Accessible.onPressAction: open()
        Keys.onReturnPressed: open()
        Keys.onSpacePressed: open()
        Image {
            objectName: "allProgramsSeparator"
            y: menu.provisional.moreTopGap
            anchors.horizontalCenter: parent.horizontalCenter
            source: menu.parts.proglistseparator.url
            smooth: false
        }
        Rectangle {
            objectName: "allProgramsHighlight"
            x: more.spec.content[0]
            y: menu.provisional.moreTopGap + menu.parts.proglistseparator.height
            width: parent.width - x - more.spec.content[1]
            height: parent.height - y - more.spec.content[3]
            color: menu.xpColors.highlight
            visible: more.highlighted
        }
        Row {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: (menu.provisional.moreTopGap - menu.provisional.moreBottomExtra) / 2
            spacing: 8
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "All Programs"
                color: more.highlighted ? menu.xpColors.highlightText : menu.xpColors.programText
                font.family: menu.themeData.moreFont.family
                font.pixelSize: menu.themeData.moreFont.pixels
                font.bold: menu.themeData.moreFont.bold
            }
            Image {
                source: (more.highlighted && menu.parts["moreprogramsarrow-hot"]
                         ? menu.parts["moreprogramsarrow-hot"] : menu.parts.moreprogramsarrow).url
                smooth: false
            }
        }
        MouseArea {
            id: moreMouse
            anchors.fill: parent
            hoverEnabled: true
            onEntered: if (!menu.allProgramsOpen) openTimer.restart()
            onExited: openTimer.stop()
            onClicked: more.open()
        }
        Timer {
            id: openTimer
            interval: menu.menuOpenDelay
            onTriggered: if (moreMouse.containsMouse && menu.visible && !menu.allProgramsOpen)
                menu.requested("all-programs")
        }
    }

    Art {
        id: places
        objectName: "places"
        x: programs.x + programs.width
        y: header.y + header.height
        width: parent.width - x - menu.frameInset[1]
        height: menu.bodyHeight
        spec: menu.parts.placeslist
        Column {
            id: placesColumn
            x: places.spec.content[0]
            y: places.spec.content[2]
            width: parent.width - x - places.spec.content[1]
            Repeater {
                model: menu.placeGroups
                delegate: Column {
                    id: group
                    required property var modelData
                    required property int index
                    width: placesColumn.width
                    Repeater {
                        model: group.modelData
                        delegate: Entry {
                            required property var modelData
                            width: group.width
                            height: menu.provisional.placesRowHeight
                            textColor: menu.xpColors.placesText
                            bold: group.index === 0
                            entryData: modelData
                            onActivated: menu.requested(modelData.action)
                        }
                    }
                    Item {
                        width: parent.width
                        height: visible ? menu.parts.placeslistseparator.height + menu.provisional.separatorGap : 0
                        visible: group.index < menu.placeGroups.length - 1
                        Image {
                            x: (places.width - width) / 2 - places.spec.content[0]
                            anchors.verticalCenter: parent.verticalCenter
                            source: menu.parts.placeslistseparator.url
                            smooth: false
                        }
                    }
                }
            }
        }
    }

    Art {
        id: footer
        objectName: "footer"
        x: menu.frameInset[0]
        y: parent.height - height - menu.frameInset[3]
        width: parent.width - menu.frameInset[0] - menu.frameInset[1]
        height: menu.metrics.footerHeight
        spec: menu.parts.logoff
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 9
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            Repeater {
                model: [{label: "Log Off", action: "logoff"}, {label: "Turn Off Computer", action: "shutdown"}]
                delegate: Item {
                    id: button
                    required property var modelData
                    enabled: modelData.action === "logoff" ? menu.canLogout : menu.canShutdown
                    opacity: enabled ? 1 : 0.5
                    activeFocusOnTab: enabled
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.label
                    Accessible.onPressAction: menu.requested(modelData.action)
                    Keys.onReturnPressed: menu.requested(modelData.action)
                    Keys.onSpacePressed: menu.requested(modelData.action)
                    width: buttonContent.width
                    height: 28
                    Row {
                        id: buttonContent
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 5
                        Image {
                            source: (buttonMouse.containsMouse && menu.parts["logoffbuttons-hot"]
                                     ? menu.parts["logoffbuttons-hot"] : menu.parts.logoffbuttons)[button.modelData.action]
                            smooth: false
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: button.modelData.label
                            color: menu.xpColors.footerText
                        }
                    }
                    MouseArea {
                        id: buttonMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: menu.requested(button.modelData.action)
                    }
                }
            }
        }
    }
}
