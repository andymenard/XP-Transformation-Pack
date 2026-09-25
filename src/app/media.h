#pragma once
// Windows XP's own files, from the install media the user pointed XPlasma at
// (State::media: the ISO, or a folder holding I386/). Some of the look lives
// in XP's programs rather than in the visual style -- the Start button's
// flag is explorer.exe's bitmap 143, Display Properties' monitor is
// themeui.dll's bitmap 126 -- so they are taken out once and kept in
// paths::extracted(). Nothing of Microsoft's ships with XPlasma.
#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>

#include <optional>

namespace xpl::media {

// A file of an installed XP ("explorer.exe"), from the extracted cache or
// the media: I386/<NAME> or its CAB-compressed I386/<NAM>_, expanded.
std::optional<QByteArray> systemFile(const QString &name, QString *error = nullptr);

// A bitmap resource of such a file, decoded (alpha kept).
std::optional<QImage> bitmap(const QString &file, int id, QString *error = nullptr);

// An icon group of such a file at `size` pixels (the deepest image of that
// size; else the nearest larger, scaled down).
std::optional<QImage> icon(const QString &file, int group, int size, QString *error = nullptr);

// An animation resource ("AVI") of such a file -- shell32.dll's file
// operations (161 copying, 160 moving, 162 deleting): its frames, in
// order, magenta see-through. They play at 10 a second.
std::optional<QList<QImage>> animation(const QString &file, int id, QString *error = nullptr);
// (A Microsoft RLE8 AVI's frames, each drawn over the last.)
QList<QImage> decodeRleAvi(const QByteArray &avi);

// The Start button's flag: explorer.exe's bitmap 143 (25 x 20, real alpha)
// with a visual style, 176 (16 x 16) in Windows Classic.
std::optional<QImage> startFlag(QString *error = nullptr);
std::optional<QImage> classicStartFlag(QString *error = nullptr);

} // namespace xpl::media
