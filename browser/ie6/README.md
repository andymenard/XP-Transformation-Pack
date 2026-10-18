# IE6 Firefox refinement sources

Use these files with the dedicated Firefox ESR installation created by the
transformation installer. They are chrome/autoconfig sources, not a Firefox
engine distribution. Close Firefox before copying chrome files into its
profile/chrome and runtime/ie6-ui.cfg into its dedicated Firefox runtime.
Existing profile settings, history, bookmarks and sessions should be preserved.

Included toolbar/bookmark PNGs are marchmountain's CC0 XP recreations. Keep
LICENSE-CC0-icons and ICON-PROVENANCE.json with redistributed copies. The four
SVG controls are original project artwork under GPL-3.0-or-later.

The CSS also references legacy assets already supplied by the installer:
page.png, downloads.png and the Microsoft idle/loading Windows flag artwork.
Those original Microsoft assets are not relicensed or stored in this repository.
Generate the genuine 50-frame waving flag from your own XP shell32.dll using:

    python3 tools/extract-xp-ie-flag.py --shell32 /path/to/shell32.dll \
      --output /path/to/profile/chrome/ie6-icons

This requires Pillow. It packages the original complete frames without warping
or blending them. See THROBBER-PROVENANCE.json for the resource identity.

Changes include a compact opaque white address field, square field/popup/row/
action-button corners, an XP-style plus, classic bookmark and overflow controls,
XP result icons that preserve actual website favicons, and loading animation
that follows the selected tab. The initial-prefs.js file seeds the original toolbar layout and bookmark bar
visibility in prefs.js only for a fresh profile. Keep these choices out of user.js,
which Firefox reapplies on every launch. Existing prefs.js files must be preserved.

The runtime enables Firefox's actual Menu Bar toggle on its first themed launch
using the persisted toolbar autohide setting. The xplasma.ie6.menuBarInitialized
marker prevents later launches from resetting the user's menu-bar choice.

XPlasma's **Dark application colors** preference also controls the IE6 browser
chrome. The autoconfig reads the small appearance flag every 1.5 seconds and
sets a root attribute; dark-mode.css changes only colors and beveled shading.
Toolbar choices and site content are preserved. Restart the browser once
after installing this new chrome/config; later toggles update live.

Dark colors cover the address selector's shadow-tree button, focused address
field and suggestions, tab/new-tab bevels, menu states and monochrome glyphs.
The original XP flag retains its frames and colors; flag-canvas.svg removes
only its white bitmap canvas when dark mode is active. Light mode keeps the
original canvas. Colorful toolbar icons and website favicons are unaffected.

Static replacement artwork uses `ie6-icons/sized/*.svg`: each wrapper embeds
the unchanged high-resolution PNG with a 16 CSS-pixel intrinsic size. This
prevents Library buttons, password prompts and other unsized controls from
expanding to the PNG's 64/96-pixel source size. Firefox's explicit control sizes
still take precedence, including the 24-pixel main toolbar and larger notices.
The original PNGs remain available for application branding and asset tools;
the animated Windows flag and real website/extension icons are unchanged.
Run `python3 browser/ie6/build-icon-wrappers.py` after editing PNG artwork.
Installer payload synchronization checks that these wrappers are current.
