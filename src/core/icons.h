#pragma once
// Icons out of a PE file: an RT_GROUP_ICON directory names RT_ICON images
// by resource id (14-byte entries, not an .ico's 16-byte ones), each image a
// PNG or a DIB -- colours, then a 1bpp AND mask -- at twice its height.
// https://devblogs.microsoft.com/oldnewthing/20120720-00/?p=7083
#include "pe.h"

#include <QImage>
#include <QMap>

namespace xpl {

struct IconImage
{
    QImage image;       // ARGB32, transparent where the mask says
    int bitDepth = 0;   // of the image it came from
};

// One RT_ICON payload.
QImage decodeIconImage(const QByteArray &data);

// A group's square images by size, the deepest of each size (XP keeps 4-,
// 8- and 32-bit versions side by side).
QMap<int, IconImage> iconGroup(const PeResources &pe, QStringView group, QString *error = nullptr);

} // namespace xpl
