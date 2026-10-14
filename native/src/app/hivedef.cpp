// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hivedef.h"

#include "core/themefile.h"

#include <QHash>
#include <QRegularExpression>

namespace xpl::hivedef {

QList<ClassicScheme> schemes(const QByteArray &inf)
{
    // INF lines, with '\' continuations joined.
    const QString text = decodeThemeText(inf);
    QStringList lines;
    QString pending;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.endsWith(QLatin1Char('\\'))) {
            pending += line.chopped(1);
            continue;
        }
        lines << pending + line;
        pending.clear();
    }

    QHash<QString, QString> strings;
    bool inStrings = false;
    for (const QString &line : std::as_const(lines)) {
        if (line.startsWith(QLatin1Char('['))) {
            inStrings = line.compare(QLatin1String("[Strings]"), Qt::CaseInsensitive) == 0;
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (inStrings && eq > 0) {
            QString value = line.mid(eq + 1).trimmed();
            if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
                value = value.mid(1, value.size() - 2);
            strings.insert(line.left(eq).trimmed().toUpper(), value);
        }
    }

    // HKCU,"Control Panel\Appearance\Schemes","%NAME%",0x00030001,02,00,...
    static const QRegularExpression entry(
        QString::fromLatin1(R"re(^HKCU,"Control Panel\\Appearance\\Schemes","([^"]+)",0x0*30001,(.*)$)re"),
        QRegularExpression::CaseInsensitiveOption);
    QList<ClassicScheme> out;
    for (const QString &line : std::as_const(lines)) {
        const auto m = entry.match(line);
        if (!m.hasMatch())
            continue;
        QByteArray blob;
        bool ok = true;
        for (const QString &hex : m.captured(2).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            const uint b = hex.trimmed().toUInt(&ok, 16);
            if (!ok || b > 255)
                break;
            blob.append(char(b));
        }
        auto scheme = ok ? ClassicScheme::fromSchemeData(blob) : std::nullopt;
        if (!scheme)
            continue;
        QString name = m.captured(1);
        if (name.startsWith(QLatin1Char('%')) && name.endsWith(QLatin1Char('%')))
            name = strings.value(name.mid(1, name.size() - 2).toUpper(), name);
        scheme->name = name;
        out << *scheme;
    }
    return out;
}

QByteArray themeBytes(const ClassicScheme &scheme)
{
    ThemeFile file;
    file.setValue(u"Theme", u"DisplayName", scheme.name);
    scheme.writeTo(file);
    return file.toBytes();
}

} // namespace xpl::hivedef
