// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Windows setup INFs, as XP's install media has them: sections of lines,
// [Strings] for %KEY% substitution, comma-separated fields with "quoting"
// ("" is a quote inside one), ';' comments. Latin-1 or UTF-16LE.
#include <QByteArray>
#include <QHash>
#include <QString>
#include <QStringList>

namespace xpl {

class Inf
{
public:
    static Inf parse(const QByteArray &bytes);

    // A section's lines, as written (comments and blank lines gone).
    QStringList lines(const QString &section) const { return m_sections.value(section.toLower()); }
    QStringList sectionNames() const { return m_sections.keys(); }
    // A line's fields: split at commas outside quotes, unquoted, %KEY%s
    // expanded from [Strings], trimmed.
    QStringList fields(const QString &line) const;
    QString expand(QString text) const;
    QString string(const QString &key) const { return m_strings.value(key.toLower()); }
    const QHash<QString, QString> &strings() const { return m_strings; }

private:
    QHash<QString, QStringList> m_sections;   // lower-case names
    QHash<QString, QString> m_strings;        // lower-case keys
};

} // namespace xpl
