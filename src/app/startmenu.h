#pragma once
// XPLasma's Start plasmoid (fyi.hotsocket.xplasma.start): the package, which
// ships inside XPLasma and never changes with the look, and the look's data,
// which does -- the Start button's images and the Start menu's art, as the
// JSON the plasmoid takes in its "theme" setting.
#include "app/look.h"

#include <QJsonObject>
#include <QString>

namespace xpl {

constexpr const char *kStartPlasmoidId = "fyi.hotsocket.xplasma.start";

// Installs the package under `plasmoids` (~/.local/share/plasma/plasmoids),
// replacing what's there (xp2plasma's Kickoff fork had this id too).
bool installStartPlasmoid(const QString &plasmoids, QString *error = nullptr);

// The look's Start data into `directory`: the button (with the flag from
// the XP media) and the menu. The JSON the plasmoid is given, or empty with
// *error.
QJsonObject writeStartTheme(const Look &look, const QString &directory, QString *error = nullptr);

} // namespace xpl
