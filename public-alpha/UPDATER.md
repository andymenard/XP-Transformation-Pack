# XP Transformation Pack Update

The public alpha feed is https://github.com/andymenard/XP-Transformation-Pack .
Checks and downloads use anonymous HTTPS. No GitHub CLI or account is needed.
The alpha channel accepts complete signed prereleases such as 1.0.0-alpha.1;
stable clients exclude prereleases. The current updater completes supported native
setup in the same window after installing the signed files, with live build
output and administrator authentication when dependencies are needed. Close
XPlasma Properties before installing native updates; log out and back in afterward
to reload desktop components. An older update whose native stage is still pending
can be completed using **Finish pending native setup** in this same app.

The updater follows the desktop's light/dark application colors. **Roll back last
update** preflights and restores native setup snapshots together with downloaded
files, preserving later edits and the original uninstall backup. Failed builds
restore the update; incomplete recovery snapshots are reported for attention.
Build dependencies and reusable build caches are retained. If an old updater
updates itself, close and reopen it once to load the unified client before its
next update.

Release manifest signatures and payload hashes are verified using the bundled
public key. Never include the private signing key or credentials in source or
releases. Native binaries must match the receiving architecture and Qt minor.
Local edits block overwrites. Backups, interrupted-transaction recovery and
rollback preserve the original installer's uninstall snapshot.

Run install.py as your desktop user to bootstrap from a complete updater directory.
A public bootstrap explicitly selects this public feed. The installer tracks the
previous configuration for restoration. Existing private clients are unchanged
until the user installs this edition or deliberately changes their feed.

Publish reviewed manifests locally with publish.py and a signing key outside the
repository, then upload release.json, release.sig and payload.tar.gz. Public
source does not include the original XP assets. Review ASSET-DISTRIBUTION.md
before uploading asset-bearing installer or OTA archives.

Native setup metadata is part of the signed release manifest (`native_setup`,
with `script` and `backup`). Only the known desktop-update and alpha-update
Python helpers are supported. Their file inventory and every input hash are
verified before execution. Existing v0.3.1 and Alpha 2 manifests are recognized
without changing their published signatures. A helper inherits the update lock,
so another updater cannot begin recovery while the build is still running.
