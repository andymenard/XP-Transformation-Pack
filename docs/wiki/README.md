# Tutorial sources

These Markdown files mirror the GitHub Wiki and remain reviewable in the main repository. Start with [Home](Home.md).

To publish, copy the tutorial pages to the separate `XPlasma-Plus.wiki.git` checkout and change local `Page.md` links to `Page` links. Images use absolute URLs to `docs/screenshots` in the main repository. `_Sidebar.md` and `_Footer.md` are Wiki navigation, maintained alongside the pages.

Installer screenshots are generated using Setup's non-mutating `--preview` mode. The lock screen uses KDE testing mode; the boot image is a rendered preview, as its caption states. No real installation, lock or reboot is needed to regenerate these documentation images.
