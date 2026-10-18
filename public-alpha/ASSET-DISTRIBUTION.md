# Asset distribution boundary

The public repository keeps Windows XP media — images, icons, cursors, sounds, fonts, visual styles and files derived from them — in one folder, `non-free/`, marked as Microsoft's copyright and not covered by the project licence ([non-free/NOTICE.md](non-free/NOTICE.md)); the installer marks the components that use them as non-free. Windows program files and installers, third-party game skins and supplied stock photographs (including the high-resolution rescans) are excluded. Code licenses and credits do not grant redistribution permission for these assets. Rights holders can open an issue on the repository to have any of them removed.

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
carries the Microsoft media in `non-free/` only. The complete installer preserves
component notices, provenance and credits, including the original XP avatars.

## Tahoma OS interface fonts

The publisher includes the renamed Microsoft Tahoma regular and bold faces in
`installer/assets/fonts/tahoma-os/`. This is an explicit exception to the source
export's font exclusion. Their original embedded copyright and license records
remain, and they are not relicensed under the project GPL. The generator and
source/derived hashes accompany the faces.
