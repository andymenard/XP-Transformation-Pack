#include "dib.h"

#include <QtEndian>

#include <limits>
#include <vector>

namespace xpl {
namespace {

// RLE4 / RLE8, decoded the way GDI does it. Qt's BMP reader leaves pixels the
// run stream never sets uninitialised (verified against XP: an RLE bitmap
// that ends immediately came back with random pixels); GDI makes them palette
// index 0. Ten fan styles reference RLE bitmaps, so this has to be exact.
QImage decodeRle(const uchar *p, qint64 size, quint32 headerSize, int width, int height, int bpp,
                 quint32 clrUsed)
{
    const quint32 colours = clrUsed ? clrUsed : (1u << bpp);
    const qint64 paletteEnd = qint64(headerSize) + qint64(colours) * 4;
    if (height <= 0 || paletteEnd > size)   // RLE bitmaps are always bottom-up
        return QImage();
    QList<QRgb> palette;
    for (quint32 i = 0; i < colours; ++i) {
        const uchar *c = p + headerSize + i * 4;
        palette.append(qRgb(c[2], c[1], c[0]));
    }
    std::vector<uchar> index(size_t(width) * size_t(height), 0);
    int x = 0, row = 0;                      // row counts up from the bottom
    auto put = [&](int value) {
        if (x < width && row < height)
            index[size_t(height - 1 - row) * width + x] = uchar(value);
        ++x;
    };
    qint64 i = paletteEnd;
    while (i + 1 < size && row < height) {
        const int count = p[i], value = p[i + 1];
        i += 2;
        if (count > 0) {                     // encoded run
            for (int k = 0; k < count; ++k)
                put(bpp == 8 ? value : (k % 2 ? value & 0x0f : value >> 4));
        } else if (value == 0) {             // end of line
            x = 0;
            ++row;
        } else if (value == 1) {             // end of bitmap
            break;
        } else if (value == 2) {             // delta
            if (i + 1 >= size)
                break;
            x += p[i];
            row += p[i + 1];
            i += 2;
        } else {                             // absolute run of `value` pixels
            const qint64 bytes = bpp == 8 ? value : (value + 1) / 2;
            if (i + bytes > size)
                break;
            for (int k = 0; k < value; ++k)
                put(bpp == 8 ? p[i + k] : (k % 2 ? p[i + k / 2] & 0x0f : p[i + k / 2] >> 4));
            i += (bytes + 1) & ~qint64(1);   // padded to a 16-bit boundary
        }
    }
    QImage img(width, height, QImage::Format_ARGB32);
    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int xx = 0; xx < width; ++xx) {
            const uchar v = index[size_t(y) * width + xx];
            line[xx] = v < palette.size() ? palette[v] : qRgb(0, 0, 0);
        }
    }
    return img;
}

} // namespace

Dib decodeDib(const QByteArray &data)
{
    Dib out;
    if (data.size() < 40)
        return out;
    const auto *p = reinterpret_cast<const uchar *>(data.constData());
    const quint32 headerSize = qFromLittleEndian<quint32>(p);
    const qint32 width = qFromLittleEndian<qint32>(p + 4);
    const qint32 height = qFromLittleEndian<qint32>(p + 8);
    const quint16 bpp = qFromLittleEndian<quint16>(p + 14);
    const quint32 compression = qFromLittleEndian<quint32>(p + 16);
    const quint32 clrUsed = qFromLittleEndian<quint32>(p + 32);
    if (headerSize < 40 || headerSize > quint32(data.size()) || width <= 0 || width > 32768
        || height == 0 || height == std::numeric_limits<qint32>::min() || qAbs(height) > 32768
        || qint64(width) * qAbs(height) > kMaxDibPixels)
        return out;
    const int h = qAbs(height);

    if (bpp == 32 && compression == 0) {
        const qint64 need = qint64(width) * h * 4;
        if (qint64(headerSize) + need <= data.size()) {
            QImage img(width, h, QImage::Format_ARGB32);
            const uchar *src = p + headerSize;
            uchar maxAlpha = 0;
            for (int y = 0; y < h; ++y) {
                // Bottom-up unless the height is negative.
                const uchar *row = src + qint64(height < 0 ? y : h - 1 - y) * width * 4;
                auto *dst = reinterpret_cast<QRgb *>(img.scanLine(y));
                for (int x = 0; x < width; ++x) {
                    const uchar b = row[x * 4], g = row[x * 4 + 1], r = row[x * 4 + 2], a = row[x * 4 + 3];
                    maxAlpha = qMax(maxAlpha, a);
                    dst[x] = qRgba(r, g, b, a);
                }
            }
            if (maxAlpha == 0) {
                // The alpha byte is unused in this file: opaque.
                for (int y = 0; y < h; ++y) {
                    auto *row = reinterpret_cast<QRgb *>(img.scanLine(y));
                    for (int x = 0; x < width; ++x)
                        row[x] |= 0xff000000u;
                }
            } else {
                out.hasAlpha = true;
            }
            out.image = img;
            return out;
        }
        // Too short for its own header's claim: let Qt try (and likely fail).
    }

    if ((compression == 1 && bpp == 8) || (compression == 2 && bpp == 4)) {
        out.image = decodeRle(p, data.size(), headerSize, width, height, bpp, clrUsed);
        return out;
    }

    // Everything else: prepend a BITMAPFILEHEADER and hand it to Qt's reader.
    // (Its 16bpp 5-5-5 and short-palette decoding match XP's GDI exactly.)
    quint32 paletteSize = 0;
    if (bpp <= 8)
        paletteSize = (clrUsed ? clrUsed : (1u << bpp)) * 4;
    else if (compression == 3 && headerSize == 40)
        paletteSize = 12;   // BI_BITFIELDS: three masks follow a v3 header
    const quint32 pixelOffset = 14 + headerSize + paletteSize;
    QByteArray file;
    file.reserve(14 + data.size());
    uchar fh[14] = {'B', 'M'};
    qToLittleEndian<quint32>(quint32(14 + data.size()), fh + 2);
    qToLittleEndian<quint32>(pixelOffset, fh + 10);
    file.append(reinterpret_cast<const char *>(fh), 14);
    file.append(data);
    QImage img;
    if (img.loadFromData(file, "BMP"))
        out.image = img.convertToFormat(QImage::Format_ARGB32);
    return out;
}

} // namespace xpl
