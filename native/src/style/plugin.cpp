// SPDX-License-Identifier: GPL-3.0-or-later
// The Qt style plugin entry point: Qt's loader reads xplasma.json's keys and
// calls create() for "XPlasma" (keys match case-insensitively).
#include <QStylePlugin>

#include "xplasmastyle.h"

class XPlasmaStylePlugin : public QStylePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QStyleFactoryInterface_iid FILE "xplasma.json")

public:
    QStyle *create(const QString &key) override
    {
        if (key.compare(QLatin1String("XPlasma"), Qt::CaseInsensitive) == 0)
            return new XPlasmaStyle;
        return nullptr;
    }
};

#include "plugin.moc"
