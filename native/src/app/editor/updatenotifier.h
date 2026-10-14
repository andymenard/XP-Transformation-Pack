// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// xplasma-edit --check-updates: XP's Automatic Updates globe for XPlasma's
// own updates (updates.h). Run at login (its autostart entry); if a check is
// due and a newer XPlasma is out, the globe goes in the tray with XP's
// balloon, "Updates are ready for your computer. Click here to download
// these updates." Clicking either opens a message box: the release, its
// notes, Download or Remind Me Later, and a box to say no to that version
// for good. The globe's menu offers the same. Otherwise -- no URL,
// not due, nothing newer, no network -- it ends at once, silently.
#include <QString>

class QWidget;

namespace xpl::editor {

// `now`: check even if one was made today (for trying it out).
int checkUpdates(bool now);
// A check asked for (About's Check for Updates): at once, whatever was
// checked today or declined before, with a wait cursor; the box above if a
// newer XPlasma is out, else a word that this is the latest, or why the
// check failed.
void checkUpdatesNow(QWidget *parent);
// The window, drawn for a made-up release into DIR/updates.png (tests).
bool saveUpdateScreenshot(const QString &dir);

} // namespace xpl::editor
