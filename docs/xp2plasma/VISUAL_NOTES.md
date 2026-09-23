# Visual follow-ups

## Installer

Restore a custom-ISO picker in the installer later (user request September 19,
2026). The shared media importer already accepts a supplied ISO or extracted
media directory, and discovers all theme presets rather than only Luna.
This does not mean automatically downloading third-party theme collections.

## Indeterminate progress bars

User reference, September 19, 2026: the moving block should be **five bars
wide**, advancing **one bar per step** rather than sliding continuously.
Implement later; do not mix into the current Start menu layout work.

## Desktop tab

Implemented 2026-09-20, monitor included. **Customize Desktop...** is disabled:
it opens XP's Desktop Items dialog (My Documents, My Computer, My Network
Places, Internet Explorer), which is a separate dialog and a separate piece of
work. Worth deciding whether it is wanted at all — the honest-Plasma-names rule
from the Start menu applies, so it would show real Plasma destinations rather
than relabelling Linux apps as Windows equivalents.

Picking a wallpaper and pressing Apply is deliberately cheap: no package
regeneration, no plasmashell restart.

## Start menu

Design direction: preserve XP's visual appearance, but name destinations
honestly for Plasma (System Settings, Default Applications, Printers, etc.).
Do not relabel Linux applications as Windows equivalents.

Measurements and remaining assumptions are recorded in
`prototypes/start_menu/README.md`. The live launcher now shares the renderer
with that isolated preview; KDE models/actions are in `start-menu/XpBackend.qml`.
All Programs uses cascading KDE category folders with delayed hover opening.
The fixed-size menu fits eight program rows; recents fill at most
`max(0, 8 - pinned)` slots. Extra pinned apps scroll. XP's more nuanced dynamic
menu growth is explicitly deferred at the user's request (September 19, 2026).
