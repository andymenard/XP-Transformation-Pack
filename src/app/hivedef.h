#pragma once
// XP's stock Windows Classic colour schemes, read from where XP itself gets
// them: I386\HIVEDEF.INF, the default user hive, whose
// HKCU\Control Panel\Appearance\Schemes values are SCHEMEDATA blobs named
// through its [Strings] section ("%BRICK%" -> "Brick").
//
// XPLasma ships these as .theme files (src/app/data/schemes, made by
// xplasma-schemes from the XP ISO), so Classic works with no XP media; the
// app tests check the shipped files against the ISO when it's present.
#include "core/classic.h"

#include <QByteArray>
#include <QList>

namespace xpl::hivedef {

// Every scheme in the file, in its order, each named.
QList<ClassicScheme> schemes(const QByteArray &inf);

// A stock scheme as a .theme file's bytes: [Theme] DisplayName, then the
// colours and metrics as XP's Display Properties writes them.
QByteArray themeBytes(const ClassicScheme &scheme);

} // namespace xpl::hivedef
