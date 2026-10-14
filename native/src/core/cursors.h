// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Windows cursors -- .cur files, .ani animations, a PE's cursor groups (XP's
// defaults live in user32.dll) -- as frames; and Xcursor, the format X and
// Wayland desktops load cursor themes in.
//
// .cur: an ICONDIR of type 2 whose entries carry the hotspot; images are
// DIBs (colours, then an AND mask) or PNGs. .ani: RIFF "ACON" -- "anih"
// (frame count, step count, default rate in jiffies, 1/60 s), optional
// "rate" and "seq ", and LIST "fram" of "icon" chunks, each a .cur (or .ico).
// Xcursor: https://www.x.org/releases/current/doc/man/man3/Xcursor.3.xhtml
#include "pe.h"

#include <QImage>
#include <QList>
#include <QPoint>

#include <optional>

namespace xpl {

struct CursorFrame
{
    QImage image;       // ARGB32
    QPoint hotspot;
    int delayMs = 0;    // how long it shows, in an animation
};

// A .cur (or .ico) file: its largest, deepest image.
std::optional<CursorFrame> decodeCursorFile(const QByteArray &file);
// An .ani: its frames in play order, each with its delay.
QList<CursorFrame> decodeAnimatedCursor(const QByteArray &file);
// Either, by what it is.
QList<CursorFrame> decodeCursor(const QByteArray &file);

// A PE's cursor group (RT_GROUP_CURSOR, 12): its largest image. Each
// RT_CURSOR payload starts with its hotspot (two WORDs).
std::optional<CursorFrame> cursorGroup(const PeResources &pe, QStringView group);

// An Xcursor file holding every frame at one nominal size.
QByteArray xcursor(const QList<CursorFrame> &frames, int nominalSize);

} // namespace xpl
