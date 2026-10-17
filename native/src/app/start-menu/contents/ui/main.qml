/*
    XPlasma's Start button and Start menu: a launcher of its own, on Plasma's
    application models (the ones Kickoff, Plasma's standard launcher, uses).

    The look is data: XPlasma writes it into the "theme" setting when it
    applies a look -- the button's normal / hot / pressed images, and the
    Start menu's art, colours, fonts and layout. Nothing here changes per
    look.

    SPDX-License-Identifier: GPL-3.0-or-later
*/
import QtQuick
import "LiveColors.js" as LiveColors
import QtQuick.Layouts
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.private.kicker as Kicker
import org.kde.kirigami as Kirigami

PlasmoidItem {
    id: start
    objectName: "xplasmaStartRoot"

    readonly property bool liveTintAvailable: false
    readonly property var liveColors: ({})
    readonly property var themeData: {
        try {
            return JSON.parse(Plasmoid.configuration.theme || "{}");
        } catch (e) {
            return {};
        }
    }
    readonly property bool xpExplorerEnabled: Plasmoid.configuration.xpExplorerEnabled || themeData.xpExplorerEnabled === true
    readonly property bool xpControlPanelEnabled: Plasmoid.configuration.xpControlPanelEnabled || themeData.xpControlPanelEnabled === true
    readonly property int recentCount: Plasmoid.configuration.recentCount
    // A look with Start panel art gets XP's two-column menu.
    readonly property bool xpMenu: start.themeData.parts !== undefined

    readonly property Kicker.RootModel rootModel: Kicker.RootModel {
        autoPopulate: false
        appletInterface: start
        appNameFormat: 0   // the name only, as XP's menus show it
        flat: true
        sorted: Plasmoid.configuration.alphaSort
        showSeparators: false
        showTopLevelItems: true
        showAllApps: true
        showAllAppsCategorized: false
        showRecentApps: false
        showRecentDocs: false
        showPowerSession: false
        showFavoritesPlaceholder: true
        Component.onCompleted: {
            // Kickoff's client name: a panel's favourites carry over from it.
            favoritesModel.initForClient("org.kde.plasma.kickoff.favorites.instance-" + Plasmoid.id);
            if (!Plasmoid.configuration.favoritesPortedToKAstats) {
                if (favoritesModel.count < 1)
                    favoritesModel.portOldFavorites(Plasmoid.configuration.favorites);
                Plasmoid.configuration.favoritesPortedToKAstats = true;
            }
        }
    }

    // The whole height of the panel, as XP's taskbar has its button.
    Plasmoid.constraintHints: Plasmoid.CanFillArea
    Plasmoid.icon: "start-here-kde"

    // XP's window sounds, from the first screen's Start button only.
    XpWindowSounds {
        enabled: Plasmoid.containment !== null && Plasmoid.containment.screen === 0
    }
    // XP's tooltip over Start (not the widget's name and description)
    toolTipMainText: i18n("Click here to begin")
    toolTipSubText: ""
    preferredRepresentation: compactRepresentation
    compactRepresentation: StartButton { launcher: start }
    fullRepresentation: start.xpMenu ? xpMenu : null
    switchWidth: Kirigami.Units.gridUnit * 10
    switchHeight: Kirigami.Units.gridUnit * 10

    Component {
        id: xpMenu
        XpFullRepresentation { launcher: start; focus: true }
    }

    Kicker.ProcessRunner { id: processRunner }
    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: i18n("Edit Applications…")
            icon.name: "kmenuedit"
            visible: Plasmoid.immutability !== PlasmaCore.Types.SystemImmutable
            onTriggered: processRunner.runMenuEditor()
        }
    ]

    // The Windows key opens it, as XP's did.
    Component.onCompleted: {
        Plasmoid.configuration.liveTintSupported = liveTintAvailable;
        if (Plasmoid.hasOwnProperty("activationTogglesExpanded"))
            Plasmoid.activationTogglesExpanded = true;
    }
}
