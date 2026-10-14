// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/archive.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QtEndian>

#include <archive.h>
#include <archive_entry.h>

namespace xpl::archive {

namespace {

QString normalised(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (path.startsWith(QLatin1String("./")))
        path.remove(0, 2);
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    return path;
}

// A member path is safe to extract when it stays inside the destination.
bool safe(const QString &path)
{
    if (path.isEmpty())
        return false;
    for (const QString &part : path.split(QLatin1Char('/')))
        if (part == QLatin1String(".."))
            return false;
    return true;
}

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// --- ISO 9660 ---------------------------------------------------------------------

constexpr qint64 kSector = 2048;

struct IsoFile
{
    Entry entry;
    QList<std::pair<qint64, qint64>> extents;   // (byte offset, length); more than one for multi-extent files
};

class Iso
{
public:
    static bool detect(QFile &file)
    {
        char magic[5];
        return file.seek(16 * kSector + 1) && file.read(magic, 5) == 5 && QByteArray(magic, 5) == "CD001";
    }

    bool open(QFile &file, QString *error)
    {
        m_file = &file;
        m_size = file.size();
        // The primary volume descriptor, and a Joliet supplementary one if
        // present (UCS-2 names, preferred).
        QByteArray root;
        for (qint64 sector = 16; sector < 16 + 32; ++sector) {
            const QByteArray vd = readAt(sector * kSector, kSector);
            if (vd.size() < kSector || vd.mid(1, 5) != "CD001")
                break;
            const auto type = quint8(vd[0]);
            if (type == 255)
                break;
            if (type == 1 && root.isEmpty())
                root = vd.mid(156, 34);
            if (type == 2 && vd[88] == 0x25 && vd[89] == 0x2f && (vd[90] == 0x40 || vd[90] == 0x43 || vd[90] == 0x45)) {
                root = vd.mid(156, 34);
                m_joliet = true;
            }
        }
        if (root.size() < 34)
            return fail(error, QStringLiteral("no ISO 9660 volume descriptor"));
        walk(root, QString(), 0);
        return true;
    }

    const QList<IsoFile> &files() const { return m_files; }

    QByteArray contents(const IsoFile &f) const
    {
        QByteArray bytes;
        for (const auto &[offset, length] : f.extents)
            bytes += readAt(offset, length);
        return bytes;
    }

private:
    QByteArray readAt(qint64 offset, qint64 length) const
    {
        if (offset < 0 || length < 0 || offset > m_size || length > m_size - offset || !m_file->seek(offset))
            return QByteArray();
        return m_file->read(length);
    }

    QString name(const QByteArray &raw) const
    {
        QString n;
        if (m_joliet) {
            for (qsizetype i = 0; i + 1 < raw.size(); i += 2)
                n += QChar(ushort((quint8(raw[i]) << 8) | quint8(raw[i + 1])));
        } else {
            n = QString::fromLatin1(raw);
        }
        if (const qsizetype semi = n.indexOf(QLatin1Char(';')); semi >= 0)
            n.truncate(semi);        // the ";1" version
        if (n.endsWith(QLatin1Char('.')))
            n.chop(1);               // "README." for a name without an extension
        return n;
    }

    void walk(const QByteArray &record, const QString &prefix, int depth)
    {
        const qint64 lba = qFromLittleEndian<quint32>(record.constData() + 2);
        const qint64 length = qFromLittleEndian<quint32>(record.constData() + 10);
        if (depth > 64 || m_visited.contains(lba))
            return;   // loops and absurd nesting: a crafted image
        m_visited.insert(lba);
        const QByteArray dir = readAt(lba * kSector, length);
        IsoFile *continuing = nullptr;
        for (qsizetype at = 0; at < dir.size();) {
            const auto len = quint8(dir[at]);
            if (len == 0) {   // the rest of this sector is padding
                at = (at / kSector + 1) * kSector;
                continue;
            }
            if (len < 34 || at + len > dir.size())
                break;
            const QByteArray rec = dir.mid(at, len);
            at += len;
            const auto nameLength = quint8(rec[32]);
            if (33 + nameLength > rec.size())
                continue;
            const QByteArray rawName = rec.mid(33, nameLength);
            if (rawName == QByteArray(1, '\0') || rawName == QByteArray(1, '\1'))
                continue;   // "." and ".."
            const auto flags = quint8(rec[25]);
            const QString path = prefix + name(rawName);
            if (flags & 0x02) {
                m_files.append(IsoFile{{path, 0, true}, {}});
                walk(rec, path + QLatin1Char('/'), depth + 1);
                continue;
            }
            const qint64 offset = qint64(qFromLittleEndian<quint32>(rec.constData() + 2)) * kSector;
            const qint64 size = qFromLittleEndian<quint32>(rec.constData() + 10);
            if (continuing && continuing->entry.path == path) {
                continuing->extents.append({offset, size});
                continuing->entry.size += size;
            } else {
                m_files.append(IsoFile{{path, size, false}, {{offset, size}}});
                continuing = &m_files.last();
            }
            if (!(flags & 0x80))   // not multi-extent: the file is complete
                continuing = nullptr;
        }
    }

    QFile *m_file = nullptr;
    qint64 m_size = 0;
    bool m_joliet = false;
    QList<IsoFile> m_files;
    QSet<qint64> m_visited;
};

// --- libarchive -------------------------------------------------------------------

struct Reader
{
    ::archive *a = archive_read_new();
    Reader()
    {
        archive_read_support_format_all(a);
        archive_read_support_filter_all(a);
    }
    ~Reader() { archive_read_free(a); }
    QString error() const { return QString::fromUtf8(archive_error_string(a)); }
};

} // namespace

std::optional<QList<Entry>> list(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, file.errorString());
        return std::nullopt;
    }
    QList<Entry> entries;
    if (Iso::detect(file)) {
        Iso iso;
        if (!iso.open(file, error))
            return std::nullopt;
        for (const IsoFile &f : iso.files())
            entries.append(f.entry);
        return entries;
    }
    file.close();
    Reader r;
    if (archive_read_open_filename(r.a, QFile::encodeName(path).constData(), 1 << 16) != ARCHIVE_OK) {
        fail(error, r.error());
        return std::nullopt;
    }
    archive_entry *e;
    int status;
    while ((status = archive_read_next_header(r.a, &e)) == ARCHIVE_OK) {
        entries.append({normalised(QString::fromUtf8(archive_entry_pathname_utf8(e) ? archive_entry_pathname_utf8(e)
                                                                                   : archive_entry_pathname(e))),
                        archive_entry_size(e), archive_entry_filetype(e) == AE_IFDIR});
        archive_read_data_skip(r.a);
    }
    if (status != ARCHIVE_EOF) {
        fail(error, r.error());
        return std::nullopt;
    }
    return entries;
}

namespace {
QString entryPath(archive_entry *e)
{
    const char *utf8 = archive_entry_pathname_utf8(e);
    return normalised(QString::fromUtf8(utf8 ? utf8 : archive_entry_pathname(e)));
}

// Reads the current entry's data; false on an archive error or a cancel.
bool readData(::archive *a, qint64 size, const std::function<bool(const char *, size_t)> &sink)
{
    Q_UNUSED(size);
    char buffer[1 << 16];
    for (;;) {
        const la_ssize_t n = archive_read_data(a, buffer, sizeof buffer);
        if (n == 0)
            return true;
        if (n < 0)
            return false;
        if (!sink(buffer, size_t(n)))
            return false;
    }
}
} // namespace

std::optional<QByteArray> read(const QString &path, const QString &member, QString *error)
{
    const QString want = normalised(member).toLower();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        fail(error, file.errorString());
        return std::nullopt;
    }
    if (Iso::detect(file)) {
        Iso iso;
        if (!iso.open(file, error))
            return std::nullopt;
        for (const IsoFile &f : iso.files())
            if (!f.entry.directory && f.entry.path.toLower() == want)
                return iso.contents(f);
        fail(error, QStringLiteral("%1: not in %2").arg(member, path));
        return std::nullopt;
    }
    file.close();
    Reader r;
    if (archive_read_open_filename(r.a, QFile::encodeName(path).constData(), 1 << 16) != ARCHIVE_OK) {
        fail(error, r.error());
        return std::nullopt;
    }
    archive_entry *e;
    while (archive_read_next_header(r.a, &e) == ARCHIVE_OK) {
        if (entryPath(e).toLower() != want) {
            archive_read_data_skip(r.a);
            continue;
        }
        QByteArray bytes;
        if (!readData(r.a, archive_entry_size(e), [&](const char *data, size_t n) {
                bytes.append(data, qsizetype(n));
                return true;
            })) {
            fail(error, r.error());
            return std::nullopt;
        }
        return bytes;
    }
    fail(error, QStringLiteral("%1: not in %2").arg(member, path));
    return std::nullopt;
}

bool forEach(const QString &path, const std::function<bool(const Entry &)> &want,
             const std::function<bool(const Entry &, const QByteArray &)> &take, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, file.errorString());
    if (Iso::detect(file)) {
        Iso iso;
        if (!iso.open(file, error))
            return false;
        for (const IsoFile &f : iso.files())
            if (!f.entry.directory && want(f.entry) && !take(f.entry, iso.contents(f)))
                return fail(error, QStringLiteral("cancelled"));
        return true;
    }
    file.close();
    Reader r;
    if (archive_read_open_filename(r.a, QFile::encodeName(path).constData(), 1 << 16) != ARCHIVE_OK)
        return fail(error, r.error());
    archive_entry *e;
    int status;
    while ((status = archive_read_next_header(r.a, &e)) == ARCHIVE_OK) {
        const Entry entry{entryPath(e), archive_entry_size(e), archive_entry_filetype(e) == AE_IFDIR};
        if (entry.directory || !want(entry)) {
            archive_read_data_skip(r.a);
            continue;
        }
        QByteArray bytes;
        if (!readData(r.a, entry.size, [&](const char *data, size_t n) {
                bytes.append(data, qsizetype(n));
                return true;
            }))
            return fail(error, r.error());
        if (!take(entry, bytes))
            return fail(error, QStringLiteral("cancelled"));
    }
    if (status != ARCHIVE_EOF)
        return fail(error, r.error());
    return true;
}

bool extract(const QString &path, const QString &directory, const Progress &progress, QString *error)
{
    const QDir dest(directory);
    if (!dest.mkpath(QStringLiteral(".")))
        return fail(error, QStringLiteral("can't create %1").arg(directory));
    auto writeFile = [&](const QString &member, const std::function<bool(QFile &)> &fill) {
        if (!safe(member))
            return fail(error, QStringLiteral("refusing %1: outside the destination").arg(member));
        const QString target = dest.filePath(member);
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail(error, out.errorString());
        return fill(out);
    };

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, file.errorString());
    if (Iso::detect(file)) {
        Iso iso;
        if (!iso.open(file, error))
            return false;
        qint64 total = 0, done = 0;
        for (const IsoFile &f : iso.files())
            total += f.entry.size;
        for (const IsoFile &f : iso.files()) {
            if (f.entry.directory) {
                if (safe(f.entry.path))
                    dest.mkpath(f.entry.path);
                continue;
            }
            if (!writeFile(f.entry.path, [&](QFile &out) {
                    out.write(iso.contents(f));
                    return true;
                }))
                return false;
            done += f.entry.size;
            if (progress && !progress(done, total))
                return fail(error, QStringLiteral("cancelled"));
        }
        return true;
    }
    file.close();

    Reader r;
    if (archive_read_open_filename(r.a, QFile::encodeName(path).constData(), 1 << 16) != ARCHIVE_OK)
        return fail(error, r.error());
    qint64 done = 0;
    const qint64 total = QFileInfo(path).size();   // (packed size: the only total known up front)
    archive_entry *e;
    int status;
    while ((status = archive_read_next_header(r.a, &e)) == ARCHIVE_OK) {
        const QString member = entryPath(e);
        const auto type = archive_entry_filetype(e);
        if (type == AE_IFDIR) {
            if (safe(member))
                dest.mkpath(member);
            continue;
        }
        if (type != AE_IFREG) {       // links, devices: not ours to create
            archive_read_data_skip(r.a);
            continue;
        }
        bool cancelled = false;
        if (!writeFile(member, [&](QFile &out) {
                return readData(r.a, archive_entry_size(e), [&](const char *data, size_t n) {
                    out.write(data, qint64(n));
                    done += qint64(n);
                    if (progress && !progress(qMin(done, total), total)) {
                        cancelled = true;
                        return false;
                    }
                    return true;
                });
            }))
            return cancelled ? fail(error, QStringLiteral("cancelled"))
                             : (error && error->isEmpty() ? fail(error, r.error()) : false);
    }
    if (status != ARCHIVE_EOF)
        return fail(error, r.error());
    return true;
}

std::optional<std::pair<QString, QByteArray>> expandCabinetFile(const QByteArray &cab, QString *error)
{
    ::archive *a = archive_read_new();
    archive_read_support_format_cab(a);
    struct Free
    {
        ::archive *a;
        ~Free() { archive_read_free(a); }
    } guard{a};
    if (archive_read_open_memory(a, cab.constData(), size_t(cab.size())) != ARCHIVE_OK) {
        fail(error, QString::fromUtf8(archive_error_string(a)));
        return std::nullopt;
    }
    archive_entry *e;
    if (archive_read_next_header(a, &e) != ARCHIVE_OK) {
        fail(error, QString::fromUtf8(archive_error_string(a)));
        return std::nullopt;
    }
    const QString name = entryPath(e);
    QByteArray bytes;
    if (!readData(a, archive_entry_size(e), [&](const char *data, size_t n) {
            bytes.append(data, qsizetype(n));
            return true;
        })) {
        fail(error, QString::fromUtf8(archive_error_string(a)));
        return std::nullopt;
    }
    return std::pair(name, bytes);
}

std::optional<QByteArray> expandCabinet(const QByteArray &cab, QString *error)
{
    auto file = expandCabinetFile(cab, error);
    if (!file)
        return std::nullopt;
    return file->second;
}

} // namespace xpl::archive
