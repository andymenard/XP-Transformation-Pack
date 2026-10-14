# Asset distribution boundary

Public source exports exclude the proprietary Windows resources and supplied stock photographs. Code licenses and credits do not grant redistribution permission for these assets.

The local test installer contains assets supplied from the user's XP ISO and wallpaper collection. Its blue Setup reference resources were extracted from `I386/WINNTBBU.DLL`: background bitmap 103 and stage indicators 104/105/109; those extraction inputs stay private. The source GUI independently draws the layout and colors. The Welcome splash is an independent QML implementation referencing the logonui.exe layout and colors, without embedding the original Windows logo.

CC0 icon recreations, appropriately licensed KDE-derived glyphs, credited Tux/KDE logos, and original code retain their own terms. The Firefox source excludes the genuine Microsoft flag frames and other original artwork supplied only with the private/local bundle.

Before publishing any full installer or OTA assets containing proprietary resources, review their redistribution permissions or replace them with appropriately licensed assets. The current source publication does not assert that those permissions have been obtained.

References: https://www.microsoft.com/en-us/legal/intellectualproperty/copyright/permissions and https://www.gettyimages.com/eula . See CREDITS.md for known photographer metadata and unresolved entries.

## Alpha 1.0 installer release

The publisher has explicitly chosen to include their supplied original XP
assets in the binary installer release. They remain separate from the code
license: Microsoft and other original creators retain their copyrights. This
records the publisher's distribution choice; it does not claim that GPL or
attribution grants permission for third-party media. The source repository
continues to exclude the original assets. The complete installer preserves
component notices, provenance and credits, including the original XP avatars.
