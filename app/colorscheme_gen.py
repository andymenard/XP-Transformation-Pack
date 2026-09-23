"""Generate a Plasma color scheme (.colors, KConfig INI format) from a
parsed .msstyles [SysMetrics] section."""
from __future__ import annotations

from msstyles_parse import MsStyle, parse_color


def _hex(rgb: tuple[int, int, int]) -> str:
    return f"{rgb[0]},{rgb[1]},{rgb[2]}"


def build_colorscheme(theme: MsStyle, name: str) -> str:
    m = theme.effective("sysmetrics")

    def c(key: str, default: tuple[int, int, int]) -> tuple[int, int, int]:
        return parse_color(m[key]) if key in m else default

    # KDE's [Colors:Window] is the dialog/window *face* -- XP's ButtonFace --
    # while XP's Window colour is the document area, KDE's [Colors:View].
    # Mapping Window->Window painted every dialog under a visual style white.
    # classic_gen already maps it this way.
    window_bg = c("window", (255, 255, 255))
    window_fg = c("windowtext", (0, 0, 0))
    button_bg = c("btnface", (238, 238, 238))
    button_fg = c("btntext", window_fg)
    selection_bg = c("highlight", (51, 94, 168))
    selection_fg = c("highlighttext", (255, 255, 255))
    active_titlebar = c("activecaption", selection_bg)
    active_titlebar_text = c("captiontext", (255, 255, 255))
    inactive_titlebar = c("inactivecaption", (200, 200, 200))
    inactive_titlebar_text = c("inactivecaptiontext", (255, 255, 255))
    disabled_fg = c("graytext", (128, 128, 128))
    tooltip_bg = c("infobackground", (255, 255, 225))
    tooltip_fg = c("infotext", (0, 0, 0))

    return f"""[ColorEffects:Disabled]
Color=239,239,239
ColorAmount=0
ColorEffect=0
ContrastAmount=0.65
ContrastEffect=1
IntensityAmount=0.1
IntensityEffect=2

[ColorEffects:Inactive]
ChangeSelectionColor=true
Color=112,111,110
ColorAmount=0.025
ColorEffect=2
ContrastAmount=0.1
ContrastEffect=2
Enable=false
IntensityAmount=0
IntensityEffect=0

[Colors:Button]
BackgroundNormal={_hex(button_bg)}
ForegroundNormal={_hex(button_fg)}
ForegroundInactive={_hex(disabled_fg)}
DecorationFocus={_hex(selection_bg)}
DecorationHover={_hex(selection_bg)}

[Colors:Selection]
BackgroundNormal={_hex(selection_bg)}
ForegroundNormal={_hex(selection_fg)}
DecorationFocus={_hex(selection_bg)}
DecorationHover={_hex(selection_bg)}

[Colors:Tooltip]
BackgroundNormal={_hex(tooltip_bg)}
ForegroundNormal={_hex(tooltip_fg)}

[Colors:View]
BackgroundNormal={_hex(window_bg)}
ForegroundNormal={_hex(window_fg)}
ForegroundInactive={_hex(disabled_fg)}
DecorationFocus={_hex(selection_bg)}
DecorationHover={_hex(selection_bg)}

[Colors:Window]
BackgroundNormal={_hex(button_bg)}
ForegroundNormal={_hex(button_fg)}
ForegroundInactive={_hex(disabled_fg)}
DecorationFocus={_hex(selection_bg)}
DecorationHover={_hex(selection_bg)}

[Colors:Header]
BackgroundNormal={_hex(button_bg)}
ForegroundNormal={_hex(button_fg)}
ForegroundInactive={_hex(disabled_fg)}
DecorationFocus={_hex(selection_bg)}
DecorationHover={_hex(selection_bg)}

[Colors:Complementary]
BackgroundNormal={_hex(button_bg)}
ForegroundNormal={_hex(button_fg)}
ForegroundInactive={_hex(disabled_fg)}
DecorationFocus={_hex(selection_bg)}
DecorationHover={_hex(selection_bg)}

[General]
ColorScheme={name}
Name={name}
shadeSortColumn=true

[KDE]
contrast=4

[WM]
activeBackground={_hex(active_titlebar)}
activeForeground={_hex(active_titlebar_text)}
inactiveBackground={_hex(inactive_titlebar)}
inactiveForeground={_hex(inactive_titlebar_text)}
"""
