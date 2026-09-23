#pragma once
// Taking over from xp2plasma, once: its imported asset library (styles,
// wallpapers, fonts, icons -- ~100 MB distilled from ~600 MB of XP media), the
// user's saved schemes and the XP install ISO move into XPLasma's own places,
// and the ISO is recorded as the XP media. Nothing else of xp2plasma's is
// carried over.
#include <QStringList>

namespace xpl {

// Moves what XPLasma doesn't have yet, leaving a symlink at the old place;
// returns a line per thing moved (empty when there was nothing to do). A move
// that fails is reported, not fatal.
QStringList adoptLegacy();

} // namespace xpl
