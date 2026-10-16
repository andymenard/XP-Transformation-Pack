// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// One visual style, loaded: its variant tables, the chosen variant's layout
// INI, and its bitmaps (decoded on first use).

#include "dib.h"
#include "ini.h"
#include "pe.h"

#include <QFile>
#include <QHash>
#include <QMutex>
#include <QStringList>

#include <memory>

namespace xpl {

// The style's own variant bookkeeping (see msstyles_parse.variant_table):
// COLORNAMES / SIZENAMES are what a .theme's ColorStyle= / Size= name, and
// FILERESNAMES is the layout INI for each (colour, size), colour-major.
struct VariantTable
{
    QStringList iniNames;       // every TEXTFILE resource, upper-case, sorted
    QStringList colourNames;
    QStringList sizeNames;
    QStringList fileNames;      // upper-case
    QString themesIni;          // THEMES_INI, the display names
};

class Theme
{
public:
    // `variant` is a layout INI resource name ("NORMALBLUE_INI"); empty picks
    // the style's own default, which is FILERESNAMES[0] -- not the
    // alphabetically first NORMAL*_INI.
    static std::shared_ptr<const Theme> load(const QString &path, const QString &variant = QString(),
                                             QString *error = nullptr);
    static std::shared_ptr<const Theme> fromData(const QByteArray &file, const QString &variant = QString(),
                                                 QString *error = nullptr);

    const VariantTable &variants() const { return m_variants; }
    QString variant() const { return m_variant; }
    const Ini &ini() const { return m_ini; }

    std::optional<QString> property(QStringView cls, QStringView part, QStringView state,
                                    QStringView property, QStringView sub = {}) const
    {
        return m_ini.lookup(sub, cls, part, state, property);
    }

    // The bitmap an ImageFile-style property names ("Luna\Button.bmp"),
    // decoded but without colour keying (that is the renderer's business).
    // Resource names are the path upper-cased with every non-alphanumeric
    // character turned into '_' ("LUNA_BUTTON_BMP").
    Dib bitmap(const QString &imageFile) const;
    static QString resourceName(const QString &imageFile);
    QStringList bitmapNames() const;

private:
    Theme() = default;
    static std::shared_ptr<Theme> build(const QByteArray &file, const QString &variant, QString *error, bool borrow);

    VariantTable m_variants;
    QString m_variant;
    Ini m_ini;
    QHash<QString, QByteArray> m_bitmapData;    // resource name -> raw DIB (into m_mapping, when mapped)
    std::unique_ptr<QFile> m_mapping;           // the mapped file m_bitmapData points into, if it is
    mutable QMutex m_cacheLock;
    mutable QHash<QString, Dib> m_bitmaps;
};

} // namespace xpl
