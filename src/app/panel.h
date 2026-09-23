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
#include <QString>

namespace xpl {

constexpr int kTaskbarHeight = 30;   // XP's, at 96 dpi

struct PanelArt
{
    QList<std::pair<QString, QImage>> panel;   // panel-background.svg's elements (hints: null images)
    QList<std::pair<QString, QImage>> tasks;   // tasks.svg's
    QList<std::pair<QString, QImage>> tray;    // xp-tray.svg's: the tray and clock backdrops, the expand buttons
    QColor panelColour;                        // the bar's average colour
    QColor textColour;                         // task and clock text
    int height = kTaskbarHeight;
};
PanelArt panelArt(const Look &look);

// Writes the Plasma desktop theme into `directory`/<id>-taskbar; returns its
// path, or empty with *error.
QString writePanelTheme(const Look &look, const QString &directory, QString *error = nullptr);

} // namespace xpl
