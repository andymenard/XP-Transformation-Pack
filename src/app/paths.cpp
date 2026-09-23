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

QString shippedStylePlugin(int qtMajor)
{
    const QString relative = QStringLiteral("qt%1/styles/libxplasma.so").arg(qtMajor);
    const QDir exe(QCoreApplication::applicationDirPath());
    QStringList candidates{exe.absoluteFilePath(QLatin1String("../lib/xplasma/") + relative)};
    // A checkout: build/<mode>-qt6/src/app/xplasma, plugins in app/plugins.
    for (QDir dir = exe; !dir.isRoot(); dir.cdUp())
        if (QFileInfo::exists(dir.filePath(QStringLiteral("src/app/start-menu")))) {
            candidates << dir.filePath(QLatin1String("app/plugins/") + relative);
            break;
        }
    for (const QString &c : std::as_const(candidates))
        if (QFileInfo::exists(c))
            return QFileInfo(c).absoluteFilePath();
    return QString();
}

QString stylePluginRoot(int qtMajor)
{
    return home() + QStringLiteral("/.local/lib/qt%1/plugins").arg(qtMajor);
}

} // namespace xpl::paths
