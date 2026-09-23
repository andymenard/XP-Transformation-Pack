#include "app/paths.h"

#include <QDir>

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

} // namespace xpl::paths
