#pragma once
// First-run setup: what XPLasma needs on this machine. None of it ships with
// XPLasma -- XP's media, its styles and fonts are Microsoft's -- so it's taken
// from media the user has, or fetched from archive.org:
//
//   * XP's install media (an ISO, or a folder holding I386): the fonts, the
//     Luna style, every .theme preset, the wallpapers. Only what's used is
//     taken out (a few MB of ~600), and the media is recorded, for the files
//     taken from it later (the Start flag, the icons).
//   * archive.org: XP's media itself, and the official theme packs (Royale,
//     Royale Noir, Zune, Embedded). Items are named; their files are read from
//     archive.org's metadata, not hardcoded.
//
// A cancelled or failed step removes what it added.
#include <QHash>
#include <QList>
#include <QString>

#include <atomic>
#include <functional>

namespace xpl::setup {

using Progress = std::function<void(const QString &line)>;
using Bytes = std::function<void(qint64 done, qint64 total)>;

// Is this a running Plasma 6 session? (and what it said it was)
bool plasmaSession(QString *detail = nullptr);

struct Check
{
    QString tool, purpose, found;   // found: its path, or empty
    bool neededToApply = false;
    bool ok() const { return !found.isEmpty(); }
};
QList<Check> preflight();

struct Imported
{
    int fonts = 0, styles = 0, themes = 0, wallpapers = 0;
};
// XP's desktop wallpapers, as its setup INFs install them (SHL_IMG.INF's
// Web\Wallpaper, ACCESSOR.INF's "Desktop Wallpaper" component): each one's
// file name on the media, lower case ("stoneh.jpg"), to the name XP gave it
// ("Stonehenge.jpg").
QHash<QString, QString> wallpaperNames(const QList<QByteArray> &infs);

// XP's install media. Cancelled or failed, nothing it added stays. Its
// wallpapers are XP's own list, by XP's names; files an older XPLasma
// imported by their 8.3 names are renamed (and what refers to them -- the
// desktop, saved themes -- follows), or removed if XP never listed them.
bool importMedia(const QString &path, Imported *counts, const Progress &progress, const std::atomic_bool *cancel = nullptr,
                 QString *error = nullptr);

struct Download
{
    QString item;       // archive.org item
    QString file;       // its file, or "*.ext" (the largest such)
    QString label;
    bool optional = false;
    bool media = false; // XP's install media: imported, not unpacked
};
// XP's media, and the official packs.
QList<Download> downloads();

// Fetched (unless the downloads cache has it), then unpacked into the theme
// library -- or, for the media, imported. archive.org's address is
// $XPLASMA_ARCHIVE_URL when set (the tests' own server).
bool fetch(const Download &download, const Progress &progress, const Bytes &bytes = {},
           const std::atomic_bool *cancel = nullptr, QString *error = nullptr);

} // namespace xpl::setup
