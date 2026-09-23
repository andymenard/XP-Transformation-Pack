#pragma once
// Decode the bare DIBs msstyles store as RT_BITMAP resources
// (BITMAPINFOHEADER + optional palette/masks + pixels; no file header).

#include <QByteArray>
#include <QImage>

namespace xpl {

struct Dib
{
    QImage image;           // Format_ARGB32; null if undecodable
    bool hasAlpha = false;  // a 32bpp bitmap whose alpha channel is actually used
};

// 32bpp BI_RGB bitmaps are decoded here, keeping the alpha channel -- unless
// every alpha byte is zero, in which case the channel is unused and the image
// is opaque. (uxtheme treats it the same way; so does xp2plasma's parser.)
// Everything else goes through Qt's own BMP reader.
// A header may claim any size, and a compressed (RLE) one costs almost nothing
// to write: 217 fuzzer-made bytes claimed 26886 x 8229 and took 11 s and a
// gigabyte to decode. Nothing larger than this is ever decoded; the largest
// bitmap in the 128-style corpus is 200 x 551, so this is 38x headroom.
constexpr qint64 kMaxDibPixels = 4 * 1024 * 1024;

Dib decodeDib(const QByteArray &data);

} // namespace xpl
