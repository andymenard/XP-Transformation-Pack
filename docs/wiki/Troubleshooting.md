# Troubleshooting and reporting bugs

This is a public beta. Back up before installing and keep recovery records. If an operation fails, preserve the output before trying another installation.

## Setup fails or appears idle

Check the output panel below the left progress bar. Local native compilation and dependency installation can take time. Look for an administrator prompt behind other windows. Do not close Setup while it is installing or rolling back. If it reports failure, copy the final error **and the preceding context**, including any recovery result.

To save output on a future attempt, open a terminal in the extracted installer folder and use:

```sh
sh ./install.sh 2>&1 | tee xplasma-setup.log
```

This starts the GUI normally; perform only the operation you intended. Logs can contain usernames, paths, hostnames or other private information. Review/redact them before posting publicly.

## No Start button, wrong taskbar or unlabeled tasks

After a successful install, save work and **log out and back in once**. Restarting an app alone does not refresh every native Plasma widget. If it remains wrong, check whether Setup disabled taskbar support for your Plasma version. Report the installer output; repeatedly killing `plasmashell` is not a substitute for fixing an installation.

A planned Plasma restart during installation can be expected; a **crash** is not. If the desktop crashes, include the Crash Handler details or relevant logs. Do not assume a crash is harmless just because the desktop recovers.

## Black or slow screen after login

Note whether the problem clears after a wait, after switching virtual terminals, or only after reboot. If comfortable, use Ctrl+Alt+F3 to reach a text console and log in. For diagnostics only, run:

```sh
loginctl list-sessions
journalctl --user -b --no-pager -n 200
```

Do not assume the graphical session is always on F4 or F5, and do not log out other users' sessions as a troubleshooting shortcut. Review logs for private data before sharing. Record whether this followed a fresh install, removal/reinstall or an ordinary login.

## Boot splash is black, flickers or shows text

First finish all boot-image rebuild prompts and confirm Setup succeeded. Check the splash feature's availability explanation. If comfortable, inspect (do not change) the current boot arguments with `cat /proc/cmdline`.

See the repository's [Boot splash troubleshooting guide](https://github.com/andymenard/XPlasma-Plus/blob/main/BOOT-SPLASH-TROUBLESHOOTING.md) for a confirmed Intel `i915` early-loading workaround on one Kubuntu/docked-laptop setup. It is specific to initramfs-tools and that driver; do not apply it to Asahi, dracut or another GPU as a generic fix.

## Fonts, arrows or icons look wrong

Record your theme, custom colors, light/dark mode, **Crisp XP text** choice and display scale. Try disabling experimental Crisp XP text or returning to stock Luna, then reopen the affected application. For oversized Firefox icons, identify the exact panel (such as Library or password save), not just “the browser.” Standard and HiDPI icon modes have the same mapped name coverage, but third-party applications/site favicons can use their own images.

## Sounds or dark mode did not apply

Check Properties' **Sounds** or **Appearance** settings, apply the desired choice and reopen affected applications. Confirm you are using the managed IE6 Firefox launcher when testing its dark mode. If a fresh install needs settings toggled twice, report it rather than treating that as the expected installation process.

## What to include in an issue

Open an [issue](https://github.com/andymenard/XPlasma-Plus/issues) with:

- Release number and exact installer filename.
- Distribution/release, architecture, Plasma/Qt/KWin versions, Wayland/X11 and display scale.
- Selected features, theme, custom colors, dark-mode and Crisp XP text choices.
- Fresh install or upgrade; exact steps, expected result and actual result.
- A screenshot and relevant setup/crash log excerpt, with private data redacted.

Useful read-only commands (some may not be installed):

```sh
cat /etc/os-release
uname -m
plasmashell --version
kwin_wayland --version
```

Use `kwin_x11 --version` instead if that is your session. Never post passwords, GitHub tokens, browser profiles, private keys or unreviewed full logs.
