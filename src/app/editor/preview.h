#pragma once
// The editor's samples, drawn by what they preview:
//
//   * Appearance: an inactive window, an active one, and a message box -- XP's
//     Appearance sample -- as real Qt widgets drawn offscreen by XPLasma's own
//     style plugin in that look, which is what Qt apps will be drawn with;
//   * Desktop: XP's monitor (themeui.dll's bitmap 126) with the wallpaper
//     where it would sit;
//   * Themes: the wallpaper, the windows, and the look's taskbar and Start
//     button, as XPLasma generates them.
#include "app/apply.h"
#include "app/look.h"

#include <QImage>
#include <QTemporaryDir>

class QStyle;

namespace xpl::preview {

// XP's sample boxes (measured off Display Properties).
constexpr QSize kThemesSize{347, 219};
constexpr QSize kAppearanceSize{347, 180};

class Previewer
{
public:
    Previewer();

    // (Style plugins for a look, made once and kept.)
    QImage appearance(const Look &look, QSize size, const QColor &background);
    QImage theme(const Look &look, const Wallpaper &desktop, QSize size);
    QImage desktop(const Wallpaper &wallpaper, QSize size);

    // Whether XPLasma's style plugin could be loaded (the samples need it).
    bool styleAvailable() const;

private:
    QStyle *styleFor(const Look &look);
    QImage windows(const Look &look, QSize size, const QImage &behind);

    QTemporaryDir m_scratch;
    QHash<QString, QStyle *> m_styles;
    QImage m_monitor;   // themeui.dll's, magenta keyed out
    bool m_monitorTried = false;
};

// Where a wallpaper goes, as Plasma's FillMode puts it, over `colour`.
QImage placeWallpaper(const QImage &image, int fill, const QColor &colour, QSize size);

} // namespace xpl::preview
