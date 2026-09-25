#pragma once
// Flatpak apps' widgets: XPLasma's Qt 6 style plugin as a KDE runtime style
// extension (org.kde.KStyle.XPLasma), so Qt apps from Flathub draw the look
// too.
//
// KDE's runtimes have an org.kde.KStyle extension point whose styles/ folders
// are merged into the apps' Qt plugin path. The plugin needs nothing past Qt
// itself, so no build against the runtime is needed: it goes in as an
// "unmaintained" extension -- a plain folder Flatpak finds on its own,
// ~/.local/share/flatpak/extension/org.kde.KStyle.XPLasma/ARCH/BRANCH -- one
// per org.kde.Platform 6 branch installed. Apps choose the style from
// kdeglobals as on the host, and read the look through a user-wide Flatpak
// override letting every app see ~/.local/share/xplasma/active read-only:
// the frozen look, and a copy of xplasmarc beside it. (A folder, not
// ~/.config/xplasmarc itself: a file shared into a sandbox stays the one
// there was when the app started, where a folder's contents follow along,
// so a new look reaches running apps.)
#include "app/session.h"

#include <QStringList>

namespace xpl::flatpak {

constexpr const char *kExtensionId = "org.kde.KStyle.XPLasma";
// What the override lets every app read.
const QStringList &readOnlyPaths();

// The org.kde.Platform 6 branches installed ("6.10", ...), user and system.
QStringList qtRuntimes(Session &session);
// The extension for each of them (and none for a branch gone), and the
// override. Nothing to do without Flatpak or a Qt 6 runtime; *log says what
// was done.
bool install(Session &session, const QString &plugin, QStringList *log, QString *error = nullptr);
// The extension and XPLasma's part of the override gone (the rest of the
// user's overrides left as they are).
void remove(QStringList *log = nullptr);

// GTK: the look's GTK 3 theme (`themeRoot`, gtk::writeTheme's, named `name`)
// as an org.gtk.Gtk3theme extension -- every GTK runtime's, version 3.22,
// mounted as the theme's gtk-3.0 folder, so its images go inside it and its
// stylesheet names them there -- and, for libadwaita apps, the user's GTK 4
// stylesheet and the themes folder readable by every app (the stylesheet
// imports XPLasma's layer from there). Nothing to do without Flatpak.
bool installGtk(Session &session, const QString &themeRoot, const QString &name, QStringList *log, QString *error = nullptr);
void removeGtk(QStringList *log = nullptr);
const QStringList &gtkReadOnlyPaths();
QString gtkExtensionRoot(const QString &name);

// Where the extension goes, and the override file (for tests).
QString extensionRoot();
QString overrideFile();

} // namespace xpl::flatpak
