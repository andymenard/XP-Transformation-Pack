#pragma once
// XPlasma's builds of Plasma's own widgets, for the Plasmas that build them
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
#include "app/session.h"
#include "app/state.h"

#include <QHash>
#include <QJsonArray>
#include <QList>
#include <QString>
#include <QVersionNumber>
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

// --- Installing them ---------------------------------------------------------

// The Plasma installed ("6.6.4": libPlasma's file name, or failing that
// plasmashell --version); empty if unknown.
QString runningPlasma(Session &session);
// The Plasma series a version belongs to: 6.7.5 is 6.7 -- and 6.7.90 and
// up, KDE's betas of the next, are 6.8.
QVersionNumber seriesOf(const QString &version);

struct Choice
{
    QString series;                   // "6.6"
    QHash<QString, QString> plugins;  // widget id -> the shipped plugin
};
// The build this copy ships for `plasma` that provides `ids`: down the
// series from the newest not newer than it, the first whose plugins all
// load here -- against this system's own Qt and Plasma, which is what decides
// (a build for another Plasma doesn't). None, *error saying why each series
// couldn't, if none does.
std::optional<Choice> choose(const QString &plasma, const QStringList &ids, QString *error = nullptr);

// Puts a choice's plugins where the session's Qt looks first, so Plasma
// loads them in place of its own; recorded in `state` for remove().
bool install(Session &session, const Choice &choice, State &state, QString *error = nullptr);
bool installed(const State &state);
void remove(State &state);

// At login, before Plasma's shell starts (XPlasma's env script runs it): the
// builds installed still fit the Plasma there is -- or, after an upgrade to
// another series, the build for that one instead, or none (Plasma's own
// widgets back: a build that doesn't load leaves the panel without them).
enum class Checked { Fine, Replaced, Removed };
Checked check(Session &session, State &state, QString *what = nullptr);

} // namespace xpl::forks
