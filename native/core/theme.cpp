#include "theme.h"

#include <QFile>

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

} // namespace

std::shared_ptr<const Theme> Theme::load(const QString &path, const QString &variant, QString *error)
{
    QFile file(path);
    // Read into memory, never map: the file may be replaced while we run.
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return nullptr;
    }
    return fromData(file.readAll(), variant, error);
}

std::shared_ptr<const Theme> Theme::fromData(const QByteArray &file, const QString &variant, QString *error)
{
    const auto pe = PeResources::parse(file, error);
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
