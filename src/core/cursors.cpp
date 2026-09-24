#include "cursors.h"

#include "icons.h"

#include <limits>   // before QtEndian: Qt 5.15.2's qfloat16.h uses it unincluded (GCC 11+)

#include <QtEndian>

namespace xpl {

namespace {

quint16 u16(const QByteArray &b, qsizetype at)
{
    return at >= 0 && at + 2 <= b.size() ? qFromLittleEndian<quint16>(b.constData() + at) : 0;
}
quint32 u32(const QByteArray &b, qsizetype at)
{
    return at >= 0 && at + 4 <= b.size() ? qFromLittleEndian<quint32>(b.constData() + at) : 0;
}

// The image an entry points at, if it's there.
QImage entryImage(const QByteArray &data)
{
    return decodeMaskedImage(data, true);
}

} // namespace

std::optional<CursorFrame> decodeCursorFile(const QByteArray &file)
{
    if (file.size() < 6 || u16(file, 0) != 0 || (u16(file, 2) != 1 && u16(file, 2) != 2))
        return std::nullopt;
    const bool cursor = u16(file, 2) == 2;
    const int count = u16(file, 4);
    std::optional<CursorFrame> best;
    int bestDepth = -1;
    for (int i = 0; i < count && i < 64; ++i) {
        const qsizetype at = 6 + qsizetype(i) * 16;
        if (at + 16 > file.size())
            break;
        const quint32 size = u32(file, at + 8), offset = u32(file, at + 12);
        if (offset >= quint32(file.size()) || size > quint32(file.size()) - offset)
            continue;
        const QByteArray data = file.mid(offset, size);
        const QImage img = entryImage(data);
        if (img.isNull())
            continue;
        const int depth = data.startsWith("\x89PNG") ? 32 : u16(data, 14);
        if (best && (img.width() < best->image.width() || (img.width() == best->image.width() && depth <= bestDepth)))
            continue;
        best = CursorFrame{img, cursor ? QPoint(u16(file, at + 4), u16(file, at + 6)) : QPoint(), 0};
        bestDepth = depth;
    }
    return best;
}

QList<CursorFrame> decodeAnimatedCursor(const QByteArray &file)
{
    QList<CursorFrame> out;
    if (file.size() < 12 || !file.startsWith("RIFF") || file.mid(8, 4) != "ACON")
        return out;
    QList<CursorFrame> frames;
    QList<quint32> rates, sequence;
    quint32 steps = 0, defaultRate = 10;
    // The chunks, and those of a LIST "fram" inside them.
    auto walk = [&](auto &&self, qsizetype from, qsizetype to, int depth) -> void {
        for (qsizetype at = from; at + 8 <= to && depth < 4;) {
            const QByteArray id = file.mid(at, 4);
            const quint32 size = u32(file, at + 4);
            const qsizetype body = at + 8;
            if (size > quint32(to - body))
                return;
            if (id == "anih" && size >= 36) {
                steps = u32(file, body + 8);
                defaultRate = u32(file, body + 28);
            } else if (id == "rate") {
                for (quint32 i = 0; i + 4 <= size; i += 4)
                    rates.append(u32(file, body + i));
            } else if (id == "seq ") {
                for (quint32 i = 0; i + 4 <= size; i += 4)
                    sequence.append(u32(file, body + i));
            } else if (id == "LIST" && size >= 4) {
                self(self, body + 4, body + size, depth + 1);
            } else if (id == "icon") {
                if (const auto f = decodeCursorFile(file.mid(body, size)))
                    frames.append(*f);
            }
            at = body + size + (size & 1);   // (chunks are word-aligned)
        }
    };
    walk(walk, 12, file.size(), 0);
    if (frames.isEmpty())
        return out;
    const quint32 n = sequence.isEmpty() ? (steps ? steps : quint32(frames.size())) : quint32(sequence.size());
    for (quint32 i = 0; i < n && i < 1024; ++i) {
        const quint32 index = sequence.isEmpty() ? i : sequence.at(i);
        if (index >= quint32(frames.size()))
            continue;
        CursorFrame f = frames.at(index);
        const quint32 jiffies = i < quint32(rates.size()) ? rates.at(i) : defaultRate;
        f.delayMs = int(qBound<quint32>(1, jiffies, 6000) * 1000 / 60);
        out.append(f);
    }
    return out;
}

QList<CursorFrame> decodeCursor(const QByteArray &file)
{
    if (file.startsWith("RIFF"))
        return decodeAnimatedCursor(file);
    if (const auto f = decodeCursorFile(file))
        return {*f};
    return {};
}

std::optional<CursorFrame> cursorGroup(const PeResources &pe, QStringView group)
{
    const Resource *dir = pe.find(u"12", group);
    if (!dir || u16(dir->data, 0) != 0 || u16(dir->data, 2) != 2)
        return std::nullopt;
    std::optional<CursorFrame> best;
    const int count = u16(dir->data, 4);
    for (int i = 0; i < count && 6 + (i + 1) * 14 <= dir->data.size(); ++i) {
        const Resource *cur = pe.find(u"1", QString::number(u16(dir->data, 6 + i * 14 + 12)));
        if (!cur || cur->data.size() < 4)
            continue;
        const QImage img = entryImage(cur->data.mid(4));
        if (img.isNull() || (best && img.width() <= best->image.width()))
            continue;
        best = CursorFrame{img, QPoint(u16(cur->data, 0), u16(cur->data, 2)), 0};
    }
    return best;
}

QByteArray xcursor(const QList<CursorFrame> &frames, int nominalSize)
{
    auto put32 = [](QByteArray &b, quint32 v) {
        char c[4];
        qToLittleEndian(v, c);
        b.append(c, 4);
    };
    constexpr quint32 kImage = 0xfffd0002, kFileHeader = 16, kTocEntry = 12, kChunkHeader = 36;
    QByteArray out;
    out.append("Xcur", 4);
    put32(out, kFileHeader);
    put32(out, 0x10000);
    put32(out, quint32(frames.size()));
    quint32 position = kFileHeader + kTocEntry * quint32(frames.size());
    for (const CursorFrame &f : frames) {
        put32(out, kImage);
        put32(out, quint32(nominalSize));
        put32(out, position);
        position += kChunkHeader + quint32(f.image.width() * f.image.height()) * 4;
    }
    for (const CursorFrame &f : frames) {
        const QImage img = f.image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        put32(out, kChunkHeader);
        put32(out, kImage);
        put32(out, quint32(nominalSize));
        put32(out, 1);
        put32(out, quint32(img.width()));
        put32(out, quint32(img.height()));
        put32(out, quint32(qBound(0, f.hotspot.x(), img.width() - 1)));
        put32(out, quint32(qBound(0, f.hotspot.y(), img.height() - 1)));
        put32(out, quint32(qMax(0, f.delayMs)));
        for (int y = 0; y < img.height(); ++y) {
            const auto *row = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x)
                put32(out, row[x]);
        }
    }
    return out;
}

} // namespace xpl
