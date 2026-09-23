#include "themefile.h"

#include <QFile>
#include <QSaveFile>

#include <algorithm>

namespace xpl {

namespace {

// Windows-1252's 0x80-0x9F (the rest of the byte range is Latin-1).
constexpr char16_t kCp1252High[32] = {
    0x20AC, 0xFFFD, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0xFFFD, 0x017D, 0xFFFD,
    0xFFFD, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0xFFFD, 0x017E, 0x0178,
};

QString fromCp1252(const QByteArray &bytes)
{
    QString s;
    s.reserve(bytes.size());
    for (const char c : bytes) {
        const auto b = quint8(c);
        s += (b >= 0x80 && b < 0xA0) ? QChar(kCp1252High[b - 0x80]) : QChar(b);
    }
    return s;
}

// False when `text` has a character Windows-1252 can't hold.
bool toCp1252(const QString &text, QByteArray *out)
{
    out->clear();
    out->reserve(text.size());
    for (const QChar ch : text) {
        const char16_t u = ch.unicode();
        if (u < 0x80 || (u >= 0xA0 && u < 0x100)) {
            out->append(char(u));
            continue;
        }
        int b = -1;
        for (int i = 0; i < 32; ++i)
            if (kCp1252High[i] == u && u != 0xFFFD)
                b = 0x80 + i;
        if (b < 0)
            return false;
        out->append(char(b));
    }
    return true;
}

QString fromUtf16(const QByteArray &bytes, bool bigEndian)
{
    QString s;
    s.reserve(bytes.size() / 2);
    for (int i = 0; i + 1 < bytes.size(); i += 2) {
        const auto a = quint8(bytes[i]), b = quint8(bytes[i + 1]);
        s += QChar(ushort(bigEndian ? (a << 8) | b : (b << 8) | a));
    }
    return s;
}

bool validUtf8(const QByteArray &bytes)
{
    for (int i = 0; i < bytes.size();) {
        const auto c = quint8(bytes[i]);
        const int n = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xe ? 2 : (c >> 3) == 0x1e ? 3 : -1;
        if (n < 0 || i + n >= bytes.size() + (n == 0 ? 1 : 0))
            return false;
        for (int k = 1; k <= n; ++k)
            if ((quint8(bytes[i + k]) >> 6) != 0x2)
                return false;
        i += n + 1;
    }
    return true;
}

} // namespace

QString decodeThemeText(const QByteArray &bytes)
{
    if (bytes.startsWith("\xff\xfe") || bytes.startsWith("\xfe\xff"))
        return fromUtf16(bytes.mid(2), bytes.startsWith("\xfe\xff"));
    if (bytes.size() > 1 && bytes[0] != 0 && bytes[1] == 0)
        return fromUtf16(bytes, false);
    const QByteArray body = bytes.startsWith("\xef\xbb\xbf") ? bytes.mid(3) : bytes;
    return validUtf8(body) ? QString::fromUtf8(body) : fromCp1252(body);
}

std::optional<ThemeFile> ThemeFile::load(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return std::nullopt;
    }
    return fromBytes(file.readAll());
}

ThemeFile ThemeFile::fromBytes(const QByteArray &bytes)
{
    return fromText(decodeThemeText(bytes));
}

ThemeFile ThemeFile::fromText(const QString &text)
{
    ThemeFile t;
    Section *current = nullptr;
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            const QString name = line.mid(1, line.size() - 2).trimmed();
            current = t.find(name);
            if (!current) {
                t.m_sections.append({name, {}});
                current = &t.m_sections.last();
            }
            continue;
        }
        const int eq = int(line.indexOf(QLatin1Char('=')));
        if (!current || eq <= 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        bool replaced = false;
        for (auto &[k, v] : current->values)
            if (k.compare(key, Qt::CaseInsensitive) == 0) {
                v = value;
                replaced = true;
            }
        if (!replaced)
            current->values.append({key, value});
    }
    return t;
}

QByteArray ThemeFile::toBytes() const
{
    QString text;
    for (const Section &s : m_sections) {
        text += QLatin1Char('[') + s.name + QLatin1String("]\r\n");
        for (const auto &[k, v] : s.values)
            text += k + QLatin1Char('=') + v + QLatin1String("\r\n");
        text += QLatin1String("\r\n");
    }
    QByteArray bytes;
    if (toCp1252(text, &bytes))
        return bytes;
    bytes = QByteArray("\xff\xfe", 2);
    for (const QChar ch : text) {
        bytes.append(char(ch.unicode() & 0xff));
        bytes.append(char(ch.unicode() >> 8));
    }
    return bytes;
}

bool ThemeFile::save(const QString &path, QString *error) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(toBytes()) < 0 || !file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

const ThemeFile::Section *ThemeFile::find(QStringView section) const
{
    for (const Section &s : m_sections)
        if (s.name.compare(section, Qt::CaseInsensitive) == 0)
            return &s;
    return nullptr;
}

ThemeFile::Section *ThemeFile::find(QStringView section)
{
    return const_cast<Section *>(std::as_const(*this).find(section));
}

bool ThemeFile::hasSection(QStringView section) const
{
    return find(section);
}

QStringList ThemeFile::sections() const
{
    QStringList names;
    for (const Section &s : m_sections)
        names << s.name;
    return names;
}

QStringList ThemeFile::keys(QStringView section) const
{
    QStringList names;
    if (const Section *s = find(section))
        for (const auto &kv : s->values)
            names << kv.first;
    return names;
}

QString ThemeFile::value(QStringView section, QStringView key) const
{
    if (const Section *s = find(section))
        for (const auto &[k, v] : s->values)
            if (k.compare(key, Qt::CaseInsensitive) == 0)
                return v;
    return QString();
}

bool ThemeFile::contains(QStringView section, QStringView key) const
{
    if (const Section *s = find(section))
        for (const auto &kv : s->values)
            if (kv.first.compare(key, Qt::CaseInsensitive) == 0)
                return true;
    return false;
}

void ThemeFile::setValue(QStringView section, QStringView key, const QString &value)
{
    Section *s = find(section);
    if (!s) {
        m_sections.append({section.toString(), {}});
        s = &m_sections.last();
    }
    for (auto &[k, v] : s->values)
        if (k.compare(key, Qt::CaseInsensitive) == 0) {
            v = value;
            return;
        }
    s->values.append({key.toString(), value});
}

void ThemeFile::remove(QStringView section, QStringView key)
{
    if (Section *s = find(section))
        s->values.erase(std::remove_if(s->values.begin(), s->values.end(),
                                       [&](const std::pair<QString, QString> &kv) {
                                           return kv.first.compare(key, Qt::CaseInsensitive) == 0;
                                       }),
                        s->values.end());
}

void ThemeFile::removeSection(QStringView section)
{
    m_sections.erase(std::remove_if(m_sections.begin(), m_sections.end(),
                                    [&](const Section &s) { return s.name.compare(section, Qt::CaseInsensitive) == 0; }),
                     m_sections.end());
}

} // namespace xpl
