
    property bool xpTrayExpanded: false
    readonly property var xpHiddenItems: Plasmoid.configuration.hiddenItems
    function xpExplicitlyHidden(itemId) {
        return root.xpTrayAvailable && !root.vertical && xpHiddenItems.indexOf(itemId) !== -1;
    }
    function xpRevealHidden(itemId) {
        return root.xpTrayExpanded && xpExplicitlyHidden(itemId);
    }
    function xpRefreshFilters() {
        if (activeModel) activeModel.invalidateFilter();
        if (hiddenModel) hiddenModel.invalidateFilter();
        if (xpExpandableModel) xpExpandableModel.invalidateFilter();
    }
    onXpTrayExpandedChanged: xpRefreshFilters()
    onXpHiddenItemsChanged: xpRefreshFilters()

    KItemModels.KSortFilterProxyModel {
        id: xpExpandableModel
        filterRoleName: "effectiveStatus"
        filterRowCallback: (sourceRow, sourceParent) => {
            const index = sourceModel.index(sourceRow, 0, sourceParent);
            // ItemId is the same base role used by KDE's ConfigEntries.qml.
            return sourceModel.data(index, filterRole) === PlasmaCore.Types.PassiveStatus
                && root.xpExplicitlyHidden(sourceModel.data(index, Qt.UserRole + 2));
        }
        Component.onCompleted: sourceModel = Plasmoid.systemTrayModel
    }
