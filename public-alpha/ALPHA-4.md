Alpha 1.0 update 4 brings XP-rendered pixel Tahoma, fixes the Log Off dialog's hover artwork and includes the desktop fixes from the Kubuntu prereleases.

- XP-rendered pixel Tahoma with designs for fractional scaling, also used by desktop widgets.
- Hovering **Log Off** no longer shifts the key icon; Windows' original hover bitmap was cut short and is now repaired.
- Optional sharp Start button on HiDPI screens. Each skin keeps its own Close button artwork under custom colors.
- Dark mode keeps XP's warm accents. Dark controls, sound activation and session-screen layouts are fixed.
- The caption animation effect is updated.

**Fresh install:** download the complete installer archive, extract it, and open Start-Installer.desktop. Desktop is selected by default; IE6 Firefox remains optional. XP applications, custom tray menus, and Codex add-ons are excluded.

**Existing installation:** close XPlasma Properties, then install through the current XPlasma Update client. Native setup runs in the same window with live output and a coordinated rollback backup. Kubuntu 26.04 Intel/AMD systems reuse the bundled, verified binaries and skip compilers and development packages; other systems build the native components locally. Use Finish pending native setup for an older applied update. If the updater updates itself, reopen it once to load the new client. Log out/in after native changes.

The boot screen change in this release needs a fresh install of the boot screen component; the update does not change system boot files.

The complete installer and OTA carry separate SHA-256 checks and signatures. Original Microsoft assets retain their copyrights; reused code, fonts, and other artwork retain their separate licenses and are listed in CREDITS.md. This is an alpha prerelease.

[Full credits and upstream projects](https://github.com/andymenard/XPlasma-Plus/blob/main/CREDITS.md), including [hotsocket’s original XPlasma](https://tangled.org/hotsocket.fyi/xplasma/).
