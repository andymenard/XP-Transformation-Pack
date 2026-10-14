# Local shared cursor fallback

KDE, GTK 3/4 and GSettings use the desktop's logical cursor size. Native Wayland
clients/compositor handle output scaling; do not force a physical pixel size
into those shared preferences. The installed XP theme has 32/48/64/96 images.

Some clients also search the generic `default` theme through the legacy Xcursor
path. The sync helper gives that path the same theme selected in KDE and keeps
it aligned after pointer/preset changes. It does not contain application rules,
change display scale, or override an application's environment.

The local user path unit watches kcminputrc and runs the helper. Log out and back
in to reload applications which cached cursor images/settings before the theme
changed. Visual confirmation is still required: Chromium's X11 cursor loader can
request unscaled GTK sizes, which is an application/backend issue rather than a
missing HiDPI image. This helper does not claim to patch Chromium itself.

The Desktop installer now bundles this helper and its path/service units when
Pointers is selected. Existing unmanaged fallback themes are preserved; a
conflict skips this optional synchronization rather than aborting installation.
Removal restores the saved files and previously enabled watcher state.
