#pragma once
// Text as Windows XP drew it, across the desktop: XP's default "Standard"
// font smoothing. GDI hinted every TrueType font fully, never used colour
// (ClearType was off), never kerned, and smoothed a font only at the sizes
// its own gasp table allowed -- so Tahoma 8 is crisp, one-bit text.
//
// Through fontconfig, which Qt, GTK and browsers all follow: a rules file
// (fontconfig/xplasma-xp-text.conf), included at the end of the user's
// fonts.conf so it's read after KDE's own settings there; the matching Xft
// settings in kdeglobals (KDE's Fonts settings, and GTK through them). Qt
// can't be told not to kern through fontconfig: the XPlasma style does that.
#include "app/session.h"
#include "app/state.h"

#include <QByteArray>
#include <QString>

namespace xpl::xptext {

// The rules for the fonts in `fontsDir` (XP's, as setup imported them).
QByteArray rules(const QString &fontsDir);

bool apply(Session &session, State &state, QString *error = nullptr);
// The user's settings back, and XPlasma's rules gone.
void revert(Session &session, State &state);

} // namespace xpl::xptext
