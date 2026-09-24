#include "app/inf.h"

#include <QRegularExpression>

namespace xpl {

Inf Inf::parse(const QByteArray &bytes)
{
    Inf inf;
    const bool wide = bytes.size() > 1 && (bytes.startsWith("\xff\xfe") || (bytes.at(1) == 0 && bytes.at(0) != 0));
    QString text = wide ? QString::fromUtf16(reinterpret_cast<const char16_t *>(bytes.constData()), bytes.size() / 2)
                        : QString::fromLatin1(bytes);
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    QString section;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')))
            continue;
        if (line.startsWith(QLatin1Char('['))) {
            section = line.mid(1, line.indexOf(QLatin1Char(']')) - 1).trimmed().toLower();
            continue;
        }
        inf.m_sections[section].append(line);
    }
    for (const QString &line : inf.m_sections.value(QStringLiteral("strings"))) {
        const int eq = int(line.indexOf(QLatin1Char('=')));
        if (eq > 0)
            inf.m_strings.insert(line.left(eq).trimmed().toLower(), line.mid(eq + 1).section(QLatin1Char(';'), 0, 0).trimmed().remove(QLatin1Char('"')));
    }
    return inf;
}

QString Inf::expand(QString text) const
{
    static const QRegularExpression key(QStringLiteral("%([^%]+)%"));
    qsizetype from = 0;
    for (auto m = key.match(text, from); m.hasMatch(); m = key.match(text, from)) {
        const QString k = m.captured(1).toLower();
        // (%10%, %25%: directory ids, kept for the caller.)
        const QString value = m_strings.contains(k) ? m_strings.value(k) : m.captured(0);
        text.replace(m.capturedStart(), m.capturedLength(), value);
        from = m.capturedStart() + value.size();
    }
    return text.trimmed();
}

QStringList Inf::fields(const QString &line) const
{
    QStringList out;
    QString field;
    bool quoted = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (c == QLatin1Char('"')) {
            if (quoted && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                field += c;   // ("" inside quotes)
                ++i;
            } else {
                quoted = !quoted;
            }
        } else if (c == QLatin1Char(';') && !quoted) {
            break;
        } else if (c == QLatin1Char(',') && !quoted) {
            out.append(expand(field));
            field.clear();
        } else {
            field += c;
        }
    }
    out.append(expand(field));
    return out;
}

} // namespace xpl
