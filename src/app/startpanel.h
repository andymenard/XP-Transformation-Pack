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

// Writes <directory>/theme.json and its PNGs (named relative to it); returns
// the JSON, or an empty object with *error when the style has no Start
// panel (Windows Classic, and styles that leave it out).
QJsonObject writeStartPanel(const Look &look, const QString &directory, QString *error = nullptr);

} // namespace xpl
