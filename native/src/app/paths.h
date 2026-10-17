// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Where XPlasma keeps its files, and where the session looks for what it
// writes.
//
// Every path is built from the home directory, never from XDG_* variables:
// what matters is where the *running Plasma session* reads (~/.local/share,
// ~/.config), and it doesn't consult the environment XPlasma was started
// with. Tests move HOME to a scratch directory.
#include <QString>
#include <QStringList>

namespace xpl::paths {

QString home();

// XPlasma's own files.
QString data();          // ~/.local/share/xplasma
QString assets();        // data()/assets: the imported library
QString themes();        //   .../themes: .msstyles and .theme files
QString wallpapers();    //   .../wallpapers
QString xpFonts();       // ~/.local/share/fonts/xp: XP's fonts, from its media
QString schemes();       // data()/schemes: saved Classic schemes (.theme)
QString extracted();     // data()/extracted: files taken out of XP's media once (the Start flag...)
QString statePath();     // data()/state.json

// Regenerable, apart from the XP media download.
QString cache();         // ~/.cache/xplasma
QString downloads();     //   .../downloads: the XP ISO and theme packs
QString build();         //   .../build: generated packages before install

// What the session reads.
QString config();        // ~/.config
QString localShare();    // ~/.local/share

// The system's shared data folders, where Plasma's own packages are:
// XDG_DATA_DIRS (/usr/local/share:/usr/share by default; NixOS's profiles,
// Flatpak's exports...), in order, without the user's own.
QStringList systemData();
// The first of them holding `relative` ("plasma/plasmoids/<id>",
// "icons/breeze"), as a full path; empty if none does.
QString findSystemData(const QString &relative);

// The prebuilt XPlasma style plugin for a Qt major, as this copy of XPlasma
// ships it: <prefix>/lib/xplasma, beside bin/ (in a checkout, build.sh's
// build/<mode>); empty if there isn't one.
QString shippedStylePlugin(int qtMajor);
// Plasma's widgets as XPlasma builds them for the Plasmas that build them in
// (build-forks.sh): <prefix>/lib/xplasma/forks/<series>/plasma/applets/
// <id>.so; empty if this copy has none.
QString shippedForks();
// XPlasma's KWin window decoration, and the Plasma version it was built
// against beside it ("<plugin>.plasma"); empty if this copy has none.
QString shippedDecoration();
// Where Qt apps find it: ~/.local/lib/qt<N>/plugins.
QString stylePluginRoot(int qtMajor);

// `dir` and the folders it's in, as long as each is empty, up to (not
// including) `top` -- what's left of a tree once XPlasma's files are out.
void removeEmptyDirs(const QString &dir, const QString &top);

// xp2plasma's, adopted once (see adopt.h).
QString legacyData();    // ~/.local/share/xp2plasma
QString legacyCache();   // ~/.cache/xp2plasma

} // namespace xpl::paths
