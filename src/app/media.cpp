#include "app/media.h"

#include "app/archive.h"
#include "app/paths.h"
#include "app/state.h"
#include "core/dib.h"
#include "core/icons.h"
#include "core/pe.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace xpl::media {

namespace {

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

// "explorer.exe" -> "EXPLORER.EX_": how XP's setup media compresses it.
QString compressedName(const QString &name)
{
    QString packed = name.toUpper();
    packed[packed.size() - 1] = QLatin1Char('_');
    return packed;
}

// A file under a folder, found case-insensitively, at most two levels down
// (media folders are the CD root, or I386 itself).
QString findIn(const QString &root, const QString &name)
{
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    const int depth = QDir(root).absolutePath().count(QLatin1Char('/'));
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.count(QLatin1Char('/')) - depth > 2)
            continue;
        if (QFileInfo(path).fileName().compare(name, Qt::CaseInsensitive) == 0)
            return path;
    }
    return QString();
}

// A member of the media, as it is stored.
std::optional<QByteArray> fromMedia(const QString &media, const QString &member, QString *error)
{
    if (QFileInfo(media).isDir()) {
        const QString path = findIn(media, member);
        QFile f(path);
        if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) {
            fail(error, QStringLiteral("%1 isn't in %2").arg(member, media));
            return std::nullopt;
        }
        return f.readAll();
    }
    return archive::read(media, QLatin1String("I386/") + member, error);
}

} // namespace

std::optional<QByteArray> systemFile(const QString &name, QString *error)
{
    const QString cached = paths::extracted() + QLatin1Char('/') + name.toLower();
    if (QFile f(cached); f.open(QIODevice::ReadOnly))
        return f.readAll();

    const QString media = State::load().media;
    if (media.isEmpty() || !QFileInfo::exists(media)) {
        fail(error, QStringLiteral("no Windows XP media (run setup, or point XPLasma at the XP ISO)"));
        return std::nullopt;
    }
    QString why;
    std::optional<QByteArray> bytes = fromMedia(media, name.toUpper(), &why);
    if (!bytes) {
        const auto packed = fromMedia(media, compressedName(name), error);
        if (!packed)
            return std::nullopt;
        bytes = archive::expandCabinet(*packed, error);
        if (!bytes)
            return std::nullopt;
    }
    QDir().mkpath(paths::extracted());
    QSaveFile out(cached);
    if (out.open(QIODevice::WriteOnly) && out.write(*bytes) == bytes->size())
        out.commit();   // (a cache: failing to keep it isn't failing)
    return bytes;
}

std::optional<QImage> bitmap(const QString &file, int id, QString *error)
{
    const auto bytes = systemFile(file, error);
    if (!bytes)
        return std::nullopt;
    const auto pe = PeResources::parse(*bytes, error);
    if (!pe)
        return std::nullopt;
    const Resource *r = pe->find(u"2", QString::number(id));   // RT_BITMAP
    if (!r) {
        fail(error, QStringLiteral("%1 has no bitmap %2").arg(file).arg(id));
        return std::nullopt;
    }
    const Dib dib = decodeDib(r->data);
    if (dib.image.isNull()) {
        fail(error, QStringLiteral("%1's bitmap %2 doesn't decode").arg(file).arg(id));
        return std::nullopt;
    }
    return dib.image;
}

QList<QImage> decodeRleAvi(const QByteArray &avi)
{
    // RIFF "AVI ": the stream format ("strf": a BITMAPINFOHEADER and its
    // palette), then LIST "movi" of frames ("00dc"), each Microsoft RLE8
    // drawn over the one before (a frame only says what changed). Bottom-up.
    QList<QImage> frames;
    // (Byte by byte: bounds-checked, and no unaligned reads.)
    auto byte = [&](qsizetype at) { return at >= 0 && at < avi.size() ? quint32(uchar(avi.at(at))) : 0u; };
    auto u32 = [&](qsizetype at) { return byte(at) | byte(at + 1) << 8 | byte(at + 2) << 16 | byte(at + 3) << 24; };
    const qsizetype strf = avi.indexOf("strf");
    if (!avi.startsWith("RIFF") || strf < 0)
        return frames;
    const qsizetype info = strf + 8;
    const quint32 headerSize = u32(info);
    const int w = int(u32(info + 4)), h = int(u32(info + 8));
    const quint32 bits = byte(info + 14) | byte(info + 15) << 8;
    quint32 colours = u32(info + 32);
    if (w <= 0 || h <= 0 || w > 2048 || h > 2048 || bits != 8)
        return frames;
    if (colours == 0 || colours > 256)
        colours = 256;
    QList<QRgb> palette(256, qRgb(255, 0, 255));
    for (quint32 i = 0; i < colours; ++i) {
        const qsizetype at = info + headerSize + qsizetype(i) * 4;
        if (at + 4 > avi.size())
            break;
        palette[int(i)] = qRgb(uchar(avi.at(at + 2)), uchar(avi.at(at + 1)), uchar(avi.at(at)));
    }
    QByteArray canvas(qsizetype(w) * h, '\0');
    const qsizetype movi = avi.indexOf("movi", info);
    qsizetype end = avi.indexOf("idx1", movi);
    if (movi < 0)
        return frames;
    if (end < 0)
        end = avi.size();
    for (qsizetype at = movi + 4; at + 8 <= end && frames.size() < 512;) {
        const QByteArray id = avi.mid(at, 4);
        const quint32 size = u32(at + 4);
        const qsizetype body = at + 8;
        if (size > quint32(end - body))
            break;
        at = body + size + (size & 1);
        if (!id.endsWith("dc") && !id.endsWith("db"))
            continue;
        const auto *d = reinterpret_cast<const uchar *>(avi.constData() + body);
        int x = 0, y = 0;
        auto put = [&](uchar c) {
            if (x >= 0 && x < w && y >= 0 && y < h)
                canvas[qsizetype(y) * w + x] = char(c);
            ++x;
        };
        for (quint32 k = 0; k + 1 < size;) {
            const uchar count = d[k], value = d[k + 1];
            k += 2;
            if (count > 0) {
                for (int n = 0; n < count; ++n)
                    put(value);
            } else if (value == 0) {        // end of line
                x = 0;
                ++y;
            } else if (value == 1) {        // end of frame
                break;
            } else if (value == 2) {        // delta
                if (k + 2 > size)
                    break;
                x += d[k];
                y += d[k + 1];
                k += 2;
            } else {                        // a literal run, word-aligned
                for (int n = 0; n < value && k + quint32(n) < size; ++n)
                    put(d[k + n]);
                k += value + (value & 1);
            }
        }
        QImage frame(w, h, QImage::Format_ARGB32);
        for (int row = 0; row < h; ++row) {
            auto *out = reinterpret_cast<QRgb *>(frame.scanLine(h - 1 - row));
            for (int col = 0; col < w; ++col) {
                const QRgb c = palette.at(uchar(canvas.at(qsizetype(row) * w + col)));
                out[col] = (c & 0xffffff) == 0xff00ff ? 0u : c;   // magenta: see-through
            }
        }
        frames.append(frame);
    }
    return frames;
}

std::optional<QList<QImage>> animation(const QString &file, int id, QString *error)
{
    const auto bytes = systemFile(file, error);
    if (!bytes)
        return std::nullopt;
    const auto pe = PeResources::parse(*bytes, error);
    if (!pe)
        return std::nullopt;
    const Resource *r = pe->find(u"AVI", QString::number(id));
    const QList<QImage> frames = r ? decodeRleAvi(r->data) : QList<QImage>();
    if (frames.isEmpty()) {
        fail(error, QStringLiteral("%1 has no animation %2 it can play").arg(file).arg(id));
        return std::nullopt;
    }
    return frames;
}

std::optional<QImage> icon(const QString &file, int group, int size, QString *error)
{
    const auto bytes = systemFile(file, error);
    if (!bytes)
        return std::nullopt;
    const auto pe = PeResources::parse(*bytes, error);
    if (!pe)
        return std::nullopt;
    const QMap<int, IconImage> images = iconGroup(*pe, QString::number(group), error);
    if (images.isEmpty()) {
        fail(error, QStringLiteral("%1 has no icon %2").arg(file).arg(group));
        return std::nullopt;
    }
    if (images.contains(size))
        return images.value(size).image;
    // (the nearest larger, scaled down)
    auto it = images.lowerBound(size);
    const QImage from = (it != images.cend() ? it : std::prev(images.cend()))->image;
    return from.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

std::optional<QImage> startFlag(QString *error)
{
    return bitmap(QStringLiteral("explorer.exe"), 143, error);
}

std::optional<QImage> classicStartFlag(QString *error)
{
    return bitmap(QStringLiteral("explorer.exe"), 176, error);
}

} // namespace xpl::media
