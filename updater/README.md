# XP Transformation Pack Update

The public alpha feed is https://github.com/andymenard/XP-Transformation-Pack .
Checks and downloads use anonymous HTTPS. No GitHub CLI or account is needed.
The alpha channel accepts complete signed prereleases such as 1.0.0-alpha.1;
stable clients exclude prereleases. Until one is published, no update is offered.

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
