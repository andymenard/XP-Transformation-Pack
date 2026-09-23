#include "pe.h"

#include <QSet>

namespace xpl {
namespace {

// Little-endian reads that fail (return false) instead of reading past the end.
struct Reader
{
    const QByteArray &data;

    bool has(qint64 offset, qint64 size) const
    {
        return offset >= 0 && size >= 0 && offset <= data.size() && size <= data.size() - offset;
    }
    bool u16(qint64 offset, quint32 &out) const
    {
        if (!has(offset, 2))
            return false;
        const auto *p = reinterpret_cast<const uchar *>(data.constData() + offset);
        out = quint32(p[0]) | quint32(p[1]) << 8;
        return true;
    }
    bool u32(qint64 offset, quint32 &out) const
    {
        if (!has(offset, 4))
            return false;
        const auto *p = reinterpret_cast<const uchar *>(data.constData() + offset);
        out = quint32(p[0]) | quint32(p[1]) << 8 | quint32(p[2]) << 16 | quint32(p[3]) << 24;
        return true;
    }
};

struct Section
{
    quint32 va, vsize, rawOffset, rawSize;
};

class Parser
{
public:
    explicit Parser(const QByteArray &file) : m_r{file} {}

    std::optional<PeResources> run(QString *error, QList<Resource> &out);

private:
    bool fail(const char *why)
    {
        m_error = QString::fromLatin1(why);
        return false;
    }
    // File offset for an RVA, or -1. Mirrors pefile's section mapping: the
    // section whose [va, va + max(vsize, rawSize)) contains the RVA.
    qint64 offsetOf(quint32 rva, quint32 size) const;
    bool name(quint32 field, QString &out) const;
    bool directory(quint32 offset, int depth, const QString &type, const QString &name, QList<Resource> &out);

    Reader m_r;
    QList<Section> m_sections;
    qint64 m_rsrc = 0;          // file offset of the resource directory root
    QSet<quint32> m_seen;       // directory offsets already walked (cycle guard)
    QString m_error;
};

qint64 Parser::offsetOf(quint32 rva, quint32 size) const
{
    for (const Section &s : m_sections) {
        const quint64 span = qMax(s.vsize, s.rawSize);
        if (rva >= s.va && quint64(rva) < quint64(s.va) + span) {
            const qint64 offset = qint64(s.rawOffset) + (rva - s.va);
            return m_r.has(offset, size) ? offset : -1;
        }
    }
    return -1;
}

bool Parser::name(quint32 field, QString &out) const
{
    if (!(field & 0x80000000u)) {
        out = QString::number(field & 0xffff);
        return true;
    }
    const qint64 at = m_rsrc + (field & 0x7fffffffu);
    quint32 length = 0;
    if (!m_r.u16(at, length) || !m_r.has(at + 2, qint64(length) * 2))
        return false;
    out = QString::fromUtf16(reinterpret_cast<const char16_t *>(m_r.data.constData() + at + 2), int(length));
    return true;
}

bool Parser::directory(quint32 offset, int depth, const QString &type, const QString &resName,
                       QList<Resource> &out)
{
    if (depth > 2 || m_seen.contains(offset))
        return fail("resource directory is cyclic or too deep");
    m_seen.insert(offset);
    const qint64 at = m_rsrc + offset;
    quint32 named = 0, ids = 0;
    if (!m_r.u16(at + 12, named) || !m_r.u16(at + 14, ids))
        return fail("truncated resource directory");
    const quint32 count = named + ids;
    if (!m_r.has(at + 16, qint64(count) * 8))
        return fail("truncated resource directory entries");
    for (quint32 i = 0; i < count; ++i) {
        quint32 nameField = 0, target = 0;
        m_r.u32(at + 16 + i * 8, nameField);
        m_r.u32(at + 16 + i * 8 + 4, target);
        QString entry;
        if (!name(nameField, entry))
            return fail("bad resource name");
        const bool isDir = target & 0x80000000u;
        const quint32 targetOffset = target & 0x7fffffffu;
        if (depth == 0) {
            if (isDir && !directory(targetOffset, 1, entry, QString(), out))
                return false;
        } else if (depth == 1) {
            if (isDir && !directory(targetOffset, 2, type, entry, out))
                return false;
        } else {
            // Language level: the first entry is the one we want.
            if (isDir)
                return fail("resource language entry is a directory");
            quint32 rva = 0, size = 0;
            if (!m_r.u32(m_rsrc + targetOffset, rva) || !m_r.u32(m_rsrc + targetOffset + 4, size))
                return fail("truncated resource data entry");
            const qint64 dataAt = offsetOf(rva, size);
            if (dataAt < 0)
                return fail("resource data outside the file");
            out.append({type, resName, m_r.data.mid(dataAt, size)});
            return true;
        }
    }
    return true;
}

std::optional<PeResources> Parser::run(QString *error, QList<Resource> &out)
{
    auto bail = [&](const char *why) -> std::optional<PeResources> {
        if (error)
            *error = m_error.isEmpty() ? QString::fromLatin1(why) : m_error;
        return std::nullopt;
    };
    const QByteArray &d = m_r.data;
    if (d.size() < 64 || d[0] != 'M' || d[1] != 'Z')
        return bail("not a PE file (no MZ header)");
    quint32 pe = 0;
    m_r.u32(0x3c, pe);
    if (!m_r.has(pe, 24) || d.mid(pe, 4) != QByteArray("PE\0\0", 4))
        return bail("not a PE file (no PE signature)");
    quint32 sections = 0, optionalSize = 0, magic = 0;
    m_r.u16(pe + 6, sections);
    m_r.u16(pe + 20, optionalSize);
    const qint64 optional = pe + 24;
    if (!m_r.u16(optional, magic) || (magic != 0x10b && magic != 0x20b))
        return bail("unknown PE optional header");
    const qint64 dirs = optional + (magic == 0x10b ? 96 : 112);
    quint32 dirCount = 0, rsrcRva = 0, rsrcSize = 0;
    m_r.u32(optional + (magic == 0x10b ? 92 : 108), dirCount);
    if (dirCount < 3 || !m_r.u32(dirs + 2 * 8, rsrcRva) || !m_r.u32(dirs + 2 * 8 + 4, rsrcSize) || rsrcRva == 0)
        return bail("no resource directory");
    const qint64 table = optional + optionalSize;
    if (sections > 96 || !m_r.has(table, qint64(sections) * 40))
        return bail("truncated section table");
    for (quint32 i = 0; i < sections; ++i) {
        Section s{};
        const qint64 at = table + i * 40;
        m_r.u32(at + 8, s.vsize);
        m_r.u32(at + 12, s.va);
        m_r.u32(at + 16, s.rawSize);
        m_r.u32(at + 20, s.rawOffset);
        m_sections.append(s);
    }
    m_rsrc = offsetOf(rsrcRva, 16);
    if (m_rsrc < 0)
        return bail("resource directory outside the file");
    if (!directory(0, 0, QString(), QString(), out))
        return bail("malformed resources");
    PeResources result;
    return result;
}

} // namespace

std::optional<PeResources> PeResources::parse(const QByteArray &file, QString *error)
{
    QList<Resource> resources;
    Parser parser(file);
    auto result = parser.run(error, resources);
    if (result)
        result->m_resources = std::move(resources);
    return result;
}

const Resource *PeResources::find(QStringView type, QStringView name) const
{
    for (const Resource &r : m_resources) {
        if (r.type.compare(type, Qt::CaseInsensitive) == 0 && r.name.compare(name, Qt::CaseInsensitive) == 0)
            return &r;
    }
    return nullptr;
}

QList<const Resource *> PeResources::ofType(QStringView type) const
{
    QList<const Resource *> out;
    for (const Resource &r : m_resources) {
        if (r.type.compare(type, Qt::CaseInsensitive) == 0)
            out.append(&r);
    }
    return out;
}

} // namespace xpl
