import QtQuick
import QtCore
import org.kde.coreaddons as KCoreAddons
import org.kde.kitemmodels as KItemModels
import org.kde.kcmutils as KCM
import org.kde.plasma.private.kicker as Kicker
import org.kde.plasma.private.sessions as Sessions
import org.kde.plasma.workspace.dbus as DBus

Item {
    id: backend
    required property var launcher
    // Dry-run mode is used only by the test harness: never invoke host actions.
    property bool dryRun: false
    signal actionDispatched(var action)
    signal browseRequested(var sourceModel, string title, bool categories)
    signal closeRequested()
    property var pinned: []
    property var recent: []
    readonly property int recentLimit: Math.max(0, 8 - pinned.length)
    property var allApps: null
    readonly property string userName: user.fullName || user.loginName
    readonly property url userPicture: user.faceIconUrl
    readonly property bool canLogout: session.canLogout
    readonly property bool canShutdown: session.canShutdown

    KCoreAddons.KUser { id: user }
    Sessions.SessionManagement { id: session }
    Kicker.RecentUsageModel {
        id: usage
        shownItems: Kicker.RecentUsageModel.OnlyApps
        ordering: Kicker.RecentUsageModel.Recent
        favoritesModel: backend.launcher.rootModel.favoritesModel
        onCountChanged: Qt.callLater(backend.refresh)
    }
    Kicker.RecentUsageModel { id: documents; shownItems: Kicker.RecentUsageModel.OnlyDocs }

    function role(model, row, name) {
        const id = model.KItemModels.KRoleNames.role(name);
        return id >= 0 ? model.data(model.index(row, 0), id) : null;
    }
    function entries(model, limit, excluded) {
        let result = [];
        if (!model) return result;
        for (let row = 0; row < model.count && result.length < limit; ++row) {
            const label = String(model.data(model.index(row, 0), Qt.DisplayRole) || "");
            if (!label || role(model, row, "isSeparator")) continue;
            const identity = String(role(model, row, "favoriteId") || role(model, row, "url") || label);
            if (excluded.indexOf(identity) !== -1) continue;
            result.push({label: label, icon: model.data(model.index(row, 0), Qt.DecorationRole),
                         description: String(role(model, row, "description") || ""), identity: identity,
                         action: {model: model, row: row, identity: identity}});
        }
        return result;
    }
    function refresh() {
        const favorites = launcher.rootModel.favoritesModel;
        // Use the launcher's real Plasma favorites, including order and an
        // intentionally empty list. No private pins or synthetic defaults.
        pinned = entries(favorites, favorites ? favorites.count : 0, []);
        const excluded = pinned.map(entry => entry.identity);
        let result = entries(usage, recentLimit, excluded);
        for (const entry of result) {
            entry.description = "";
        }
        recent = result;
        // Keep KDE's category tree instead of selecting KICKER_ALL_MODEL.
        allApps = launcher.rootModel;
    }
    function pathUrl(location) {
        // QtCore's QML StandardPaths wrapper already returns an encoded URL.
        return String(StandardPaths.writableLocation(location));
    }
    readonly property var places: [
        [{label: "Documents", icon: "folder-documents", action: {url: pathUrl(StandardPaths.DocumentsLocation)}},
         {label: "Recent Files", icon: "folder-recent-files", action: "recent-documents"},
         {label: "Pictures", icon: "folder-pictures", action: {url: pathUrl(StandardPaths.PicturesLocation)}},
         {label: "Music", icon: "folder-music", action: {url: pathUrl(StandardPaths.MusicLocation)}},
         {label: "File System", icon: "computer", action: {url: "file:///"}}],
        [{label: "System Settings", icon: "preferences-system", action: {kcm: ""}},
         {label: "Default Applications", icon: "preferences-other", action: {kcm: "kcm_componentchooser"}},
         {label: "Printers", icon: "printer", action: {kcm: "kcm_printer_manager"}}],
        [{label: "Help Center", icon: "help-contents", action: {url: "help:/"}},
         {label: "Search", icon: "edit-find", action: "search"},
         {label: "Run Command...", icon: "system-run", action: "run"}]
    ]
    function activate(action) {
        if (dryRun) { actionDispatched(action); return; }
        if (action === "all-programs") {
            refresh();
            if (allApps) browseRequested(allApps, "All Programs", true);
            return;
        }
        if (action === "recent-documents") { browseRequested(documents, "Recent Files", false); return; }
        if (typeof action === "object" && action.model) {
            // Resolve the stable ID again: applications can be installed/removed
            // or usage reordered while a menu is open. Never launch a stale row.
            const current = entries(action.model, action.model.count, []).find(entry => entry.identity === action.identity);
            if (!current) { refresh(); return; }
            action.model.trigger(current.action.row, "", null);
        } else if (typeof action === "object" && action.url) {
            Qt.openUrlExternally(action.url);
        } else if (typeof action === "object" && action.kcm !== undefined) {
            KCM.KCMLauncher.openSystemSettings(action.kcm);
        } else if (action === "logoff" && canLogout) {
            session.requestLogout(Sessions.SessionManagement.ConfirmationMode.ForcePrompt);
        } else if (action === "shutdown" && canShutdown) {
            session.requestShutdown(Sessions.SessionManagement.ConfirmationMode.ForcePrompt);
        } else if (action === "run" || action === "search") {
            DBus.SessionBus.asyncCall({service: "org.kde.krunner", path: "/App",
                iface: "org.kde.krunner.App", member: "display", arguments: []});
        } else if (action !== "close") { return; }
        closeRequested();
    }
    Connections {
        target: backend.launcher.rootModel
        function onRefreshed() { backend.refresh(); }
        function onCountChanged() { Qt.callLater(backend.refresh); }
    }
    Connections {
        target: backend.launcher.rootModel.favoritesModel
        function onCountChanged() { backend.refresh(); }
        function onDataChanged() { backend.refresh(); }
        function onModelReset() { backend.refresh(); }
        function onRowsMoved() { Qt.callLater(backend.refresh); }
        function onLayoutChanged() { Qt.callLater(backend.refresh); }
        function onFavoritesChanged() { Qt.callLater(backend.refresh); }
    }
    Component.onCompleted: refresh()
}
