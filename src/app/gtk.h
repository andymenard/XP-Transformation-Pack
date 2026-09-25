#pragma once
// GTK 3 apps' widgets: a GTK theme generated from the look, since GTK has no
// style plugins. ~/.local/share/themes/<look>/gtk-3.0/gtk.css starts from
// GTK's own Adwaita (so nothing is ever left unstyled) and lays XP over it:
// the look's system colours, and its controls -- buttons, check boxes and
// radio buttons, text fields, scroll bars, progress bars, tabs, sliders,
// combo boxes, menus, tooltips -- as images XPlasma's Qt style draws for the
// look (Classic too), each a CSS border-image cut 3px in so the controls keep
// XP's sizes.
//
// Applying switches GTK to it through Plasma's own GTK settings (kded's
// gtkconfig, as System Settings does), keeping the theme there was for reset.
#include "app/look.h"
#include "app/session.h"
#include "app/state.h"

#include <QString>
#include <QStringList>

namespace xpl::gtk {

// A stylesheet's rules made to outrank Adwaita's (which the theme starts
// from): each selector's subject given four classes it can't have.
QString boosted(const QString &css);
// The theme's name for `look` (its folder under ~/.local/share/themes).
QString themeName(const Look &look);
// Writes the theme into `directory`/NAME; returns its path, or empty with
// *error. Needs XPlasma's style plugin (it draws the controls).
QString writeTheme(const Look &look, const QString &directory, QString *error = nullptr);
// Installs it and switches GTK to it; *log says what was done.
bool apply(Session &session, const Look &look, State &state, QStringList *log, QString *error = nullptr);
// GTK back to the theme it had, XPlasma's themes gone.
void revert(Session &session, State &state, QStringList *log);

} // namespace xpl::gtk
