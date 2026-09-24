#pragma once
// XPLasma's builds of Plasma's own widgets, for the Plasmas that build them
// into their plugins (from 6.5 on) and so can't be patched from the home
// folder: each series' upstream source (pinned tarballs), patched with the
// same rules as the packaged widgets (overrides/patches.json) -- or the
// series' own where its source has moved on -- and built in that series'
// container (build-forks.sh) against its libraries and Qt floor, the QML
// kept as source so the plugin loads into any Qt from that floor on.
//
// overrides/forks.json holds each series: its container image, its source
// tarballs, and per widget where its files are in the source, what's added
// to it, and the extra QML files its build registers.
#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace xpl::forks {

struct Source
{
    QString project;   // "plasma-workspace"
    QString version;   // "6.6.4"
    QString sha256;
    QString dir() const { return project + QLatin1Char('-') + version; }
};

struct Widget
{
    QString id;                       // plugin id and build target: "org.kde.plasma.systemtray"
    QString project;                  // which source it's in
    QString dir;                      // its directory there: "applets/systemtray"
    QHash<QString, QString> files;    // patches.json key -> file under dir
    QHash<QString, QString> extras;   // overrides/ file -> file under dir (added)
    QHash<QString, QString> copies;   // file under dir (added) <- original file under dir, unpatched
    QHash<QString, QStringList> registered;   // QML module path ("" the root) -> added files to build
};

struct Series
{
    QString name;      // "6.6"
    QString image;     // its build container
    QString qtFloor;   // the oldest Qt this Plasma series runs on ("6.10"): the plugins need no newer
    QList<Source> sources;
    QList<Widget> widgets;
    QHash<QString, QJsonArray> rules;   // patches.json key -> this series' rules instead;
                                        // "extra/<file>" -> rules for an added file
};

QStringList seriesNames();
std::optional<Series> series(const QString &name, QString *error = nullptr);

// Makes `work` (holding <project>-<version>/ trees) the series' patched
// source, from the pristine trees under `pristine`: only files whose content
// differs are written, so a build there is incremental. Returns how many were
// written (or removed); *error lists each file that couldn't be patched.
std::optional<int> syncSources(const Series &series, const QString &pristine, const QString &work, QString *error = nullptr);

} // namespace xpl::forks
