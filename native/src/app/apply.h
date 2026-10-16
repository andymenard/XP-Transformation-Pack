// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Putting a look on the running desktop, and taking it off again.
//
// Apply generates every package for the look, installs them under
// ~/.local/share, and switches the session to them: KWin's window borders,
// the colour scheme and fonts, Qt's widget style, the taskbar (its Plasma
// theme, the panel as XP's, the Start button), the wallpaper, the icons.
// What it installed says so (the X-XPlasma-Generated marker), so reset
// removes XPlasma's and only XPlasma's.
#include "app/forks.h"
#include "app/look.h"
#include "app/session.h"
#include "app/state.h"

#include <QColor>
#include <QString>

#include <functional>
#include <optional>

namespace xpl {

struct Wallpaper
{
    QString image;             // empty: the colour alone
    std::optional<int> fill;   // Plasma FillMode, or leave the user's
    QColor colour;
};

struct ApplyOptions
{
    std::optional<bool> darkMode;
    bool allowColorOnly = true; // reuse installed runtime for same-base color profiles
    bool properties = true;    // Properties launcher and desktop menu
    bool colours = true;       // Desktop colour scheme
    bool decoration = true;    // Window borders and rules
    bool fonts = true;         // XP font preferences and rendering
    bool widgets = true;       // Qt apps' widget style
    bool taskbar = true;       // the panel, the Start button, the tray
    bool icons = true;
    bool sounds = true;        // XP's sounds with the look, as sounds::wantedWithLook says
    std::optional<Wallpaper> wallpaper;
    QString themeFile;         // the .theme the look came from, if any (recorded)
    std::function<void(const QString &)> progress;
};

bool applyLook(Session &session, const Look &look, const ApplyOptions &options, QString *error = nullptr);

// State's settings: the release number (project.ini's Version) of the
// XPlasma that last applied the look. One older than this XPlasma's means
// the desktop wears an older XPlasma's make of it: Apply is wanted, and
// XPlasma Properties keeps it ready until it's done.
constexpr const char *kAppliedWithKey = "appliedWith";
bool lookOutdated(const State &state);
// What of the whole XP look this Plasma can't take (empty: all of it) --
// what applyLook checks before changing anything; and, `taskbar` asked
// for, the builds of Plasma's widgets it would use.
QStringList cannotTake(Session &session, bool taskbar = true, std::optional<forks::Choice> *chosen = nullptr,
                       bool properties = true, bool decoration = true);
// The desktop only: its wallpaper and colour, the look left as it is.
void applyWallpaper(Session &session, const Wallpaper &wallpaper, const std::function<void(const QString &)> &progress = {});
// XP's taskbar -- not floating, `height` px, opaque, the full width -- in
// the shell's saved state of each bottom panel, for writing while the shell
// is down: a running shell loads a panel's saved state a moment after its
// scripting answers, so a script run then is undone.
void settleTaskbars(Session &session, int height);
// The Start button only, redrawn for `look` (the one applied) with the
// user's extras (State's settings, kStartButtonKey) and handed to the
// running taskbar's Start buttons.
bool applyStartButton(Session &session, const Look &look, QString *error = nullptr);
// Qt apps' widgets only.
bool applyWidgets(Session &session, const Look &look, const std::function<void(const QString &)> &progress, QString *error = nullptr);
// Plasma's default look back (Breeze, the default panel and wallpaper), and
// everything XPlasma added gone.
bool resetDesktop(Session &session, const std::function<void(const QString &)> &progress, QString *error = nullptr);

// What a template script (src/app/scripts/NAME.js) says with its ${KEY}s
// filled in (values are JSON).
QString scriptFrom(const QString &name, const QVariantMap &values);

// The generated packages XPlasma (or xp2plasma) left under ~/.local/share,
// except those named in `keep`.
QStringList generatedPackages(const QStringList &keep = {});

} // namespace xpl
