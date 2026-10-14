// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// A Windows .theme file: an INI document naming a visual style (or a Classic
// colour scheme), a wallpaper, fonts and metrics.
//
// XP writes them in the ANSI code page with CRLF; fan bundles and later
// Windows use UTF-16 with a BOM, sometimes UTF-8. Sections and keys match
// case-insensitively, keep their spelling and order, and a duplicate key's
// last value wins -- as GetPrivateProfileString sees them. Comments are not
// kept.
#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace xpl {

class ThemeFile
{
public:
    static std::optional<ThemeFile> load(const QString &path, QString *error = nullptr);
    static ThemeFile fromBytes(const QByteArray &bytes);
    static ThemeFile fromText(const QString &text);   // already decoded (a style's THEMES_INI)

    // Windows-1252 with CRLF, as XP writes; UTF-16LE with a BOM when a value
    // needs characters outside it.
    QByteArray toBytes() const;
    bool save(const QString &path, QString *error = nullptr) const;

    bool hasSection(QStringView section) const;
    QStringList sections() const;
    QStringList keys(QStringView section) const;
    // Empty when absent (use contains() to tell an empty value apart).
    QString value(QStringView section, QStringView key) const;
    bool contains(QStringView section, QStringView key) const;

    void setValue(QStringView section, QStringView key, const QString &value);
    void remove(QStringView section, QStringView key);
    void removeSection(QStringView section);

private:
    struct Section
    {
        QString name;
        QList<std::pair<QString, QString>> values;
    };
    const Section *find(QStringView section) const;
    Section *find(QStringView section);

    QList<Section> m_sections;
};

// Text in a .theme's encoding, detected: a UTF-16 BOM (or the zero bytes of
// UTF-16LE without one), UTF-8, else Windows-1252.
QString decodeThemeText(const QByteArray &bytes);

} // namespace xpl
