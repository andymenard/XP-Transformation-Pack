#pragma once
// The taskbar: a Plasma desktop theme ("Plasma Style") holding the panel's
// background, the task buttons, and the notification area's art, plus the
// colours Plasma draws panel text in.
//
// Measured on XP (Luna, 800 x 600): the taskbar is 30px tall and its task
// buttons are TaskBand::Toolbar buttons drawn the full height of it (their
// art keeps its own transparent inset); XP's own renders of both match ours
// exactly. Everything is drawn at that height, so Plasma only ever stretches
// or tiles along the bar.
#include "app/look.h"

#include <QImage>
#include <QList>
#include <QMargins>
#include <QString>

namespace xpl {

constexpr int kTaskbarHeight = 30;   // XP's, at 96 dpi

struct PanelArt
{
    QList<std::pair<QString, QImage>> panel;   // panel-background.svg's elements (hints: null images)
    QList<std::pair<QString, QImage>> tasks;   // tasks.svg's
    QList<std::pair<QString, QImage>> tray;    // xp-tray.svg's: the tray and clock backdrops, the expand buttons
    // xp-notify.svg's: what notification balloons and the copy dialog draw
    // with -- the balloon's close button, XP's severity icons, push buttons,
    // a text box, the progress bar, and shell32's file animations.
    QList<std::pair<QString, QImage>> notify;
    // dialogs/background.svg's and widgets/tooltip.svg's: Plasma's popups
    // (the tray's, KRunner's...) and tooltips, as XP's flyouts and tooltips.
    QList<std::pair<QString, QImage>> popup;
    QList<std::pair<QString, QImage>> tooltip;
    QColor panelColour;                        // the bar's average colour
    QColor textColour;                         // task and clock text
    QColor trayColour;                         // the notification area's average colour
    QColor trayTextColour;                     // the clock's text: what reads on the tray
    int height = kTaskbarHeight;
};
PanelArt panelArt(const Look &look);

// FrameSvg's nine pieces of `image` for `prefix`, cut at `margins`: its
// middle stretched (hint-stretch-borders) or, `tilePeriod` > 0, tiled; the
// content hint margins where given. (How all XPLasma's frames are cut.)
void appendFrame(QList<std::pair<QString, QImage>> &out, const QString &prefix, const QImage &image, QMargins margins,
                 int tilePeriod = 0, const QMargins &content = QMargins());

// What KSvg's FrameSvg paints for `prefix` at `size` from such elements:
// corners at their own size, edges and centre stretched (with
// hint-stretch-borders) or tiled. (The previews and tests draw with it.)
QImage composeFrame(const QList<std::pair<QString, QImage>> &elements, const QString &prefix, QSize size);

// Writes the Plasma desktop theme into `directory`/<id>-taskbar; returns its
// path, or empty with *error.
QString writePanelTheme(const Look &look, const QString &directory, QString *error = nullptr);

} // namespace xpl
