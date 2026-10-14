// SPDX-License-Identifier: GPL-3.0-or-later
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

// The rules for the fonts in `fontsDir` (XP's, as setup imported them), for
// a display at `scale`. The sizes in a gasp table are the sizes text is
// drawn at, but fontconfig is asked about a font at its unscaled size (Qt
// checks Tahoma 11 on a 200% screen as 11 px and draws it at 22): so the
// sizes are divided by the scale, and the smoothing falls where XP's would
// on a screen of that many pixels -- as XP's Large Fonts smoothed the bigger
// text.
QByteArray rules(const QString &fontsDir, double scale = 1.0);

// The main screen's scale (1, 1.25, 2...): KWin's saved setup for the
// screens connected now (~/.config/kwinoutputconfig.json and the kernel's
// connector states), its first-priority screen's. Readable at login, before
// KWin starts. 1 when it can't tell.
double displayScale();
// The same, from that file's contents and the connectors connected ("eDP-1").
double displayScale(const QByteArray &kwinOutputConfig, const QStringList &connected);

bool apply(Session &session, State &state, QString *error = nullptr);
// The rules written again for the display's scale now, if it's changed since
// they were (at login: a scale changed in the last session); whether it was.
bool refresh(Session &session, State &state);

// GTK 4 at a fractional scale draws the one-bit glyphs the rules ask for
// and then stretches them, and a one-pixel stem stretched by a fraction can
// vanish ("k" came out "<" at 125%). There, GTK 4's font rendering is made
// "manual" (GNOME's interface settings), so it smooths all its text as its
// antialiasing setting says; at whole scales, and on revert, the user's own
// setting is back. Qt and GTK 3 draw the rules' one-bit text cleanly.
void setGtk4Rendering(Session &session, State &state, double scale);
// The user's settings back, and XPlasma's rules gone.
void revert(Session &session, State &state);

} // namespace xpl::xptext
