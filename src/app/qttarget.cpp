#include "app/qttarget.h"

#include "app/paths.h"
#include "core/themefile.h"

#include <QCryptographicHash>
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
    if (path.isEmpty()) {
        session.run({QStringLiteral("systemctl"), QStringLiteral("--user"), QStringLiteral("unset-environment"), QStringLiteral("QT_PLUGIN_PATH")});
        return;
    }
    const QString value = QStringLiteral("QT_PLUGIN_PATH=") + path.join(QLatin1Char(':'));
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
            *error = QStringLiteral("no XPLasma style plugin to install (a checkout builds them with ./build.sh)");
        return false;
    }
    if (!writeAtomically(envScript(),
                         QByteArray("# Written by XPLasma: lets Qt find the user-local XPLasma style plugins.\n"
                                    "export QT_PLUGIN_PATH=\"")
                             + roots.join(QLatin1Char(':')).toUtf8() + "${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}\"\n",
                         error))
        return false;
    QStringList path = roots;
    for (const QString &p : sessionPluginPath(session))
        if (!path.contains(p))
            path << p;
    setSessionPluginPath(session, path);

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
    // (Apps that still have an older one open read it into memory already.)
    for (const QFileInfo &f : QDir(activeDir()).entryInfoList(QDir::Files))
        if (f.absoluteFilePath() != frozen)
            QFile::remove(f.absoluteFilePath());

    // The widget style, remembering what it was.
    const QString current = session.readConfig(QStringLiteral("kdeglobals"), {QStringLiteral("KDE")}, QStringLiteral("widgetStyle"));
    if (!state.settings.contains(QStringLiteral("previousWidgetStyle")) && current.compare(QLatin1String(kStyleKey), Qt::CaseInsensitive) != 0)
        state.settings.insert(QStringLiteral("previousWidgetStyle"), current.isEmpty() ? QStringLiteral("Breeze") : current);
    setWidgetStyle(session, QLatin1String(kStyleKey));
    *log << QStringLiteral("widget style: XPLasma (%1)").arg(look.name);
    return true;
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
    for (const int major : kMajors)
        QFile::remove(installedPlugin(major));
    QDir(activeDir()).removeRecursively();
    // Only our roots come out of the path: the rest is someone else's.
    QStringList path = sessionPluginPath(session);
    for (const int major : kMajors)
        path.removeAll(paths::stylePluginRoot(major));
    setSessionPluginPath(session, path);
    *log << QStringLiteral("widget style: back to %1").arg(previous);
}

} // namespace xpl::qtstyle
