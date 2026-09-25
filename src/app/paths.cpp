#include "app/paths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace xpl::paths {

QString home()
{
    return QDir::homePath();
}

QString localShare()
{
    return home() + QLatin1String("/.local/share");
}

QString config()
{
    return home() + QLatin1String("/.config");
}

QStringList systemData()
{
    QStringList out;
    const QString dirs = qEnvironmentVariable("XDG_DATA_DIRS");
    const QString own = QDir::cleanPath(localShare());
    for (const QString &dir : (dirs.isEmpty() ? QStringLiteral("/usr/local/share:/usr/share") : dirs).split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString clean = QDir::cleanPath(dir);
        if (clean != own && !out.contains(clean))
            out << clean;
    }
    return out;
}

QString findSystemData(const QString &relative)
{
    for (const QString &dir : systemData())
        if (QFileInfo::exists(dir + QLatin1Char('/') + relative))
            return dir + QLatin1Char('/') + relative;
    return QString();
}

QString data()
{
    return localShare() + QLatin1String("/xplasma");
}

QString assets()
{
    return data() + QLatin1String("/assets");
}

QString themes()
{
    return assets() + QLatin1String("/themes");
}

QString xpFonts()
{
    return localShare() + QLatin1String("/fonts/xp");
}

QString wallpapers()
{
    return assets() + QLatin1String("/wallpapers");
}

QString schemes()
{
    return data() + QLatin1String("/schemes");
}

QString extracted()
{
    return data() + QLatin1String("/extracted");
}

QString statePath()
{
    return data() + QLatin1String("/state.json");
}

QString cache()
{
    return home() + QLatin1String("/.cache/xplasma");
}

QString downloads()
{
    return cache() + QLatin1String("/downloads");
}

QString build()
{
    return cache() + QLatin1String("/build");
}

QString legacyData()
{
    return localShare() + QLatin1String("/xp2plasma");
}

QString legacyCache()
{
    return home() + QLatin1String("/.cache/xp2plasma");
}

// Something this copy of XPLasma ships under <prefix>/lib/xplasma, beside
// bin/ -- in a checkout, build.sh's build/<mode>; empty if it isn't there.
static QString shipped(const QString &relative)
{
    const QDir exe(QCoreApplication::applicationDirPath());
    QStringList candidates{exe.absoluteFilePath(QLatin1String("../lib/xplasma/") + relative)};
    // A checkout: this is build/<mode>-qt6/..., and build.sh installed into
    // build/<mode>.
    for (QDir dir = exe; !dir.isRoot(); dir.cdUp()) {
        const QString name = dir.dirName();
        const int dash = name.lastIndexOf(QLatin1String("-qt"));
        QDir build = dir;
        if (dash > 0 && build.cdUp() && build.dirName() == QLatin1String("build")) {
            candidates << build.filePath(name.left(dash) + QLatin1String("/lib/xplasma/") + relative);
            break;
        }
    }
    for (const QString &c : std::as_const(candidates))
        if (QFileInfo::exists(c))
            return QFileInfo(c).absoluteFilePath();
    return QString();
}

QString shippedStylePlugin(int qtMajor)
{
    return shipped(QStringLiteral("qt%1/styles/libxplasma.so").arg(qtMajor));
}

QString shippedForks()
{
    return shipped(QStringLiteral("forks"));
}

void removeEmptyDirs(const QString &dir, const QString &top)
{
    const QString stop = QDir::cleanPath(top);
    for (QString d = QDir::cleanPath(dir); d.startsWith(stop + QLatin1Char('/')); d = QFileInfo(d).absolutePath())
        if (!QDir().rmdir(d))
            break;
}

QString stylePluginRoot(int qtMajor)
{
    return home() + QStringLiteral("/.local/lib/qt%1/plugins").arg(qtMajor);
}

} // namespace xpl::paths
