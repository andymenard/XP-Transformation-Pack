Alpha 1.0 update 2 adds an optional **Crisp XP text** setting in XPlasma Properties → Appearance and fixes several desktop and Firefox regressions.

- Crisp text uses lou's MS Sans Serif reconstruction, credited with its separate CC BY-SA 3.0 license. Qt glyph rendering was checked at 100%, 125%, 150%, 175%, and 200% scaling; shapes can still differ across scale factors and application toolkits.
- IE6 Firefox no longer shows overlapping Bookmarks/Favorites labels when crisp text is enabled; replacement labels use the selected font. Website text is unchanged.
- Theme changes retain the Start-menu popup helper. The helper is built for the target computer and included in the native package.
- Tab labels retain their full glyphs, and KDE Display Settings spin boxes have enough vertical space for both smooth and crisp text.
- KDE's gesture effects remain available instead of being disabled by the XP behavior profile.

**Fresh install:** download the complete installer archive, extract it, and open Start-Installer.desktop. Desktop is selected by default; IE6 Firefox remains optional. XP applications, custom tray menus, and Codex add-ons are excluded.

**Existing Alpha 1.0 installation:** apply the signed update in XP Transformation Pack Update, then close XPlasma Properties and run **XP Alpha Update Setup** from the application menu. Setup installs Linux build dependencies if needed and builds the native fixes against your architecture and Qt version. Log out and back in afterward to reload native components. The separate setup backup supports `setup.py --undo`; updater rollback covers the downloaded files. Do not run setup as root.

The complete installer and OTA carry separate SHA-256 checks and signatures. The original release remains available. Original Microsoft assets retain their copyrights; reused code, fonts, and other artwork retain their separate licenses and are listed in CREDITS.md.

Validated locally on Fedora Asahi ARM64 with Plasma 6.7 and Qt 6.11. Other supported distribution adapters and Intel/AMD builds still need fresh-machine testing. This is an alpha prerelease.
