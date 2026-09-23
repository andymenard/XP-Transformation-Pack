// The Qt style plugin entry point: Qt's loader reads xplasma.json's keys and
// calls create() for "XPLasma" (keys match case-insensitively).
#include <QStylePlugin>

#include "xplasmastyle.h"

class XPLasmaStylePlugin : public QStylePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QStyleFactoryInterface_iid FILE "xplasma.json")

public:
    QStyle *create(const QString &key) override
    {
        if (key.compare(QLatin1String("XPLasma"), Qt::CaseInsensitive) == 0)
            return new XPLasmaStyle;
        return nullptr;
    }
};

#include "plugin.moc"
