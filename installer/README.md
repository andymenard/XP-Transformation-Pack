# Alpha installer

The public edition includes Desktop by default, optional IE6 Firefox, and signed
public update support. XP applications, custom tray menus, Codex colors and
Clippy are excluded. See the root README and CREDITS for scope and attribution.

This source checkout alone is not the complete asset-bearing installer. Local
test bundles include user-supplied resources; see ASSET-DISTRIBUTION.md before
redistributing them. In a complete local bundle, launch Start-Installer.desktop
or install.sh as the desktop user. The GUI permits individual feature selection,
icon/wallpaper resolution choices and dark application colors. It never logs out
or reboots automatically. System components and dependencies request authentication.

Native components build against the receiving machine's KDE/Qt. Desktop features
target Plasma 6 on supported Fedora, Debian/Ubuntu, Arch and openSUSE families.
Unknown distributions can use resource-only features when tools already exist.
Allow 5 GB for native builds or 2 GB for resource-only installation. Firefox uses a
separate ESR profile; existing browsing data and toolbar preferences are preserved.

Login uses SDDM on systemd. Boot requires existing splash-enabled Plymouth with
dracut or update-initramfs; mkinitcpio-only boot configurations are not supported.
The Welcome splash is separate from login and boot screens and appears as Plasma
starts. Remove an older overlapping installer before switching installation managers.

Updates use andymenard/XP-Transformation-Pack anonymously; GitHub CLI and a GitHub
account are not required. Installer backups include the updater feed configuration.
Selecting public update support explicitly switches an existing private update feed
in this new installation; it does not change clients on other computers.
