"""Generate a Plasma desktop theme (panel background, task buttons, colors)
from a parsed .msstyles' TaskBar / TaskBand classes.

Plasma theme package layout (missing files fall back to the "default" theme):
  <id>/
    metadata.json
    colors                          -- KColorScheme used by panel/popup text
    widgets/panel-background.svg    -- translucent/adaptive panels
    solid/widgets/panel-background.svg  -- opaque panels
    widgets/tasks.svg               -- task manager button backgrounds

FrameSvg element naming: <prefix><piece> with piece in
{topleft,top,topright,left,center,right,bottomleft,bottom,bottomright}
plus hint elements (hint-<side>-margin sizes give content padding,
hint-stretch-borders makes edges stretch instead of tile). A bottom panel
looks up the "south-" prefix first, then no prefix; task buttons use
"south-<state>-" then "<state>-".
"""
from __future__ import annotations

import json
import os

from PIL import Image

from aurorae_gen import _prestretch, _slice_strip, _solid, _svg_image_element, _wrap_svg
from msstyles_parse import MsStyle, parse_color, parse_margins

LOCATION_PREFIXES = ("south-", "")
PANEL_PADDING = {"left": 0, "right": 2, "top": 3, "bottom": 3}
# How far the Start applet is pulled out of the panel's left margin so an
# XP pill sits flush with the screen edge, as in XP.
START_OVERHANG_LEFT = PANEL_PADDING["left"] + 4
TASK_PADDING = {"left": 3, "right": 3, "top": 2, "bottom": 2}

# Plasma task state -> (msstyles class, part, frame index). The TaskBand
# toolbar button strip follows the standard toolbar state order:
# normal, hot, pressed, disabled, checked, hot-checked -- XP's active task
# is the "checked" (pressed-in, dark) frame.
TASK_STATES = {
    "normal": ("taskband::toolbar", "button", 0),
    "hover": ("taskband::toolbar", "button", 1),
    "focus": ("taskband::toolbar", "button", 4),
    "minimized": ("taskband::toolbar", "button", 0),
    "attention": ("taskband", "flashbutton", 0),
}


def _frames(theme: MsStyle, cls: str, part: str):
    eff = theme.effective(cls, part)
    if "imagefile" not in eff:
        return None, eff
    img = theme.get_background_bitmap(cls, part)
    n = int(eff.get("imagecount", 1))
    vertical = eff.get("imagelayout", "vertical").lower() == "vertical"
    return _slice_strip(img, n, vertical), eff


def _margins(eff: dict[str, str]) -> tuple[int, int, int, int]:
    return parse_margins(eff["sizingmargins"]) if "sizingmargins" in eff else (0, 0, 0, 0)


def _nine_slice(frame: Image.Image, l: int, r: int, t: int, b: int) -> dict[str, Image.Image]:
    w, h = frame.size
    if l + r >= w:  # keep at least a 1px stretchable centre
        l, r = max(0, (w - 1) // 2), max(0, w - 1 - (w - 1) // 2)
    if t + b >= h:
        t, b = max(0, (h - 1) // 2), max(0, h - 1 - (h - 1) // 2)
    c = frame.crop
    return {
        "topleft": c((0, 0, l, t)), "top": c((l, 0, w - r, t)), "topright": c((w - r, 0, w, t)),
        "left": c((0, t, l, h - b)), "center": c((l, t, w - r, h - b)), "right": c((w - r, t, w, h - b)),
        "bottomleft": c((0, h - b, l, h)), "bottom": c((l, h - b, w - r, h)), "bottomright": c((w - r, h - b, w, h)),
    }


def _emit_frame(elements: list[str], prefix: str, pieces: dict[str, Image.Image],
                padding: dict[str, int], stretch_horizontal: bool) -> None:
    for name, im in pieces.items():
        if im.width == 0 or im.height == 0:
            continue
        if stretch_horizontal and name in ("top", "center", "bottom"):
            im = _prestretch(im, "width", "stretch")
        elements.append(_svg_image_element(f"{prefix}{name}", im))
    for side, px in padding.items():
        size = (px, 1) if side in ("left", "right") else (1, px)
        elements.append(_svg_image_element(f"{prefix}hint-{side}-margin", _solid(*size, (0, 0, 0, 0))))
    elements.append(_svg_image_element(f"{prefix}hint-stretch-borders", _solid(1, 1, (0, 0, 0, 0))))


def build_panel_background(theme: MsStyle) -> tuple[str, int, tuple[int, int, int]]:
    """Returns (svg, panel_height, average_bg_rgb)."""
    frames, eff = _frames(theme, "taskbar", "backgroundbottom")
    if not frames:
        raise ValueError("theme has no [TaskBar.BackgroundBottom] image")
    frame = frames[0]
    l, r, t, b = _margins(eff)
    pieces = _nine_slice(frame, l, r, t, b)
    stretch = eff.get("sizingtype", "stretch").lower() == "stretch"

    elements: list[str] = []
    for prefix in LOCATION_PREFIXES:
        _emit_frame(elements, prefix, pieces, PANEL_PADDING, stretch)

    avg = frame.convert("RGB").resize((1, 1), Image.BOX).getpixel((0, 0))
    return _wrap_svg(elements, frame.width * 4, frame.height), frame.height, avg


def build_tasks_svg(theme: MsStyle, button_height: int) -> str | None:
    elements: list[str] = []
    for state, (cls, part, idx) in TASK_STATES.items():
        frames, eff = _frames(theme, cls, part)
        if not frames:
            continue
        frame = frames[min(idx, len(frames) - 1)]
        l, r, t, b = _margins(eff)
        # XP draws these buttons shorter than the art and squeezes the fixed
        # top/bottom caps proportionally; FrameSvg won't, so pre-squeeze.
        if frame.height > button_height:
            scale = button_height / frame.height
            frame = frame.resize((frame.width, button_height), Image.LANCZOS)
            t, b = round(t * scale), round(b * scale)
        pieces = _nine_slice(frame, l, r, t, b)
        for prefix in LOCATION_PREFIXES:
            _emit_frame(elements, f"{prefix}{state}-", pieces, TASK_PADDING, stretch_horizontal=False)
    if not elements:
        return None
    return _wrap_svg(elements, 64, button_height)


def build_tray_svg(theme: MsStyle) -> str | None:
    """Horizontal XP notification-area art, plus its real expand buttons."""
    frames, eff = _frames(theme, "traynotifyhoriz::traynotify", "background")
    if not frames:
        return None
    frame = frames[0]
    l, r, t, b = _margins(eff)
    elements = []
    padding = dict(left=0, right=0, top=0, bottom=0)
    # The left cap includes transparent artwork, not usable icon space.
    # Preserve XP's content inset even when there is no expander button.
    content_left = parse_margins(eff.get("contentmargins", "0, 0, 0, 0"))[0]
    _emit_frame(elements, "tray-", _nine_slice(frame, l, r, t, b),
                dict(padding, left=content_left), True)
    # The clock continues the tray's centre; don't repeat the left separator.
    middle = frame.crop((l, 0, max(l + 1, frame.width - r), frame.height))
    _emit_frame(elements, "clock-", _nine_slice(middle, 0, 0, t, b), padding, True)
    for prefix, cls in (("expand", "traynotifyhoriz::button"),
                        ("collapse", "traynotifyhorizopen::button")):
        buttons, _ = _frames(theme, cls, "")
        for index, state in enumerate(("normal", "hover", "pressed")):
            if buttons:
                elements.append(_svg_image_element(f"{prefix}-{state}", buttons[min(index, len(buttons)-1)]))
    return _wrap_svg(elements, frame.width * 4, frame.height)


def start_font(theme: MsStyle):
    """Use the theme's face, point size and style at XP's 96 DPI."""
    from PIL import ImageFont
    import hostexec as host
    fields = [v.strip() for v in theme.effective("start::button").get(
        "font", "Franklin Gothic Medium, 14, italic").split(",")]
    family = fields[0]
    pixels = round(int(fields[1]) * 96 / 72)
    flags = {v.lower() for v in fields[2:]}
    style = ("Bold " if "bold" in flags else "") + ("Italic" if "italic" in flags else "Regular")
    result = host.run(["fc-match", f"{family}:style={style}", "-f", "%{file}"], capture=True)
    path = result.stdout.strip()
    return ImageFont.truetype(path, pixels) if path and os.path.isfile(path) else ImageFont.load_default(size=pixels)


def build_colors(theme: MsStyle, name: str, panel_bg: tuple[int, int, int]) -> str:
    m = theme.effective("sysmetrics")

    def c(key: str, default: tuple[int, int, int]) -> str:
        r, g, b = parse_color(m[key]) if key in m else default
        return f"{r},{g},{b}"

    tb = theme.effective("taskbar::toolbar")
    text = parse_color(tb["textcolor"]) if "textcolor" in tb else (255, 255, 255)
    bg = f"{panel_bg[0]},{panel_bg[1]},{panel_bg[2]}"
    fg = f"{text[0]},{text[1]},{text[2]}"
    sections = []
    for group in ("Window", "Button", "View", "Header", "Complementary"):
        sections.append(
            f"[Colors:{group}]\nBackgroundNormal={bg}\nBackgroundAlternate={bg}\n"
            f"ForegroundNormal={fg}\nForegroundActive={fg}\nForegroundInactive={fg}\n"
            f"DecorationFocus={c('highlight', (51, 94, 168))}\nDecorationHover={c('highlight', (51, 94, 168))}\n"
        )
    sections.append(
        f"[Colors:Selection]\nBackgroundNormal={c('highlight', (51, 94, 168))}\n"
        f"ForegroundNormal={c('highlighttext', (255, 255, 255))}\n"
    )
    sections.append(
        f"[Colors:Tooltip]\nBackgroundNormal={c('infobackground', (255, 255, 225))}\n"
        f"ForegroundNormal={c('infotext', (0, 0, 0))}\n"
    )
    sections.append(f"[General]\nName={name}\nColorScheme={name}\n")
    return "\n".join(sections)


def _row_color(img: Image.Image, y: int) -> tuple[float, float, float]:
    px = [img.getpixel((x, y)) for x in range(img.width)]
    px = [p for p in px if p[3] > 30] or px
    return tuple(sum(p[i] for p in px) / len(px) for i in range(3))


def _dist(a, b) -> float:
    return sum(abs(a[i] - b[i]) for i in range(3))


def _trim_taskbar_bleed(theme: MsStyle, frame: Image.Image, top: int, bottom: int) -> tuple[int, int]:
    """Some Start button art bakes rows of the taskbar's own colour into its
    top/bottom edge (invisible on XP where they land on identical pixels,
    a visible seam on ours). Drop edge rows that look like the taskbar
    rather than like the button."""
    tb_frames, _eff = _frames(theme, "taskbar", "backgroundbottom")
    if not tb_frames:
        return top, bottom
    tb = tb_frames[0].convert("RGBA")
    tb_rows = [_row_color(tb, y) for y in range(tb.height)]
    span = bottom - top
    core_rows = [_row_color(frame, y) for y in range(top + span // 4, bottom - span // 4 + 1)]
    core = tuple(sum(r[i] for r in core_rows) / len(core_rows) for i in range(3))

    def taskbar_like(y: int) -> bool:
        alpha = [frame.getpixel((x, y))[3] for x in range(frame.width)]
        if sum(a > 200 for a in alpha) < len(alpha) // 2:
            return False  # a soft antialiased edge, not baked-in background
        row = _row_color(frame, y)
        d = min(_dist(row, c) for c in tb_rows)
        return d < 90 and d < _dist(row, core)

    # Bleed is only ever a couple of rows; never eat into a dark button on a
    # dark taskbar.
    for _ in range(3):
        if bottom > top + 8 and taskbar_like(bottom):
            bottom -= 1
    for _ in range(3):
        if top < bottom - 8 and taskbar_like(top):
            top += 1
    return top, bottom


START_STATES = (("normal", 0), ("hover", 1), ("pressed", 2))


def build_start_button_pngs(theme: MsStyle, out_dir: str, base_name: str, bar_height: int = 30) -> str | None:
    """Render the [Start::Button] pill in its normal / hot / pressed frames
    with the real XP flag (explorer.exe bitmap 143, when the install media
    is available) and a "start" label baked in. Returns the *normal* PNG
    path; the other states sit next to it as -hover.png / -pressed.png. The
    file names carry a content hash so image caches never show a stale pill.
    Classic-style themes ship the button as a tiny 9-slice; that gets
    stretched to bar height and a label-sized width like XP does."""
    import hashlib
    from PIL import ImageDraw, ImageFont
    from aurorae_gen import _nine_slice_resize
    from xp_assets import start_flag

    eff = theme.effective("start::button")
    if "imagefile" not in eff:
        return None
    img = theme.get_background_bitmap("start::button", "")
    n = int(eff.get("imagecount", 3))
    frames = _slice_strip(img, n, eff.get("imagelayout", "vertical").lower() == "vertical")
    content_left = parse_margins(eff["contentmargins"])[0] if "contentmargins" in eff else 10
    content_right = parse_margins(eff["contentmargins"])[1] if "contentmargins" in eff else 8
    text_rgb = parse_color(eff["textcolor"]) if "textcolor" in eff else (255, 255, 255)
    flag = start_flag()
    flag = flag.convert("RGBA") if flag is not None else None

    font = start_font(theme)

    # A negative left ContentMargin pushes XP's label (and flag) off the button
    # entirely: the art *is* the button -- an orb, a Windows 10 logo, a baked
    # "Start" graphic. Draw it as-is at its own size, never stretched.
    graphic_only = content_left < 0
    if graphic_only:
        flag = None
    stretchable = frames[0].width < 40 and not graphic_only
    if graphic_only:
        top, bottom = 0, frames[0].height - 1
    elif stretchable:
        # 9-slice art: size it like XP would -- bar height minus a 2px inset,
        # wide enough for margins + flag + label.
        target_h = max(12, bar_height - 4)
        label_w = round(font.getlength("start"))
        target_w = content_left + (flag.width + 3 if flag is not None else 0) + label_w + content_right
        margins = parse_margins(eff["sizingmargins"]) if "sizingmargins" in eff else (2, 2, 2, 2)
        frames = [_nine_slice_resize(f.convert("RGBA"), margins, target_w, target_h) for f in frames]
        top, bottom = 0, target_h - 1
    else:
        # Crop all frames to one common opaque row range: if the states
        # differed in height their aspect ratios would differ and the button
        # would re-fit (visibly shrink) whenever it changed state. The range is
        # the intersection, so no state keeps an empty edge row.
        top, bottom = None, None
        for _state, idx in START_STATES:
            fr = frames[min(idx, len(frames) - 1)].convert("RGBA")
            alpha = fr.getchannel("A")
            rows = [y for y in range(fr.height) if any(alpha.getpixel((x, y)) > 30 for x in range(fr.width))]
            if rows:
                # Rows *every* state fills: if one frame carried an extra
                # opaque row (Royale's pressed pill does), the union would leave
                # a transparent row under the others and they'd sit 1px high.
                top = rows[0] if top is None else max(top, rows[0])
                bottom = rows[-1] if bottom is None else min(bottom, rows[-1])
        if top is None:
            top, bottom = 0, frames[0].height - 1
        top, bottom = _trim_taskbar_bleed(theme, frames[0].convert("RGBA"), top, bottom)

    rendered: dict[str, Image.Image] = {}
    for state, idx in START_STATES:
        pill = frames[min(idx, len(frames) - 1)].convert("RGBA")
        full_h = pill.height
        pill = pill.crop((0, top, pill.width, bottom + 1))
        if not graphic_only and not stretchable and pill.height != bar_height and "sizingmargins" in eff:
            # XP stretches the pill to the taskbar's height through its sizing
            # margins (rims fixed, middle stretched) rather than letting it
            # stand taller than the bar -- a 31px Royale pill poked 1px above.
            _l, _r, mt, mb = parse_margins(eff["sizingmargins"])
            mt = max(0, mt - top)
            mb = max(0, mb - (full_h - 1 - bottom))
            if mt + mb < min(pill.height, bar_height):
                pill = _nine_slice_resize(pill, (0, 0, mt, mb), pill.width, bar_height)
        h = pill.height
        if graphic_only:
            if h > bar_height:  # an orb taller than the bar is scaled to fit
                pill = pill.resize((max(1, pill.width * bar_height // h), bar_height), Image.LANCZOS)
            padded = Image.new("RGBA", (pill.width, bar_height), (0, 0, 0, 0))
            padded.alpha_composite(pill, (0, (bar_height - pill.height) // 2))
            rendered[state] = padded
            continue
        if flag is not None:
            pill.alpha_composite(flag, (max(0, content_left - 3), max(0, (h - flag.height) // 2)))
            text_x = content_left - 3 + flag.width + 3
        else:
            text_x = content_left + 2
        d = ImageDraw.Draw(pill)
        bbox = font.getbbox("start")
        ty = (h - (bbox[3] - bbox[1])) // 2 - bbox[1]
        light_text = sum(text_rgb) > 384
        if light_text:
            d.text((text_x + 1, ty + 1), "start", font=font, fill=(0, 60, 0, 160))
        d.text((text_x, ty), "start", font=font, fill=text_rgb + (255,))
        if stretchable:
            # XP insets the Classic button 2px inside the bar; keep the PNG at
            # full bar height so the launcher's overhang math still holds.
            padded = Image.new("RGBA", (pill.width, bar_height), (0, 0, 0, 0))
            padded.alpha_composite(pill, (0, (bar_height - pill.height) // 2))
            pill = padded
        rendered[state] = pill

    os.makedirs(out_dir, exist_ok=True)
    digest = hashlib.sha1(b"".join(im.tobytes() for im in rendered.values())).hexdigest()[:8]
    paths = {}
    for state, im in rendered.items():
        paths[state] = os.path.join(out_dir, f"{base_name}-{digest}-{state}.png")
        im.save(paths[state])
    return paths["normal"]


XP_TASKBAR_HEIGHT = 30   # stock XP taskbar at 96 DPI
MIN_TASKBAR_ART_HEIGHT = 16  # anything shorter is a stretch-strip, not a real height
MAX_TASKBAR_ART_HEIGHT = 60  # anything taller is an undeclared multi-frame strip


def generate_plasma_theme(theme: MsStyle, out_dir: str, name: str,
                          panel_height: int | None = None) -> tuple[str, int]:
    """Writes the package under out_dir/<id>; returns (pkg_dir, panel_height)."""
    plugin_id = "".join(ch if ch.isalnum() else "" for ch in name).lower() + "-panel" or "xppanel"
    pkg = os.path.join(out_dir, plugin_id)
    for sub in ("widgets", os.path.join("solid", "widgets")):
        os.makedirs(os.path.join(pkg, sub), exist_ok=True)

    panel_svg, art_height, avg = build_panel_background(theme)
    for sub in ("widgets", os.path.join("solid", "widgets")):
        with open(os.path.join(pkg, sub, "panel-background.svg"), "w", encoding="utf-8") as f:
            f.write(panel_svg)

    if panel_height is None:
        # A bitmap strip is not the finished panel height: Luna's is 28px,
        # but XP renders a 30px bar. Keep taller theme art and explicit overrides.
        panel_height = max(XP_TASKBAR_HEIGHT, art_height) if MIN_TASKBAR_ART_HEIGHT <= art_height <= MAX_TASKBAR_ART_HEIGHT else XP_TASKBAR_HEIGHT
    button_height = max(8, panel_height - PANEL_PADDING["top"] - PANEL_PADDING["bottom"])
    tasks_svg = build_tasks_svg(theme, button_height)
    tray_svg = build_tray_svg(theme)
    if tray_svg:
        with open(os.path.join(pkg, "widgets", "xp-tray.svg"), "w", encoding="utf-8") as f:
            f.write(tray_svg)
    if tasks_svg:
        with open(os.path.join(pkg, "widgets", "tasks.svg"), "w", encoding="utf-8") as f:
            f.write(tasks_svg)

    with open(os.path.join(pkg, "colors"), "w", encoding="utf-8") as f:
        f.write(build_colors(theme, name, avg))

    meta = {
        "KPlugin": {
            "Id": plugin_id,
            "Name": f"{name} (XP taskbar)",
            "Description": f"Panel theme ported from the Windows XP visual style \"{name}\" by XPLasma",
            "Authors": [{"Name": "XPLasma"}],
            "Version": "1.0",
            "License": "Unknown",
        },
        "X-Plasma-API": "5.0",
    }
    with open(os.path.join(pkg, "metadata.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)
    return pkg, panel_height
