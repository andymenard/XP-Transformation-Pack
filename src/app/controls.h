// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Plasma's own controls -- the buttons, check boxes, radio buttons, text
// fields, list highlights, progress bars, sliders, scroll bars and tabs in
// its popups -- as XP draws them. Each is drawn by XPlasma's Qt style for
// the look, in the states Plasma shows, and cut into the frames its theme's
// SVGs hold (widgets/button.svg and the rest); the Plasma theme carries
// them beside the taskbar.
#include "app/look.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QMap>
#include <QString>

class QStyle;

namespace xpl {

// XPlasma's Qt style drawing `look` (one instance per look, kept), or null:
// it needs its plugin and a QApplication.
QStyle *xpStyle(const Look &look);

// widgets/<name>.svg's elements, by name; empty without the style (Plasma's
// own controls stay then).
QHash<QString, QList<std::pair<QString, QImage>>> plasmaControls(const Look &look);

// The icons Plasma's spin boxes put on their buttons (value-decrease,
// value-increase; nothing else of Plasma's asks for them), by size: the
// arrows of XP's up-down on its side, as XP's tab rows have it -- pointing
// left, and right. For the icon theme; empty for a Classic look without the
// style.
QHash<QString, QMap<int, QImage>> spinIcons(const Look &look);

} // namespace xpl
