#pragma once
// Where XPLasma keeps its files, and where the session looks for what it
// writes.
//
// Every path is built from the home directory, never from XDG_* variables:
// what matters is where the *running Plasma session* reads (~/.local/share,
// ~/.config), and it doesn't consult the environment XPLasma was started
// with. Tests move HOME to a scratch directory.
#include <QString>

namespace xpl::paths {

QString home();

// XPLasma's own files.
QString data();          // ~/.local/share/xplasma
QString assets();        // data()/assets: the imported library
QString themes();        //   .../themes: .msstyles and .theme files
QString wallpapers();    //   .../wallpapers
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

// The prebuilt XPLasma style plugin for a Qt major, as this copy of XPLasma
// ships it: <prefix>/lib/xplasma, beside bin/ (in a checkout, build.sh's
// build/<mode>); empty if there isn't one.
QString shippedStylePlugin(int qtMajor);
// Where Qt apps find it: ~/.local/lib/qt<N>/plugins.
QString stylePluginRoot(int qtMajor);

// xp2plasma's, adopted once (see adopt.h).
QString legacyData();    // ~/.local/share/xp2plasma
QString legacyCache();   // ~/.cache/xp2plasma

} // namespace xpl::paths
