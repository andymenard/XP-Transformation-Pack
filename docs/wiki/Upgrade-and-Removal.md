# Upgrade, rebuild or remove

Back up your data and configuration, save work and close other programs first. Keep the extracted full installer accessible. Run it as your normal desktop user.

## Upgrade to a newer release

1. Read the new release's compatibility and migration notes.
2. Download and verify the appropriate **complete installer**, then extract it to its own folder.
3. Open its **Start-Installer.desktop** (or run `sh ./install.sh` from that folder).
4. Choose **Upgrade installation**. Installed components stay selected; add supported components if desired.
5. Wait for success, then close/reopen apps and log out and back in. Reboot to test changed boot components.

Upgrade keeps the original rollback backup, selected features, current theme and relevant preferences. An existing component cannot be dropped simply by clearing its checkbox during an upgrade; remove the managed installation first if you need to change that set. This public beta has **no OTA/background update client**. **Download New Releases** in Properties opens GitHub releases.

## After a Plasma or KWin update

A KDE update can invalidate native widgets or effects. The installer checks versions rather than loading an arbitrary old binary. If the XP taskbar is paused and Setup offers **Rebuild for Plasma …**, use it to build against the supported installed KDE/Qt version; theme/settings should be retained. Read the operation output, then log out/in to refresh loaded code.

Rebuild is not an upgrade to support a new Plasma series: **6.8 is not supported by 1.0.1's native integrations**. Do not edit version guards to force old widgets to load. Use a compatible release or wait for a port.

## Remove the managed installation

1. Open Setup from the extracted installer.
2. Click **Remove installed features**.
3. Read the confirmation. By default, leave **Also delete the settings backups** unchecked so recovery records remain available.
4. Confirm and wait for removal/recovery to finish. Complete local administrator prompts for selected system components.
5. Reopen apps, then log out/in; reboot when verifying restored boot configuration.

Removal restores settings from before installation and removes managed generated looks. Later edits are preserved when possible, but the original backup is not a guarantee of recovering every unrelated desktop change. Saved themes, custom colors, the IE6 browser profile and Linux dependencies are retained. The optional backup-deletion checkbox removes recovery data and should not be selected casually.

Do not delete `~/.local/share/kde-xp-suite` or generated theme folders as a substitute for the remover: the installation record and backups are used for managed restoration. If removal fails, keep the output and [report the problem](Troubleshooting.md).
