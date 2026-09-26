// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// The window border and title bar: a KWin Aurorae theme.
//
// Aurorae draws a window's decoration from one SVG as a FrameSvg -- corners
// at their own size, edges and the middle stretched or tiled -- and its
// buttons from one SVG each. The pieces here are rendered by XP's rules at
// XP's geometry (SM_CXSIZEFRAME frames, SM_CYCAPTION captions, buttons
// SM_CYSIZE - 4 square, 2px in), so what's fixed in XP is fixed here pixel for
// pixel and only what XP itself stretches or tiles is left to KSvg.
#include "app/look.h"

#include <QImage>
#include <QList>
#include <QString>

namespace xpl {

struct DecorationLayout
{
    int frame = 4;            // SM_CXSIZEFRAME
    int caption = 26;         // SM_CYCAPTION
    QSize button;             // caption buttons
    int buttonGap = 2;
};
DecorationLayout decorationLayout(const Look &look);

// The art, before it becomes SVG: decoration.svg's elements by id, and each
// button's ("close" -> "hover-inactive-center" -> image). Hints are empty
// images.
struct DecorationArt
{
    QList<std::pair<QString, QImage>> frame;
    QList<std::pair<QString, QList<std::pair<QString, QImage>>>> buttons;
    QImage element(const QString &id) const;
};
DecorationArt decorationArt(const Look &look);

// Writes the Aurorae package into `directory`/<id>; returns its path, or
// empty with *error. KWin names the theme "__aurorae__svg__<id>".
QString writeDecoration(const Look &look, const QString &directory, QString *error = nullptr);

} // namespace xpl
