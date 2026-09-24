#pragma once
// Qt apps' widgets: XPLasma's style plugin, drawing the look itself.
//
// The plugin ships prebuilt for Qt 6 and Qt 5 (each against the oldest Qt of
// its major, so it loads into every newer one) and goes into
// ~/.local/lib/qt<N>/plugins, which the session's QT_PLUGIN_PATH names. The
// look it draws is a frozen, content-addressed copy under
// ~/.local/share/xplasma/active -- every running app reads it, so it never
// changes under them -- named by ~/.config/xplasmarc, which the plugin
// watches: a new apply reaches running apps. Flatpak's Qt 6 apps get the
// same plugin as a runtime extension (flatpak.h).
#include "app/look.h"
#include "app/session.h"
#include "app/state.h"

#include <QStringList>

namespace xpl::qtstyle {

constexpr const char *kStyleKey = "XPLasma";

bool apply(Session &session, const Look &look, State &state, QStringList *log, QString *error = nullptr);
// The session's QT_PLUGIN_PATH, now and at every login, naming XPLasma's
// plugin roots ahead of the system's: the style's, and where Plasma's
// widgets as XPLasma builds them go (forks.h).
bool ensurePluginPath(Session &session, QString *error = nullptr);
// At login: the Flatpak style extension caught up with the runtimes
// installed (one added since, or gone), while XPLasma's style is applied.
// *what says what changed, if anything.
void refreshFlatpak(Session &session, QString *what);
// Back to the widget style the user had; the plugins, the frozen looks and
// the environment hook gone.
void revert(Session &session, State &state, QStringList *log);

// The frozen copy the plugin reads: the style itself, or the Classic scheme
// as a .theme.
QString freeze(const Look &look, QString *error = nullptr);

} // namespace xpl::qtstyle
