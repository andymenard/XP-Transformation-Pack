#include "app/look.h"

#include "app/library.h"

#include <QFileInfo>

namespace xpl {

QString packageId(const QString &name)
{
    QString id;
    bool gap = false;
    for (const QChar c : name.toLower()) {
        if (c.isLetterOrNumber() && c.unicode() < 0x80) {
            if (gap && !id.isEmpty())
                id += QLatin1Char('-');
            id += c;
            gap = false;
        } else {
            gap = true;
        }
    }
    return id.isEmpty() ? QStringLiteral("xp") : id;
}

std::optional<Look> Look::fromStyle(const QString &path, const QString &variant, QString *error)
{
    auto theme = Theme::load(path, variant, error);
    if (!theme)
        return std::nullopt;
    Look look;
    look.stylePath = QFileInfo(path).absoluteFilePath();
    look.variant = theme->variant();
    look.system = ClassicScheme::fromSysMetrics(*theme);
    look.renderer = std::make_shared<const Renderer>(theme);

    // Named as XP's Display Properties would show the choice.
    const StyleInfo info = Library::style(path);
    look.name = info.label.isEmpty() ? QFileInfo(path).completeBaseName() : info.label;
    if (const StyleVariant *v = info.variant(look.variant)) {
        QString colour, size;
        for (const StyleOption &c : info.colours)
            if (c.key == v->colour)
                colour = c.label;
        for (const StyleOption &s : info.sizes)
            if (s.key == v->size && s.key != QLatin1String("NORMAL"))
                size = s.label;
        QStringList qualifiers;
        if (info.colours.size() > 1 && !colour.isEmpty())
            qualifiers << colour;
        if (!size.isEmpty())
            qualifiers << size;
        if (!qualifiers.isEmpty())
            look.name += QStringLiteral(" (%1)").arg(qualifiers.join(QStringLiteral(", ")));
    }
    look.system.name = look.name;
    look.id = packageId(look.name);
    return look;
}

Look Look::fromScheme(const ClassicScheme &scheme)
{
    Look look;
    look.name = QStringLiteral("Windows Classic (%1)").arg(scheme.name);
    look.id = packageId(look.name);
    look.system = scheme;
    return look;
}

} // namespace xpl
