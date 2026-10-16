// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace xpl {
namespace {

QStringList utf16List(const QByteArray &data)
{
    const QString text = QString::fromUtf16(reinterpret_cast<const char16_t *>(data.constData()), int(data.size() / 2));
    QStringList out;
    for (const QString &s : text.split(QChar(0)))
        if (!s.isEmpty())
            out.append(s);
    return out;
}

// XPlasma's frozen copy of a look: active/<sha256 of its bytes>.msstyles.
// Written once under that name, atomically, and only ever deleted after --
// never changed in place -- so it can be mapped: every app on the desktop
// then shares the one copy of its bitmaps the kernel has, instead of each
// reading its own.
bool contentAddressed(const QString &path)
{
    const QFileInfo info(path);
    const QString name = info.completeBaseName();
    if (info.dir().dirName() != QLatin1String("active") || info.suffix().compare(QLatin1String("msstyles"), Qt::CaseInsensitive) != 0
        || name.size() != 64)
        return false;
    for (const QChar c : name)
        if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))))
            return false;
    return true;
}

} // namespace

std::shared_ptr<const Theme> Theme::load(const QString &path, const QString &variant, QString *error)
{
    if (contentAddressed(path)) {
        auto mapped = std::make_unique<QFile>(path);
        if (mapped->open(QIODevice::ReadOnly) && mapped->size() > 0) {
            if (uchar *bytes = mapped->map(0, mapped->size())) {
                auto theme = build(QByteArray::fromRawData(reinterpret_cast<const char *>(bytes), qsizetype(mapped->size())),
                                   variant, error, true);
                if (theme)
                    theme->m_mapping = std::move(mapped);
                return theme;
            }
        }
    }
    QFile file(path);
    // Anything else is read into memory, never mapped: it may be replaced
    // while we run.
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return nullptr;
    }
    return fromData(file.readAll(), variant, error);
}

std::shared_ptr<const Theme> Theme::fromData(const QByteArray &file, const QString &variant, QString *error)
{
    return build(file, variant, error, false);
}

std::shared_ptr<Theme> Theme::build(const QByteArray &file, const QString &variant, QString *error, bool borrow)
{
    const auto pe = PeResources::parse(file, error, borrow);
    if (!pe)
        return nullptr;
    std::shared_ptr<Theme> theme(new Theme);
    QHash<QString, QByteArray> inis;
    for (const Resource &r : pe->all()) {
        const QString type = r.type.toUpper();
        if (type == QLatin1String("TEXTFILE")) {
            inis.insert(r.name.toUpper(), r.data);
        } else if (type == QLatin1String("2")) {
            theme->m_bitmapData.insert(r.name.toUpper(), r.data);
        } else if (type == QLatin1String("COLORNAMES")) {
            theme->m_variants.colourNames = utf16List(r.data);
        } else if (type == QLatin1String("SIZENAMES")) {
            theme->m_variants.sizeNames = utf16List(r.data);
        } else if (type == QLatin1String("FILERESNAMES")) {
            for (const QString &name : utf16List(r.data))
                theme->m_variants.fileNames.append(name.toUpper());
        }
    }
    if (inis.isEmpty()) {
        if (error)
            *error = QStringLiteral("no TEXTFILE resources: not a classic (XP-era) visual style");
        return nullptr;
    }
    theme->m_variants.iniNames = inis.keys();
    theme->m_variants.iniNames.sort();
    if (inis.contains(QStringLiteral("THEMES_INI")))
        theme->m_variants.themesIni = decodeText(inis.value(QStringLiteral("THEMES_INI")));

    QString chosen = variant.toUpper();
    if (chosen.isEmpty()) {
        // XP's default: the first colour at the first size, FILERESNAMES[0];
        // then any NORMAL* variant; then the largest INI (layout dwarfs
        // THEMES_INI). Same order as msstyles_parse.MsStyle.load.
        for (const QString &n : theme->m_variants.fileNames) {
            if (inis.contains(n)) {
                chosen = n;
                break;
            }
        }
        if (chosen.isEmpty()) {
            for (const QString &n : theme->m_variants.iniNames) {
                if (n.startsWith(QLatin1String("NORMAL"))) {
                    chosen = n;
                    break;
                }
            }
        }
        if (chosen.isEmpty()) {
            qsizetype largest = -1;
            for (auto it = inis.cbegin(); it != inis.cend(); ++it) {
                if (it.value().size() > largest) {
                    largest = it.value().size();
                    chosen = it.key();
                }
            }
        }
    }
    if (!inis.contains(chosen)) {
        if (error)
            *error = QStringLiteral("no layout INI named %1").arg(chosen);
        return nullptr;
    }
    theme->m_variant = chosen;
    theme->m_ini = Ini::parse(inis.value(chosen));
    return theme;
}

QString Theme::resourceName(const QString &imageFile)
{
    QString out = imageFile.toUpper();
    for (QChar &c : out) {
        const ushort u = c.unicode();
        if (!((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')))
            c = QLatin1Char('_');
    }
    return out;
}

QStringList Theme::bitmapNames() const
{
    QStringList names = m_bitmapData.keys();
    names.sort();
    return names;
}

Dib Theme::bitmap(const QString &imageFile) const
{
    const QString key = resourceName(imageFile);
    QMutexLocker lock(&m_cacheLock);
    auto cached = m_bitmaps.constFind(key);
    if (cached != m_bitmaps.cend())
        return cached.value();
    const Dib dib = decodeDib(m_bitmapData.value(key));
    m_bitmaps.insert(key, dib);
    return dib;
}

} // namespace xpl
