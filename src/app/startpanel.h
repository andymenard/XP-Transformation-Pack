#pragma once
// The Start menu's art and measures for XPLasma's Start plasmoid: every
// StartPanel part (and its hot state) as uxtheme keys it, with its sizing
// and content margins and pane size; the text colours and fonts; and the
// menu's layout. Only data changes from look to look; the plasmoid's code
// doesn't.
#include "app/look.h"

#include <QJsonObject>
#include <QString>

namespace xpl {

// Writes <directory>/theme.json and its PNGs (by absolute file URL: the
// plasmoid receives the JSON as a setting); returns the JSON, or an empty
// object with *error when a style has no Start panel art. Windows Classic's
// is drawn from the scheme, as XP draws its menu with visual styles off (the
// XP media gives the footer's buttons).
QJsonObject writeStartPanel(const Look &look, const QString &directory, QString *error = nullptr);

} // namespace xpl
