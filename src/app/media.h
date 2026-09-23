#pragma once
// Windows XP's own files, from the install media the user pointed XPLasma at
// (State::media: the ISO, or a folder holding I386/). Some of the look lives
// in XP's programs rather than in the visual style -- the Start button's
// flag is explorer.exe's bitmap 143, Display Properties' monitor is
// themeui.dll's bitmap 126 -- so they are taken out once and kept in
// paths::extracted(). Nothing of Microsoft's ships with XPLasma.
#include <QByteArray>
#include <QImage>
#include <QString>

#include <optional>

namespace xpl::media {

// A file of an installed XP ("explorer.exe"), from the extracted cache or
// the media: I386/<NAME> or its CAB-compressed I386/<NAM>_, expanded.
std::optional<QByteArray> systemFile(const QString &name, QString *error = nullptr);

// A bitmap resource of such a file, decoded (alpha kept).
std::optional<QImage> bitmap(const QString &file, int id, QString *error = nullptr);

// The Start button's flag: explorer.exe's bitmap 143 (25 x 20, real alpha)
// with a visual style, 176 (16 x 16) in Windows Classic.
std::optional<QImage> startFlag(QString *error = nullptr);
std::optional<QImage> classicStartFlag(QString *error = nullptr);

} // namespace xpl::media
