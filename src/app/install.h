#pragma once
// XPlasma's own files onto the machine, and off again: what install.sh does,
// for the Setup wizard (xplasma-setup). A package (a release tree: bin/,
// lib/xplasma/, share/applications/) is copied into a prefix (~/.local by
// default) -- the programs, the style plugins, the builds of Plasma's
// widgets, the desktop entries pointed at the installed editor -- each file
// written under a fresh name and renamed into place, so a running program
// keeps the one it has open. The prefix is kept in State's settings, for
// Repair and Remove.
#include <QString>
#include <QStringList>

#include <functional>

namespace xpl::installer {

using Progress = std::function<void(const QString &line)>;

// The package this program runs from: the folder above its bin/.
QString packageRoot();
// Whether `root` holds a package (its bin/xplasma, at least).
bool isPackage(const QString &root);
// Where XPlasma is installed (State's record, else ~/.local if its bin/xplasma
// is there), or empty.
QString installedPrefix();

// The package's files into `prefix` (recorded as the install's prefix).
bool installFiles(const QString &package, const QString &prefix, const Progress &progress, QString *error = nullptr);
// XPlasma's files out of `prefix` (its record gone). With `extracted`, also
// what it took out of Windows XP and made of it (the fonts, the theme
// library, its settings); with `downloads`, the downloaded Windows XP CD
// image and theme packs.
void removeFiles(const QString &prefix, bool extracted, bool downloads, const Progress &progress);
// How much the downloads take (bytes), for the wizard to say.
qint64 downloadsSize();
// The paths installFiles writes, relative to the prefix (for removal).
QStringList installedPaths();

} // namespace xpl::installer
