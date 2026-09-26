// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Text measured the way GDI measures it, for laying text out where XP does.
// GDI advances glyphs by whole pixels -- a TrueType font's hdmx table gives
// them per size, else the rounded scaled advance -- and centres a line by
// the font's Windows ascent and descent (OS/2 usWinAscent/usWinDescent).
#include <QFont>
#include <QString>

namespace xpl {

struct GdiMetrics
{
    int ascent = 0;    // tmAscent
    int descent = 0;   // tmDescent
    int height() const { return ascent + descent; }
};

// For a font with a pixel size (the em, as a negative lfHeight gives it).
GdiMetrics gdiMetrics(const QFont &font);
// GetTextExtentPoint32's width.
int gdiTextWidth(const QFont &font, const QString &text);
// Whether XP's standard font smoothing greys this font at this size: the
// font's gasp table decides (Tahoma 8 is drawn bi-level, Franklin Gothic 14
// smoothed). QFont::NoAntialias otherwise.
bool gdiSmooths(const QFont &font);
QFont gdiFont(QFont font);   // with that decided

} // namespace xpl
