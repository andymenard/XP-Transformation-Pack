# Prerelease update policy

OTA updates are excluded from prerelease installers. Use the complete installer
from [GitHub releases](https://github.com/andymenard/XP-Transformation-Pack/releases)
and follow its migration instructions. Properties opens that page directly and
does not launch an update client. Existing updater installations are not removed
or reconfigured by this change.

The historical updater source is retained in the repository for future work;
it is not shipped in the prerelease payload. Signed installer checksums remain
available independently of OTA.
