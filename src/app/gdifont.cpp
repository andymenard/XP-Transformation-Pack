#include "app/gdifont.h"

#include <QRawFont>
#include <QtEndian>

#include <cmath>

namespace xpl {

namespace {
quint16 u16(const QByteArray &table, int at)
{
    return at + 2 <= table.size() ? qFromBigEndian<quint16>(table.constData() + at) : 0;
}
} // namespace

GdiMetrics gdiMetrics(const QFont &font)
{
    const QRawFont raw = QRawFont::fromFont(font);
    const QByteArray head = raw.fontTable("head"), os2 = raw.fontTable("OS/2");
    const int upem = u16(head, 18);
    const int px = font.pixelSize() > 0 ? font.pixelSize() : int(std::lround(raw.pixelSize()));
    GdiMetrics m;
    if (upem > 0 && os2.size() >= 78) {
        m.ascent = int(std::lround(double(u16(os2, 74)) * px / upem));
        m.descent = int(std::lround(double(u16(os2, 76)) * px / upem));
    } else {
        m.ascent = int(std::lround(raw.ascent()));
        m.descent = int(std::lround(raw.descent()));
    }
    return m;
}

int gdiTextWidth(const QFont &font, const QString &text)
{
    const QRawFont raw = QRawFont::fromFont(font);
    const QList<quint32> glyphs = raw.glyphIndexesForString(text);
    const int px = font.pixelSize() > 0 ? font.pixelSize() : int(std::lround(raw.pixelSize()));
    // hdmx: version, numRecords, sizeDeviceRecord; then per size
    // {pixelSize, maxWidth, widths[numGlyphs]}.
    const QByteArray hdmx = raw.fontTable("hdmx");
    if (hdmx.size() >= 8) {
        const int records = qint16(u16(hdmx, 2));
        const int recordSize = int(qFromBigEndian<qint32>(hdmx.constData() + 4));
        for (int i = 0; i < records && recordSize > 2; ++i) {
            const int at = 8 + i * recordSize;
            if (at + recordSize > hdmx.size() || quint8(hdmx[at]) != px)
                continue;
            int width = 0;
            for (const quint32 g : glyphs)
                width += int(g) + 2 < recordSize ? quint8(hdmx[at + 2 + int(g)]) : 0;
            return width;
        }
    }
    const QByteArray head = raw.fontTable("head"), hhea = raw.fontTable("hhea"), hmtx = raw.fontTable("hmtx");
    const int upem = u16(head, 18), metrics = u16(hhea, 34);
    int width = 0;
    for (const quint32 g : glyphs) {
        const int index = int(g) < metrics ? int(g) : metrics - 1;
        width += upem > 0 ? int(std::lround(double(u16(hmtx, index * 4)) * px / upem)) : 0;
    }
    return width;
}

bool gdiSmooths(const QFont &font)
{
    const QRawFont raw = QRawFont::fromFont(font);
    const QByteArray gasp = raw.fontTable("gasp");
    const int px = font.pixelSize() > 0 ? font.pixelSize() : int(std::lround(raw.pixelSize()));
    // gasp: version, numRanges, then {rangeMaxPPEM, rangeGaspBehavior};
    // GASP_DOGRAY is 0x0002.
    const int ranges = u16(gasp, 2);
    for (int i = 0; i < ranges && 4 + (i + 1) * 4 <= gasp.size(); ++i)
        if (px <= u16(gasp, 4 + i * 4))
            return u16(gasp, 6 + i * 4) & 0x0002;
    return gasp.isEmpty();   // (no table: smoothed)
}

QFont gdiFont(QFont font)
{
    font.setHintingPreference(QFont::PreferFullHinting);
    if (!gdiSmooths(font))
        font.setStyleStrategy(QFont::NoAntialias);
    return font;
}

} // namespace xpl
