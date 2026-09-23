#pragma once
// XP's own icons as a freedesktop icon theme laid over the user's: shell32's
// folders and drives, user32's message icons, the tray's volume, network,
// Bluetooth and battery states. Each name gets XP's native sizes only (a
// 48px icon is never scaled down over its 16px art); names XP has nothing
// for are inherited from the theme it's laid over.
#include <QString>
#include <QStringList>

namespace xpl {

constexpr const char *kIconThemeId = "xplasma-icons";

// XP's files the icons come from.
QStringList iconSourceFiles();

// Writes the theme to `directory`/xplasma-icons, inheriting `inherit`;
// returns its path, or empty with *error. The XP files come from the media
// (media::systemFile).
QString writeIconTheme(const QString &directory, const QString &inherit, QString *error = nullptr);

} // namespace xpl
