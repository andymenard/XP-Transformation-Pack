// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// XP's visual-style schema: class -> part names/ids -> state names/ids.
//
// uxtheme addresses parts and states by number (tmschema.h, XP SP2 SDK),
// while the INI inside a .msstyles addresses them by name. The number
// matters twice over: it is what DrawThemeBackground and the oracle take,
// and uxtheme picks a frame out of an image strip by state number (frame =
// state - 1). So the names have to map exactly as XP maps them; the oracle
// checks every entry used by rendering it by number and comparing.

#include <QString>

namespace xpl::schema {

// 0 when unknown (uxtheme ignores INI sections whose part or state it
// doesn't know, and so do we).
int partId(QStringView cls, QStringView part);
int stateId(QStringView cls, QStringView part, QStringView state);

// The reverse, lower-case; empty when unknown.
QString partName(QStringView cls, int part);
QString stateName(QStringView cls, QStringView part, int state);

} // namespace xpl::schema
