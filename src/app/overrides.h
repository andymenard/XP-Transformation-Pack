#pragma once
// Where Plasma's own widgets have to be told about XP: user-local copies of
// KDE's QML, patched, that Plasma prefers over the system's -- and removed
// again on reset, leaving KDE's untouched.
//
//   * The notification area and the clock (org.kde.plasma.systemtray,
//     org.kde.plasma.digitalclock) over the panel theme's xp-tray art, with
//     XP's show-hidden-icons button, 16px icons, and the clock at 96 dpi.
//   * The task manager (org.kde.plasma.taskmanager), with XP's task button
//     layout when the panel theme marks its buttons as XP's TaskBand
//     (hint-xplasma-taskband): one every 160px at most, 2px apart, a 16px
//     icon 10px in and the label 3px after it.
//   * Notifications (org.kde.plasma.notifications), as XP's balloons when
//     the panel theme has their art (widgets/xp-notify): one at a time from
//     the sender's tray icon -- or an icon of its own, next to the others,
//     which blinks if it went unread -- and file transfers as XP's copy
//     dialog.
//   * The desktop (org.kde.desktopcontainment), whose right-click menu gets
//     XP's Properties, as "XP Properties": Display Properties; and where
//     what's dragged from the Start menu lands as a shortcut (a link),
//     without KIO's Move/Copy/Link menu.
//   * KWin's Aurorae renderer (kwin/aurorae/aurorae.qml), for the caption
//     text shadows XP styles give and its QML renderer leaves out, and XP's
//     title bar for dialogs: Close alone, no icon, when a window can neither
//     minimize nor maximize.
//
// Patches are data (overrides/patches.json: text found once, replaced), so a
// Plasma whose QML has moved on is refused with a message, never
// half-patched.
#include <QString>

#include <optional>

namespace xpl::overrides {

bool installPanelWidgets(const QString &plasmoids, QString *error = nullptr);
void removePanelWidgets(const QString &plasmoids);

// The desktop's right-click menu (org.kde.desktopcontainment, which the
// desktop, org.kde.plasma.folder, is): "XP Properties", starting `editor`.
bool installDesktopMenu(const QString &plasmoids, const QString &editor, QString *error = nullptr);
void removeDesktopMenu(const QString &plasmoids);

// `themes` is where the Aurorae themes are (their rc files say which want a
// shadow); `target` the override's path (~/.local/share/kwin/aurorae/aurorae.qml).
bool installTitleShadows(const QString &themes, const QString &target, QString *error = nullptr);
void removeTitleShadows(const QString &target);

// Patch `source` by `file`'s rules in patches.json ("systemtray/main.qml").
std::optional<QString> patched(const QString &file, const QString &source, QString *error = nullptr);

} // namespace xpl::overrides
