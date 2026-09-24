#include "icons.h"

#include <limits>   // before QtEndian: Qt 5.15.2's qfloat16.h uses it unincluded (GCC 11+)

#include <QtEndian>

namespace xpl {

namespace {
quint16 u16(const QByteArray &b, int at)
{
    return qFromLittleEndian<quint16>(b.constData() + at);
}
quint32 u32(const QByteArray &b, int at)
{
    return qFromLittleEndian<quint32>(b.constData() + at);
}
} // namespace

QImage decodeIconImage(const QByteArray &data)
{
    return decodeMaskedImage(data, false);
}

QImage decodeMaskedImage(const QByteArray &data, bool cursor)
{
    if (data.startsWith("\x89PNG"))
        return QImage::fromData(data, "PNG").convertToFormat(QImage::Format_ARGB32);
    if (data.size() < 40)
        return QImage();
    const quint32 header = u32(data, 0);
    const int width = qint32(u32(data, 4));
    const int height = qint32(u32(data, 8)) / 2;   // colours and mask
    const int bpp = u16(data, 14);
    const quint32 compression = u32(data, 16);
    quint32 colours = u32(data, 32);
    if (header < 40 || width <= 0 || height <= 0 || width > 1024 || height > 1024 || compression != 0
        || !(bpp == 1 || bpp == 4 || bpp == 8 || bpp == 24 || bpp == 32))
        return QImage();
    if (bpp <= 8 && colours == 0)
        colours = 1u << bpp;
    if (bpp > 8)
        colours = 0;
    const qint64 palette = header, pixels = palette + qint64(colours) * 4;
    const qint64 stride = ((qint64(width) * bpp + 31) / 32) * 4, maskStride = ((width + 31) / 32) * 4;
    const qint64 mask = pixels + stride * height;
    if (mask + maskStride * height > data.size())
        return QImage();
    const auto *p = reinterpret_cast<const uchar *>(data.constData());
    QImage img(width, height, QImage::Format_ARGB32);
    bool alpha = false;
    for (int y = 0; y < height; ++y) {
        const uchar *row = p + pixels + stride * (height - 1 - y);   // bottom-up
        auto *out = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < width; ++x) {
            QRgb c = 0;
            if (bpp == 32) {
                c = qRgba(row[x * 4 + 2], row[x * 4 + 1], row[x * 4], row[x * 4 + 3]);
                alpha |= row[x * 4 + 3] != 0;
            } else if (bpp == 24) {
                c = qRgb(row[x * 3 + 2], row[x * 3 + 1], row[x * 3]);
            } else {
                const int bit = x * bpp;
                const int index = (row[bit / 8] >> (8 - bpp - bit % 8)) & ((1 << bpp) - 1);
                const uchar *e = p + palette + (quint32(index) < colours ? index : 0) * 4;
                c = qRgb(e[2], e[1], e[0]);
            }
            out[x] = c;
        }
    }
    // Without an alpha channel, the AND mask's set bits are transparent --
    // unless the colour isn't black: that pixel inverts the screen, which a
    // cursor (only) does, and which comes out black here (its shape).
    if (!alpha)
        for (int y = 0; y < height; ++y) {
            const uchar *row = p + mask + maskStride * (height - 1 - y);
            auto *out = reinterpret_cast<QRgb *>(img.scanLine(y));
            for (int x = 0; x < width; ++x) {
                const bool masked = (row[x / 8] >> (7 - x % 8)) & 1;
                if (!masked)
                    out[x] |= 0xff000000u;
                else
                    out[x] = cursor && (out[x] & 0xffffffu) ? 0xff000000u : 0u;
            }
        }
    return img;
}

QMap<int, IconImage> iconGroup(const PeResources &pe, QStringView group, QString *error)
{
    QMap<int, IconImage> out;
    const Resource *dir = pe.find(u"14", group);
    if (!dir || dir->data.size() < 6 || u16(dir->data, 0) != 0 || u16(dir->data, 2) != 1) {
        if (error)
            *error = QStringLiteral("no icon group %1").arg(group.toString());
        return out;
    }
    const int count = u16(dir->data, 4);
    for (int i = 0; i < count && 6 + (i + 1) * 14 <= dir->data.size(); ++i) {
        const int at = 6 + i * 14;
        const int id = u16(dir->data, at + 12);
        const Resource *icon = pe.find(u"3", QString::number(id));
        if (!icon)
            continue;
        const QImage img = decodeIconImage(icon->data);
        if (img.isNull() || img.width() != img.height())
            continue;
        const int bits = icon->data.startsWith("\x89PNG") ? 32 : u16(icon->data, 14);
        auto it = out.find(img.width());
        if (it == out.end() || it->bitDepth < bits)
            out.insert(img.width(), {img, bits});
    }
    if (out.isEmpty() && error)
        *error = QStringLiteral("icon group %1 has no usable images").arg(group.toString());
    return out;
}

} // namespace xpl
