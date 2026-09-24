#pragma once
// XP's own icons as a freedesktop icon theme laid over the user's: shell32's
// folders and drives, user32's message icons, the tray's volume, network,
// Bluetooth and battery states. Each name gets XP's native sizes only (a
// 48px icon is never scaled down over its 16px art); names XP has nothing
// for are inherited from the theme it's laid over.
#include <QColor>
#include <QString>
#include <QStringList>

namespace xpl {

constexpr const char *kIconThemeId = "xplasma-icons";

// A symbolic SVG in one colour: its text parts (KDE's ColorScheme-Text
// class, or plain currentColor) in `text`, its other classes in their own
// colours, written in so the icon loader can't recolour it again.
QByteArray recolouredSvg(const QByteArray &svg, const QColor &text);

// XP's files the icons come from.
QStringList iconSourceFiles();

// Writes the theme to `directory`/xplasma-icons, inheriting `inherit`;
// returns its path, or empty with *error. The XP files come from the media
// (media::systemFile). With `trayGlyphs`, the tray's icons XP has no art
// for are the inherited theme's in that colour (the tray's text colour).
QString writeIconTheme(const QString &directory, const QString &inherit, const QColor &trayGlyphs = QColor(),
                       QString *error = nullptr);

} // namespace xpl
