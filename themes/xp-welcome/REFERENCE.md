# Welcome typography reference

Checked against the user-supplied Windows XP SP3 ISO, not inferred from a video frame.
`logonui.exe`, UIFILE 1000 (English 1033), `leftpanelss` defines:
- `rcstr(1)`: Arial (inherited font face).
- `rcstr(7)`: lowercase `welcome`.
- `rcint(44)`: 36 points, bold italic (48 logical pixels at 96 DPI).
- Foreground RGB 239,247,255; shadow RGB 49,81,181 offset 2,3.

The optional local font payload uses the ISO's `I386/ARIALBI.TT_`, expanded to
`arialbi.ttf`. SHA-256: `722c61a99c1af1413d762d0a3b185dd497fe55b873c8672a0c3c4bfe05d29d92`.
The font retains Microsoft's copyright and is not included in public source.
Qt scales the logical text size for HiDPI rather than tying it to screen resolution.

Setup text families were also checked in ISO `I386/WINNTBBU.DLL`: STRING IDs
4 and 19 are Tahoma, ID 5 is Arial. Installer informational headings use Arial
and the sidebar/body/controls use Tahoma. Project branding is separate.
