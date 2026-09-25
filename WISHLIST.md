# Wishlist

What we'd like XPlasma to have, in one place. Add to it freely; move an item
to **Done** (with the date) when it lands. Bugs and measured differences from
XP go in NOTES.md instead; the plan for the big phases is PLAN.md.

## Doing

## Wanted

- **XPlasma's own update notifier** (from the user): a tray icon when a new
  XPlasma is out, as XP's Automatic Updates globe (wuauclt.exe's icon 1) --
  kept for it, not used for the system's updates.
- **Window sounds** (XP's Open program, Close program, Minimize, Maximize,
  Restore Up/Down, Menu popup): Plasma has no notification event for them.
  The taskbar's task model sees windows open, close, minimize and maximize;
  a sound played from there would reach the scheme's names.
- **Sounds and Audio Devices' other tabs**: Audio, Voice, Hardware.
- **Other sound schemes** (the user's idea: a small rock band recording a
  whole XP sound theme).
- **Flatpak's Qt 5 apps** (KDE runtime 5.15): the Qt 5 plugin as the same
  kind of extension, if its runtime's extension point takes it.
- **GTK 2 apps** (a pixmap-engine gtkrc), if any still matter.
- **A KDecoration plugin** on the same core, in place of Aurorae (optional).
- **Real rendering in the editor's previews** (`xplasma-render`).
- **Plasma 6.5 builds**, if a distribution still ships 6.5.
- **A Plasma 6.7 build for Qt 6.10**, if a distribution turns up with that pair.

## Not wanted

- **Pushing GTK further** (the user, 2026-09-25): no XP captions on
  libadwaita's or GTK's own title bars, no reshaping of libadwaita's rounded
  cards and spacing. GTK stays at XP's controls and colours: going further
  fights libadwaita's own design and comes out uglier.
- **Dark mode** (the user, 2026-09-24): XPlasma doesn't try to follow
  Plasma's dark mode or its switch; XP had none.

## Done

- XP's sounds on with the first look applied, as XP had them from the
  start; `xplasma sounds off` is remembered (2026-09-25).
- Sounds and Audio Devices Properties, its Volume tab: the default output's
  name, volume and mute (pactl, else wpctl), the volume icon in the taskbar
  (Plasma's tray); Advanced... opens Plasma's audio settings (2026-09-25).
- Sounds and Audio Devices Properties, its Sounds tab: XP's Windows Default
  and No Sounds schemes (XP's registry defaults, its WAVs from the media),
  the user's own (Save As...), each event's sound picked from WINDOWS\Media
  or a file, played with the play button; applied as Plasma's sound theme,
  with the log-in, log-out, lock and unlock sounds turned on. First opened,
  Windows Default with Apply ready (2026-09-25).
- The installer (the user's design): XPlasma-Setup, a Windows Installer
  wizard that's a self-extracting archive -- downloads XP, takes its files
  out, installs XPlasma and XP's pointers, launches XPlasma; run again,
  repairs or removes. No look chosen or applied in it; no XP assets in it
  (2026-09-25).
- GTK 3 and GTK 4 apps in the XP look, libadwaita apps' controls too:
  a theme generated from the look, switched to on apply (2026-09-24).
- The unchecked check box in Plasma's popups as XP's box, and hover on
  check boxes as XP's hot rim (2026-09-24).
- Flatpak's Qt 6 apps in the XP style: an org.kde.KStyle.XPlasma extension
  per KDE runtime, installed by apply; a new look reaches running apps
  (qBittorrent, Prism Launcher, Elisa, DeckTuner checked) (2026-09-24).
- Extras... in Display Properties (the user's idea): the Start button's
  label, and its logo -- XP's flag, characters (an emoji), or a picture.
  Applied alone it only redraws the button (2026-09-24).
- XP's tray icons for the rest (the user's picks): Automatic Updates'
  green and yellow shields for system updates, Power Options' monitor
  with a moon for brightness, and a charging battery that keeps its level
  with XP's plug in its corner (2026-09-24).
- Plasma's own widgets on the taskbar (its clock, the keyboard layout)
  in the taskbar's white text (2026-09-24).
- The last of Plasma's popup controls: spin boxes' buttons as XP's up-down
  on its side; scroll bars seen live (the clipboard's popup, Fedora, thumb
  and hover track). The unchecked check box stays a small button: see
  NOTES.md (2026-09-24).
- The taskbar's text white again (the Plasma theme declares its API
  version) (2026-09-24).
- XP's controls in Plasma's popups: buttons, radio buttons, text fields,
  lists, progress bars, sliders, scroll bars, tabs; switches as XP's check
  box; XP's arrows (2026-09-24).
- XP tooltips (yellow, black border) and XP-coloured tray popups (2026-09-24).
- XP's account pictures in the Start menu for users without one (2026-09-24).
- "Click here to begin" over Start (2026-09-24).
- Date and Time Properties, from the clock (2026-09-24).
- Start menu shortcuts dragged onto the desktop, with XP's arrow (2026-09-24).
- Blinking notification icons, each to its own beat (2026-09-24).
- Plasma 6.3 to 6.7 and six distributions, systemd or not (2026-09-24).
