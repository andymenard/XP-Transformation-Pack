# XP maximized title-bar fix

On Plasma 6.6, Aurorae v2 can stretch the maximized XP caption into a flat
background. Reapplying XPlasma fixes it by selecting `org.kde.kwin.aurorae`
instead of `org.kde.kwin.aurorae.v2`. Both plugins ship with the distribution.

On your already-themed computer, update this repository and run as your desktop
user (without sudo):

```bash
git pull --ff-only
python3 tools/install-decoration-fix.py
```

The installer selects the compatible plugin now if KWin is available and adds a
login check. It also integrates with the existing XP session-maintenance helper
where present. The check changes only the plugin preference for generated XP
Aurorae themes currently using v2. Correct selections and other themes are left
alone. It does not regenerate artwork, reapply global appearance, switch through
Breeze, or restart Plasma. A changed selection asks KWin to reconfigure.

Existing helper files are backed up under
`~/.local/share/xplasma/maintenance/decoration-compatibility/`.

Verified on the affected computer: switching only to v2 reproduced the fault;
selecting the legacy plugin fixed it without changing theme files. Regression
checks cover the working plugin, other themes, and generated XP themes:

```bash
python3 -m unittest discover -s tools/tests -v
```

This installer is available directly from the repository. It is not yet part of
a signed OTA release; publishing an OTA payload requires the existing signing
key on the release computer.
