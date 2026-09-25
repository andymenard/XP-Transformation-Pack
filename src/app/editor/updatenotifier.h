#pragma once
// xplasma-edit --check-updates: XP's Automatic Updates globe for XPlasma's
// own updates (updates.h). Run at login (its autostart entry); if a check is
// due and a newer XPlasma is out, the globe goes in the tray with XP's
// balloon, "Updates are ready for your computer. Click here to download
// these updates." Clicking opens the release's page; its menu can also put
// it off till next time, or say no to that version for good. Otherwise --
// no URL, not due, nothing newer, no network -- it ends at once, silently.
namespace xpl::editor {

// `now`: check even if one was made today (for trying it out).
int checkUpdates(bool now);

} // namespace xpl::editor
