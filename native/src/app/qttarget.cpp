// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/qttarget.h"

#include "app/flatpak.h"
#include "app/paths.h"
#include "core/themefile.h"

#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <cstdio>

namespace xpl::qtstyle {

namespace {

const int kMajors[] = {6, 5};

QString activeDir()
{
    return paths::data() + QLatin1String("/active");
}

QString configPath()
{
    return paths::config() + QLatin1String("/xplasmarc");
}

QString envScript()
{
    return paths::config() + QLatin1String("/plasma-workspace/env/xplasma-style.sh");
}

QString installedPlugin(int major)
{
    return paths::stylePluginRoot(major) + QLatin1String("/styles/libxplasma.so");
}

bool writeAtomically(const QString &path, const QByteArray &bytes, QString *error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) < 0 || !f.commit()) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

bool sameContents(const QString &a, const QString &b)
{
    QFile fa(a), fb(b);
    return fa.size() == fb.size() && fa.open(QIODevice::ReadOnly) && fb.open(QIODevice::ReadOnly) && fa.readAll() == fb.readAll();
}

// Every running Qt app -- plasmashell, KWin, the dialogs on screen -- has the
// installed plugin mapped. Writing into it rewrites that inode under them
// (the session takes a SIGBUS); a new file renamed over it gets its own, and
// whoever mapped the old one keeps it.
bool installPlugin(const QString &from, const QString &to, QString *error)
{
    if (QFileInfo::exists(to) && sameContents(from, to))
        return true;
    QDir().mkpath(QFileInfo(to).absolutePath());
    const QString staged = to + QLatin1String(".new");
    QFile::remove(staged);
    if (!QFile::copy(from, staged) || std::rename(QFile::encodeName(staged).constData(), QFile::encodeName(to).constData()) != 0) {
        if (error)
            *error = QStringLiteral("can't install %1").arg(to);
        return false;
    }
    return true;
}

QStringList sessionPluginPath(Session &session)
{
    const QString env = QString::fromUtf8(session.run({QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("show-environment")}).out);
    for (const QString &line : env.split(QLatin1Char('\n')))
        if (line.startsWith(QLatin1String("QT_PLUGIN_PATH=")))
            return line.mid(15).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    return {};
}

void setSessionPluginPath(Session &session, const QStringList &path)
{
    // (and this program's own: a shell it starts itself, in a session
    // without systemd, has it from here)
    if (path.isEmpty()) {
        session.run({QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("unset-environment"), QStringLiteral("QT_PLUGIN_PATH")});
        qunsetenv("QT_PLUGIN_PATH");
        return;
    }
    const QString value = QStringLiteral("QT_PLUGIN_PATH=") + path.join(QLatin1Char(':'));
    qputenv("QT_PLUGIN_PATH", path.join(QLatin1Char(':')).toLocal8Bit());
    session.run({QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("set-environment"), value});
    session.run({QStringLiteral("dbus-update-activation-environment"), QStringLiteral("--systemd"), value});
}

void setWidgetStyle(Session &session, const QString &style)
{
    session.writeConfig(QStringLiteral("kdeglobals"), {QStringLiteral("KDE")}, QStringLiteral("widgetStyle"), style);
    // KGlobalSettings::StyleChanged, which running KDE apps follow.
    session.signal(QStringLiteral("/KGlobalSettings"), QStringLiteral("org.kde.KGlobalSettings"), QStringLiteral("notifyChange"),
                   {2, 0});
}

} // namespace

bool ensurePluginPath(Session &session, QString *error)
{
    // XPlasma's plugin roots: Qt 6's always (the style's, and Plasma's
    // widgets as XPlasma builds them), Qt 5's where its style is installed.
    QStringList roots{paths::stylePluginRoot(6)};
    if (QFileInfo::exists(installedPlugin(5)))
        roots << paths::stylePluginRoot(5);
    // And, before Plasma's shell starts, XPlasma's builds of its widgets
    // checked against the Plasma there is (an upgrade to another series
    // would leave the panel without them).
    QByteArray check;
    if (const QString cli = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("xplasma")); QFileInfo(cli).isExecutable()) {
        QByteArray quoted = cli.toUtf8();
        quoted.replace('\'', "'\\''");
        check = "[ -x '" + quoted + "' ] && '" + quoted + "' check-forks >/dev/null 2>&1\n";
    }
    if (!writeAtomically(envScript(),
                         QByteArray("# Written by XPlasma: lets Qt find XPlasma's user-local plugins first.\n"
                                    "export QT_PLUGIN_PATH=\"")
                             + roots.join(QLatin1Char(':')).toUtf8() + "${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}\"\n" + check,
                         error))
        return false;
    QStringList path = roots;
    for (const QString &p : sessionPluginPath(session))
        if (!path.contains(p))
            path << p;
    setSessionPluginPath(session, path);
    return true;
}

QString freeze(const Look &look, QString *error)
{
    QByteArray bytes;
    QString suffix;
    if (look.classic()) {
        ThemeFile file;
        file.setValue(u"Theme", u"DisplayName", look.system.name);
        look.system.writeTo(file);
        bytes = file.toBytes();
        suffix = QStringLiteral(".theme");
    } else {
        QFile f(look.stylePath);
        if (!f.open(QIODevice::ReadOnly)) {
            if (error)
                *error = QStringLiteral("%1: %2").arg(look.stylePath, f.errorString());
            return QString();
        }
        bytes = f.readAll();
        suffix = QStringLiteral(".msstyles");
    }
    const QString path = activeDir() + QLatin1Char('/')
                         + QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) + suffix;
    if (!QFileInfo::exists(path) && !writeAtomically(path, bytes, error))
        return QString();
    return path;
}

bool apply(Session &session, const Look &look, State &state, QStringList *log, QString *error)
{
    // The plugins, and the session told where they are (this login's
    // environment, which apps started from now on inherit; and every later
    // login's, through plasma-workspace's env scripts).
    QStringList roots;
    for (const int major : kMajors) {
        const QString shipped = paths::shippedStylePlugin(major);
        if (shipped.isEmpty())
            continue;
        if (!installPlugin(shipped, installedPlugin(major), error))
            return false;
        roots << paths::stylePluginRoot(major);
        *log << QStringLiteral("Qt %1 style plugin installed").arg(major);
    }
    if (roots.isEmpty()) {
        if (error)
            *error = QStringLiteral("no XPlasma style plugin to install (a checkout builds them with ./build.sh)");
        return false;
    }
    if (!ensurePluginPath(session, error))
        return false;

    // The look, frozen, named by xplasmarc.
    const QString frozen = freeze(look, error);
    if (frozen.isEmpty())
        return false;
    const QString source = look.classic() ? QStringLiteral("classic:") + look.system.name : look.stylePath;
    const QByteArray rc = "[Theme]\nPath=" + frozen.toUtf8() + "\nVariant=" + (look.classic() ? QByteArray() : look.variant.toUtf8())
                          + "\nSource=" + source.toUtf8() + "\n"
                          // (XP's text: GDI never kerned. See xptext.h.)
                          + "\n[Text]\nKerning=false\n";
    if (!writeAtomically(configPath(), rc, error))
        return false;
    // (And beside the look, for Flatpak apps: their sandboxes see that folder
    // live, where a single file shared with them would stay the old one.)
    if (!writeAtomically(activeDir() + QLatin1String("/xplasmarc"), rc, error))
        return false;
    // (Apps that still have an older one open read it into memory already.)
    for (const QFileInfo &f : QDir(activeDir()).entryInfoList(QDir::Files))
        if (f.absoluteFilePath() != frozen && f.fileName() != QLatin1String("xplasmarc"))
            QFile::remove(f.absoluteFilePath());

    // The widget style, remembering what it was.
    const QString current = session.readConfig(QStringLiteral("kdeglobals"), {QStringLiteral("KDE")}, QStringLiteral("widgetStyle"));
    if (!state.settings.contains(QStringLiteral("previousWidgetStyle")) && current.compare(QLatin1String(kStyleKey), Qt::CaseInsensitive) != 0)
        state.settings.insert(QStringLiteral("previousWidgetStyle"), current.isEmpty() ? QStringLiteral("Breeze") : current);
    setWidgetStyle(session, QLatin1String(kStyleKey));
    *log << QStringLiteral("widget style: XPlasma (%1)").arg(look.name);
    // Flatpak's Qt apps too (a failure there isn't the look's).
    QString why;
    if (const QString plugin = paths::shippedStylePlugin(6); !plugin.isEmpty() && !flatpak::install(session, plugin, log, &why))
        *log << QStringLiteral("Flatpak apps: not styled (%1)").arg(why);
    return true;
}

// The runtime branches the extension is there for, of every arch.
static QStringList flatpakBranches()
{
    QStringList branches;
    const QDir root(flatpak::extensionRoot());
    for (const QString &arch : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        branches << QDir(root.filePath(arch)).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    branches.sort();
    return branches;
}

void refreshFlatpak(Session &session, QString *what)
{
    // (The plugin as shipped: the host's is only replaced at an apply.)
    const QString plugin = paths::shippedStylePlugin(6);
    if (!QFileInfo::exists(configPath()) || plugin.isEmpty())
        return;
    const QStringList before = flatpakBranches();
    QStringList log;
    QString why;
    if (!flatpak::install(session, plugin, &log, &why)) {
        if (what)
            *what = QStringLiteral("Flatpak apps: not styled (%1)").arg(why);
        return;
    }
    if (what && flatpakBranches() != before)
        *what = log.join(QLatin1Char('\n'));
}

void revert(Session &session, State &state, QStringList *log)
{
    // xp2plasma's Python app kept the previous style in a file.
    QString previous = state.settings.value(QStringLiteral("previousWidgetStyle")).toString();
    if (previous.isEmpty())
        if (QFile f(paths::data() + QLatin1String("/widgetstyle-previous.txt")); f.open(QIODevice::ReadOnly))
            previous = QString::fromUtf8(f.readAll()).trimmed();
    if (previous.isEmpty())
        previous = QStringLiteral("Breeze");
    setWidgetStyle(session, previous);
    state.settings.remove(QStringLiteral("previousWidgetStyle"));
    for (const QString &p : QStringList{envScript(), configPath(), paths::data() + QLatin1String("/widgetstyle-previous.txt")})
        QFile::remove(p);
    const QString local = QFileInfo(paths::localShare()).absolutePath();   // ~/.local (its folders, as they empty)
    for (const int major : kMajors) {
        QFile::remove(installedPlugin(major));
        paths::removeEmptyDirs(QFileInfo(installedPlugin(major)).absolutePath(), local);
    }
    flatpak::remove(log);
    QDir(activeDir()).removeRecursively();
    // Only our roots come out of the path: the rest is someone else's.
    QStringList path = sessionPluginPath(session);
    for (const int major : kMajors)
        path.removeAll(paths::stylePluginRoot(major));
    setSessionPluginPath(session, path);
    *log << QStringLiteral("widget style: back to %1").arg(previous);
}

} // namespace xpl::qtstyle
