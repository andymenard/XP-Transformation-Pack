// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/forks.h"

#include "app/overrides.h"
#include "app/paths.h"
#include "app/qttarget.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QVersionNumber>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace xpl::forks {

namespace {

QJsonObject catalogue()
{
    return QJsonDocument::fromJson(overrides::resourceText(QStringLiteral("forks.json")).toUtf8()).object();
}

QHash<QString, QString> stringMap(const QJsonValue &v)
{
    QHash<QString, QString> out;
    const QJsonObject o = v.toObject();
    for (auto it = o.begin(); it != o.end(); ++it)
        out.insert(it.key(), it.value().toString());
    return out;
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

std::optional<QByteArray> readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return f.readAll();
}

// The build file's changes: the widget's QML kept as source -- code compiled
// ahead from it calls Qt's private API, which would tie the build to one Qt
// (set first: Qt decides as the files are registered) -- and the files it
// adds registered with the widget's QML module.
QByteArray cmakeEdited(const QByteArray &original, const Widget &w)
{
    QByteArray out = "# --- XPlasma: this widget's QML kept as source (code compiled ahead from it\n"
                     "# calls Qt's private API, which would tie the build to one exact Qt).\n"
                     "file(GLOB_RECURSE _xplasma_qml RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS *.qml *.js)\n"
                     "set_source_files_properties(${_xplasma_qml} PROPERTIES QT_QML_SKIP_CACHEGEN TRUE)\n"
                     "# ---\n\n";
    out += original;
    if (!w.registered.isEmpty()) {
        out += "\n# --- XPlasma: the files it adds.\n";
        QStringList paths = w.registered.keys();
        paths.sort();
        for (const QString &path : std::as_const(paths)) {
            out += "ecm_target_qml_sources(" + w.id.toUtf8() + " SOURCES";
            for (const QString &f : w.registered.value(path))
                out += ' ' + f.toUtf8();
            if (!path.isEmpty())
                out += " PATH " + path.toUtf8();
            out += ")\n";
        }
    }
    return out;
}

} // namespace

QStringList seriesNames()
{
    QStringList names = catalogue().keys();
    names.sort();
    return names;
}

std::optional<Series> series(const QString &name, QString *error)
{
    const QJsonObject all = catalogue();
    if (!all.contains(name)) {
        fail(error, QStringLiteral("no Plasma series %1 in forks.json (there are: %2)").arg(name, seriesNames().join(QStringLiteral(", "))));
        return std::nullopt;
    }
    const QJsonObject o = all.value(name).toObject();
    Series s;
    s.name = name;
    s.image = o.value(QStringLiteral("image")).toString();
    s.qtFloor = o.value(QStringLiteral("qtFloor")).toString();
    const QJsonObject sources = o.value(QStringLiteral("sources")).toObject();
    for (auto it = sources.begin(); it != sources.end(); ++it) {
        const QJsonObject src = it.value().toObject();
        s.sources.append({it.key(), src.value(QStringLiteral("version")).toString(), src.value(QStringLiteral("sha256")).toString()});
    }
    for (const QJsonValue &v : o.value(QStringLiteral("widgets")).toArray()) {
        const QJsonObject wo = v.toObject();
        Widget w;
        w.id = wo.value(QStringLiteral("id")).toString();
        w.project = wo.value(QStringLiteral("project")).toString();
        w.dir = wo.value(QStringLiteral("dir")).toString();
        w.files = stringMap(wo.value(QStringLiteral("files")));
        w.extras = stringMap(wo.value(QStringLiteral("extras")));
        w.copies = stringMap(wo.value(QStringLiteral("copies")));
        const QJsonObject reg = wo.value(QStringLiteral("register")).toObject();
        for (auto it = reg.begin(); it != reg.end(); ++it) {
            QStringList files;
            for (const QJsonValue &f : it.value().toArray())
                files << f.toString();
            w.registered.insert(it.key(), files);
        }
        s.widgets.append(w);
    }
    const QJsonObject rules = o.value(QStringLiteral("rules")).toObject();
    for (auto it = rules.begin(); it != rules.end(); ++it)
        s.rules.insert(it.key(), it.value().toArray());
    return s;
}

std::optional<int> syncSources(const Series &series, const QString &pristine, const QString &work, QString *error)
{
    // First what differs from each pristine tree (by path relative to it):
    // nothing is written unless every file patches.
    QStringList problems;
    QHash<QString, QHash<QString, QByteArray>> changes;   // source dir -> path -> content
    for (const Source &src : series.sources) {
        const QString from = QDir(pristine).filePath(src.dir());
        if (!QFileInfo(from).isDir()) {
            fail(error, QStringLiteral("%1 isn't unpacked").arg(from));
            return std::nullopt;
        }
        QHash<QString, QByteArray> &changed = changes[src.dir()];
        for (const Widget &w : series.widgets) {
            if (w.project != src.project)
                continue;
            const QString base = w.dir + QLatin1Char('/');
            for (auto it = w.files.cbegin(); it != w.files.cend(); ++it) {
                const auto original = readFile(from + QLatin1Char('/') + base + it.value());
                if (!original) {
                    problems << QStringLiteral("%1: %2 isn't in the source").arg(w.id, base + it.value());
                    continue;
                }
                QString why;
                const auto own = series.rules.constFind(it.key());   // (the series' rules, if it has its own)
                const QJsonArray *rules = own != series.rules.cend() ? &own.value() : nullptr;
                const auto text = overrides::patched(it.key(), QString::fromUtf8(*original), &why, rules);
                if (!text)
                    problems << QStringLiteral("%1: %2").arg(w.id, why);
                else
                    changed.insert(base + it.value(), text->toUtf8());
            }
            for (auto it = w.copies.cbegin(); it != w.copies.cend(); ++it) {
                const auto original = readFile(from + QLatin1Char('/') + base + it.value());
                if (original)
                    changed.insert(base + it.key(), *original);
                else
                    problems << QStringLiteral("%1: %2 isn't in the source").arg(w.id, base + it.value());
            }
            // (XPlasma's own files too, where the series has rules for them:
            // "extra/<file>" -- an import that moved, say.)
            for (auto it = w.extras.cbegin(); it != w.extras.cend(); ++it) {
                QString text = overrides::resourceText(it.key());
                const auto own = series.rules.constFind(QLatin1String("extra/") + it.key());
                if (own != series.rules.cend()) {
                    QString why;
                    const auto adapted = overrides::patched(QLatin1String("extra/") + it.key(), text, &why, &own.value());
                    if (!adapted) {
                        problems << QStringLiteral("%1: %2").arg(w.id, why);
                        continue;
                    }
                    text = *adapted;
                }
                changed.insert(base + it.value(), text.toUtf8());
            }
            const auto cmake = readFile(from + QLatin1Char('/') + base + QLatin1String("CMakeLists.txt"));
            if (!cmake)
                problems << QStringLiteral("%1: no CMakeLists.txt in %2").arg(w.id, w.dir);
            else
                changed.insert(base + QLatin1String("CMakeLists.txt"), cmakeEdited(*cmake, w));
        }
    }
    if (!problems.isEmpty()) {
        fail(error, problems.join(QLatin1Char('\n')));
        return std::nullopt;
    }

    // Then every file as it should be, written only where it isn't.
    int written = 0;
    for (const Source &src : series.sources) {
        const QString from = QDir(pristine).filePath(src.dir());
        const QString to = QDir(work).filePath(src.dir());
        QHash<QString, QByteArray> changed = changes.value(src.dir());
        QSet<QString> wanted;
        auto put = [&](const QString &rel, const QByteArray &bytes) {
            wanted.insert(rel);
            const QString path = to + QLatin1Char('/') + rel;
            if (const auto current = readFile(path); current && *current == bytes)
                return true;
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile f(path);
            if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
                return fail(error, QStringLiteral("can't write %1").arg(path));
            ++written;
            return true;
        };
        QDirIterator it(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString rel = it.next().mid(from.size() + 1);
            const QByteArray bytes = changed.contains(rel) ? changed.take(rel) : readFile(it.filePath()).value_or(QByteArray());
            if (!put(rel, bytes))
                return std::nullopt;
        }
        for (auto c = changed.cbegin(); c != changed.cend(); ++c)
            if (!put(c.key(), c.value()))
                return std::nullopt;
        // (Files added by an earlier sync and no longer wanted: gone.)
        QDirIterator old(to, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (old.hasNext()) {
            const QString rel = old.next().mid(to.size() + 1);
            if (!wanted.contains(rel) && QFile::remove(old.filePath()))
                ++written;
        }
    }
    return written;
}

QString runningPlasma(Session &session)
{
    // libPlasma's file name first -- libPlasma.so.6.7.5, released with
    // Plasma, the same version -- as it needs nothing started: at login,
    // before the session's display is up, plasmashell can't start even to
    // say its version, and aborts (a "crash" the user would be told of).
    // (Where Qt's libraries are, and the usual places.)
    QStringList dirs{QLibraryInfo::path(QLibraryInfo::LibrariesPath)};
    for (const char *d : {"/usr/lib64", "/usr/lib", "/usr/lib/x86_64-linux-gnu", "/usr/local/lib64", "/usr/local/lib"})
        dirs << QLatin1String(d);
    static const QRegularExpression library(QStringLiteral("^libPlasma\\.so\\.(\\d+\\.\\d+\\.\\d+)$"));
    for (const QString &dir : std::as_const(dirs))
        for (const QString &name : QDir(dir).entryList({QStringLiteral("libPlasma.so.*")}, QDir::Files))
            if (const auto m = library.match(name); m.hasMatch())
                return m.captured(1);
    // Failing that, plasmashell's own word -- started as the session starts
    // it, not in this program's offscreen platform, which Plasma 6.6's
    // crashes in.
    static const QRegularExpression version(QStringLiteral("(\\d+\\.\\d+(\\.\\d+)?)"));
    const host::Result r = session.run({QStringLiteral("env"), QStringLiteral("-u"), QStringLiteral("QT_QPA_PLATFORM"),
                                        QStringLiteral("plasmashell"), QStringLiteral("--version")});
    if (const auto m = version.match(QString::fromUtf8(r.out)); m.hasMatch())
        return m.captured(1);
    return QString();
}

QVersionNumber seriesOf(const QString &version)
{
    const QVersionNumber v = QVersionNumber::fromString(version);
    if (v.segmentCount() < 2)
        return v;
    if (v.segmentCount() >= 3 && v.microVersion() >= 90)
        return QVersionNumber(v.majorVersion(), v.minorVersion() + 1);
    return QVersionNumber(v.majorVersion(), v.minorVersion());
}

std::optional<Choice> choose(const QString &plasma, const QStringList &ids, QString *error)
{
    const QVersionNumber running = seriesOf(plasma);
    if (running.isNull()) {
        fail(error, QStringLiteral("can't tell which Plasma this is (plasmashell --version)"));
        return std::nullopt;
    }
    const QString shipped = paths::shippedForks();
    QStringList series = shipped.isEmpty() ? QStringList() : QDir(shipped).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::sort(series.begin(), series.end(), [](const QString &a, const QString &b) {
        return QVersionNumber::fromString(b) < QVersionNumber::fromString(a);
    });
    QStringList why;
    for (const QString &name : std::as_const(series)) {
        if (running < QVersionNumber::fromString(name))
            continue;   // (for a newer Plasma)
        Choice choice{name, {}};
        QString problem;
        for (const QString &id : ids) {
            const QString path = QStringLiteral("%1/%2/plasma/applets/%3.so").arg(shipped, name, id);
            if (!QFileInfo::exists(path)) {
                problem = QStringLiteral("no %1").arg(id);
                break;
            }
            // Loaded here -- its Qt, its KDE libraries, its libPlasma
            // version all this system's. (And left loaded: unloading leaves
            // Qt's type registries pointing into code that's gone.)
            QPluginLoader loader(path);
            if (!loader.load()) {
                problem = loader.errorString();
                break;
            }
            choice.plugins.insert(id, path);
        }
        if (problem.isEmpty())
            return choice;
        why << QStringLiteral("the %1 build: %2").arg(name, problem);
    }
    fail(error, why.isEmpty() ? QStringLiteral("XPlasma has no build of Plasma's %1 for Plasma %2").arg(ids.join(QStringLiteral(", ")), plasma)
                              : QStringLiteral("none of XPlasma's builds of Plasma's widgets loads in Plasma %1 (%2)").arg(plasma, why.join(QStringLiteral("; "))));
    return std::nullopt;
}

bool install(Session &session, const Choice &choice, State &state, QString *error)
{
    // In the style plugin's root, which the session's QT_PLUGIN_PATH names
    // ahead of the system's.
    if (!qtstyle::ensurePluginPath(session, error))
        return false;
    remove(state);
    const QString applets = paths::stylePluginRoot(6) + QLatin1String("/plasma/applets/");
    QDir().mkpath(applets);
    QJsonArray installed;
    for (auto it = choice.plugins.cbegin(); it != choice.plugins.cend(); ++it) {
        const QString to = applets + it.key() + QLatin1String(".so");
        // (A new file renamed over the old, never written into: a running
        // shell has the old one mapped.)
        const QString staged = to + QLatin1String(".new");
        QFile::remove(staged);
        if (!QFile::copy(it.value(), staged))
            return fail(error, QStringLiteral("can't install %1").arg(to));
        // (QFile::rename won't replace a file: the old one is unlinked first,
        // which a process that has it mapped keeps.)
        if (!QFile::rename(staged, to) && !(QFile::remove(to) && QFile::rename(staged, to)))
            return fail(error, QStringLiteral("can't install %1").arg(to));
        installed.append(to);
    }
    state.settings.insert(QStringLiteral("forkPlugins"), installed);
    state.settings.insert(QStringLiteral("forkSeries"), choice.series);
    state.settings.insert(QStringLiteral("forkPlasma"), runningPlasma(session));
    state.settings.remove(QStringLiteral("forkLost"));
    return true;
}

bool installed(const State &state)
{
    return !state.settings.value(QStringLiteral("forkPlugins")).toArray().isEmpty();
}

Checked check(Session &session, State &state, QString *what)
{
    if (!installed(state))
        return Checked::Fine;
    const QString plasma = runningPlasma(session);
    if (plasma.isEmpty())
        return Checked::Fine;   // (can't tell: left as it is)
    // (the same series as when chosen: nothing to do -- the usual login)
    if (seriesOf(plasma) == seriesOf(state.settings.value(QStringLiteral("forkPlasma")).toString()))
        return Checked::Fine;
    QStringList ids;
    for (const QJsonValue &v : state.settings.value(QStringLiteral("forkPlugins")).toArray())
        ids << QFileInfo(v.toString()).completeBaseName();
    QString why;
    if (const auto choice = choose(plasma, ids, &why)) {
        if (choice->series == state.settings.value(QStringLiteral("forkSeries")).toString()) {
            state.settings.insert(QStringLiteral("forkPlasma"), plasma);
            return Checked::Fine;
        }
        if (install(session, *choice, state, &why)) {
            if (what)
                *what = QStringLiteral("Plasma %1: XPlasma's %2 build of its widgets installed").arg(plasma, choice->series);
            return Checked::Replaced;
        }
    }
    remove(state);
    state.settings.insert(QStringLiteral("forkLost"), plasma);
    if (what)
        *what = QStringLiteral("Plasma %1: XPlasma's builds of its widgets taken off, Plasma's own back (%2)").arg(plasma, why);
    return Checked::Removed;
}

void remove(State &state)
{
    const QString local = QFileInfo(paths::localShare()).absolutePath();   // ~/.local (its folders, as they empty)
    for (const QJsonValue &v : state.settings.value(QStringLiteral("forkPlugins")).toArray()) {
        QFile::remove(v.toString());
        paths::removeEmptyDirs(QFileInfo(v.toString()).absolutePath(), local);
    }
    state.settings.remove(QStringLiteral("forkPlugins"));
    state.settings.remove(QStringLiteral("forkSeries"));
    state.settings.remove(QStringLiteral("forkPlasma"));
}

} // namespace xpl::forks
