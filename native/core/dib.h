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
Dib decodeDib(const QByteArray &data);

} // namespace xpl
