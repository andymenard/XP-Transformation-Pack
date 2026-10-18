# Non-free assets

Everything in this folder is **not** covered by the project licence (GPL).

These are Windows XP resources — images, icons, cursors, sounds, fonts and
visual styles — and files derived from them, such as the pixel fonts traced
from XP's Tahoma and icon themes built from XP's icons. They are Microsoft's
copyright (or derived from it), and Microsoft's trademarks where they show
its marks. They are in this repository, in this one folder, for convenience:
they are what the installer's payload ships, at the same paths under
`payload/`. Packaging marks the components that use them as non-free.

Windows program files (`.exe`, `.dll`, `.cpl`) and installers are not
included — only visual styles' resource-only `ShellStyle.dll` files, which hold
bitmaps and no code — nor are the high-resolution photo rescans, which belong
to their photographers, or third-party game skins. Where a
file here is a CC0 or otherwise licensed recreation rather than Microsoft's
own, its provenance file (`PROVENANCE.json`, `ICON-PROVENANCE.json`) says so.

`MANIFEST.json` lists every file with its checksum.

Rights holders: open an issue on this repository, and any of these files
will be removed.
