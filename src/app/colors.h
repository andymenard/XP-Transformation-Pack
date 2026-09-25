#pragma once
// The system appearance as Plasma settings: a KDE colour scheme (.colors)
// and the fonts kdeglobals names.
#include "core/classic.h"

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QString>

#include <utility>

namespace xpl {

// A KDE colour scheme from XP's system colours. KDE's Window group is the
// dialog face -- XP's ButtonFace -- and its View group the document area,
// XP's Window colour.
// (`complementary`: the taskbar's background and text, for the Complementary
// set -- XPlasma's taskbar widgets take it, and Plasma up to 6.3 reads it
// from here, not from the Plasma theme; the button face's otherwise.)
QByteArray colorScheme(const ClassicScheme &system, const QString &name,
                       const std::pair<QColor, QColor> &complementary = {});

// One key in a KConfig file under ~/.config.
struct ConfigValue
{
    QString file;    // "kdeglobals"
    QString group;   // "General", or "WM"
    QString key;
    QString value;
};

// kdeglobals' fonts: the UI font (XP's message font: Tahoma 8), menus,
// toolbars, and the title bar font KWin draws captions with.
QList<ConfigValue> fontSettings(const ClassicScheme &system);

// A LOGFONT as QFont::toString() writes it (what kdeglobals holds).
QString qtFontString(const LogFont &font);

} // namespace xpl
