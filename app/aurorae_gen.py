"""Generate a KWin Aurorae SVG theme package from a parsed .msstyles WINDOW class.

Aurorae theme format (per https://develop.kde.org/docs/plasma/aurorae/):
  ThemeName/
    metadata.desktop
    ThemeNamerc
    decoration.svg   -- elements named <prefix>-<side>, side in
                         {left,right,top,bottom,topleft,topright,
                          bottomleft,bottomright,center}, prefix in
                         {decoration, decoration-inactive, decoration-maximized, ...}
    close.svg, minimize.svg, maximize.svg, restore.svg
                     -- elements named <state>-center, state in
                        {active, inactive, hover, hover-inactive,
                         pressed, pressed-inactive, deactivated, deactivated-inactive}

We embed each cropped/composited PNG as a base64 data-URI <image> tagged with
the required element id -- KSvg resolves each id independently by its own
bounding box, so exact global layout inside the SVG canvas doesn't matter.
"""
from __future__ import annotations

import base64
import io
import json
import os
from xml.sax.saxutils import escape

from PIL import Image

from msstyles_parse import MsStyle, parse_color, parse_margins

# Caption/frame parts use ImageCount=2, vertical layout: [active, inactive].
ACTIVE, INACTIVE = 0, 1

# Caption buttons use ImageCount=8: [normal,hot,pushed,disabled] x [active,inactive].
BUTTON_STATE_INDEX = {
    "active": 0,
    "hover": 1,
    "pressed": 2,
    "deactivated": 3,
    "inactive": 4,
    "hover-inactive": 5,
    "pressed-inactive": 6,
    "deactivated-inactive": 7,
}

AURORAE_BUTTON_NAMES = {
    "closebutton": "close",
    "minbutton": "minimize",
    "maxbutton": "maximize",
    "restorebutton": "restore",
}


def _slice_strip(img: Image.Image, count: int, vertical: bool) -> list[Image.Image]:
    if vertical:
        fh = img.height // count
        return [img.crop((0, i * fh, img.width, (i + 1) * fh)) for i in range(count)]
    fw = img.width // count
    return [img.crop((i * fw, 0, (i + 1) * fw, img.height)) for i in range(count)]


def _glyph_alpha_from_corner(img: Image.Image) -> Image.Image:
    """Classic-era glyph sheets use a flat colour-key background rather than
    real alpha (bare BMPs decode with alpha forced to 255) -- but different
    theme authors pick different key colours (white, magenta, ...). Sample
    the frame's own corner pixel (never covered by the small centred glyph
    mark) as the key, and derive per-pixel alpha from distance-to-key so the
    glyph floats over whatever it's composited onto regardless of which key
    colour this particular asset used."""
    rgb = img.convert("RGB")
    key = rgb.getpixel((0, 0))
    out = Image.new("RGBA", img.size)
    pixels = []
    for r, g, b in rgb.getdata():
        dist = abs(r - key[0]) + abs(g - key[1]) + abs(b - key[2])
        alpha = min(255, dist * 3)
        pixels.append((r, g, b, alpha))
    out.putdata(pixels)
    return out


def _glyph_with_alpha(glyph: Image.Image) -> Image.Image:
    if glyph.info.get("has_alpha"):
        return glyph.convert("RGBA")
    return _glyph_alpha_from_corner(glyph)


def _composite_center(bg: Image.Image, glyph: Image.Image) -> Image.Image:
    bg = bg.convert("RGBA")
    glyph_a = _glyph_with_alpha(glyph)
    x = (bg.width - glyph_a.width) // 2
    y = (bg.height - glyph_a.height) // 2
    out = bg.copy()
    out.alpha_composite(glyph_a, (max(0, x), max(0, y)))
    return out


def _png_data_uri(img: Image.Image) -> str:
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    b64 = base64.b64encode(buf.getvalue()).decode("ascii")
    return f"data:image/png;base64,{b64}"


def _svg_image_element(elem_id: str, img: Image.Image) -> str:
    return (
        f'<image id="{escape(elem_id)}" width="{img.width}" height="{img.height}" '
        f'x="0" y="0" xlink:href="{_png_data_uri(img)}"/>'
    )


def _wrap_svg(elements: list[str], canvas_w: int, canvas_h: int) -> str:
    body = "\n  ".join(elements)
    return (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
        f'width="{canvas_w}" height="{canvas_h}">\n'
        f"  {body}\n"
        "</svg>\n"
    )


def _solid(w: int, h: int, rgba: tuple[int, int, int, int]) -> Image.Image:
    return Image.new("RGBA", (max(1, w), max(1, h)), rgba)


# Aurorae always *tiles* left/right/top/bottom decoration elements natively
# (KSvg's FrameSvg model). A theme part declaring SizingType=Stretch expects
# the opposite -- one smooth stretch across the window edge, not a repeat --
# so pre-stretch those to a size comfortably larger than any real window
# before handing them to Aurorae, making its native tiling a no-op in
# practice instead of producing a visible "brick" repeat.
_PRESTRETCH_TARGET = 4096


def _prestretch(img: Image.Image, axis: str, sizingtype: str) -> Image.Image:
    if sizingtype.lower() != "stretch":
        return img
    if axis == "width":
        return img.resize((_PRESTRETCH_TARGET, img.height), Image.BILINEAR)
    return img.resize((img.width, _PRESTRETCH_TARGET), Image.BILINEAR)


def _nine_slice_resize(img: Image.Image, margins: tuple[int, int, int, int], tw: int, th: int) -> Image.Image:
    """Scale a 9-slice bitmap to (tw, th): corners fixed, edges stretched
    along one axis, centre stretched both ways -- what uxtheme does with
    SizingType=Stretch art that's smaller than the part it paints."""
    l, r, t, b = margins
    w, h = img.size
    tw, th = max(tw, l + r + 1), max(th, t + b + 1)
    if (w, h) == (tw, th):
        return img
    l, r = min(l, w - 1), min(r, w - 1 - min(l, w - 1))
    t, b = min(t, h - 1), min(b, h - 1 - min(t, h - 1))
    out = Image.new("RGBA", (tw, th), (0, 0, 0, 0))
    xs = [(0, l, 0, l), (l, w - r, l, tw - r), (w - r, w, tw - r, tw)]
    ys = [(0, t, 0, t), (t, h - b, t, th - b), (h - b, h, th - b, th)]
    for sx0, sx1, dx0, dx1 in xs:
        for sy0, sy1, dy0, dy1 in ys:
            if sx1 <= sx0 or sy1 <= sy0 or dx1 <= dx0 or dy1 <= dy0:
                continue
            piece = img.crop((sx0, sy0, sx1, sy1)).resize((dx1 - dx0, dy1 - dy0), Image.BILINEAR)
            out.paste(piece, (dx0, dy0))
    return out


# Art narrower/shorter than this is a stretchable 9-slice (Windows Classic
# style), not a button drawn at final size.
AT_SIZE_MIN_PX = 12


def caption_shadow(theme: MsStyle, active: bool = True):
    """XP's single, unblurred text shadow, resolved at the caption state."""
    props = theme.effective("window", "caption", "active" if active else "inactive")
    if props.get("textshadowtype", "none").lower() != "single":
        return None
    colour = parse_color(props.get("textshadowcolor", "0 0 0"))
    offset = tuple(int(v.strip()) for v in props.get("textshadowoffset", "1,1").split(","))
    return colour, (offset[0], offset[-1])


def _caption_metrics(theme: MsStyle) -> tuple[int, int]:
    m = theme.effective("sysmetrics")
    try:
        cap_h = int(m.get("captionbarheight", 25))
        cap_w = int(m.get("captionbarwidth", cap_h))
    except ValueError:
        cap_h, cap_w = 25, 25
    return cap_h, cap_w


def _button_target_size(theme: MsStyle) -> tuple[int, int]:
    """Caption button size: the art's own size when drawn at size, else the
    theme's metrics (XP fits buttons ~4px inside the caption height)."""
    size = _button_frame_size(theme, "closebutton") or (21, 21)
    if size[0] >= AT_SIZE_MIN_PX and size[1] >= AT_SIZE_MIN_PX:
        return size
    cap_h, cap_w = _caption_metrics(theme)
    return max(8, cap_w - 3), max(8, cap_h - 4)


def build_decoration_svg(theme: MsStyle) -> tuple[str, dict[str, dict[str, object]]]:
    cap = theme.effective("window", "caption")
    cap_img = theme.get_background_bitmap("window", "caption")
    frames = _slice_strip(cap_img, int(cap.get("imagecount", 2)), cap.get("imagelayout", "vertical").lower() == "vertical")
    fw, fh = frames[ACTIVE].size
    left_m, right_m, top_m, bottom_m = parse_margins(cap["sizingmargins"])

    cap_sizingtype = cap.get("sizingtype", "stretch")

    # KSvg tiles a FrameSvg's edge elements unless the SVG carries a
    # "hint-stretch-borders" element. We used to fake stretching by
    # pre-stretching the art to 4096px, but a tiled 4096px element only ever
    # shows its first window-width slice -- fine for a caption whose stretch
    # region is a plain vertical gradient, wrong for art that varies along the
    # edge (Blackcomb's frame bottom is a left-to-right gradient and came out
    # flat). When every frame part is SizingType=Stretch, hand KSvg the art at
    # its natural size and let it stretch properly.
    def _part_stretches(part: str) -> bool:
        eff = theme.effective("window", part)
        return "imagefile" not in eff or eff.get("sizingtype", "stretch").lower() == "stretch"

    stretch_borders = all(_part_stretches(p) for p in ("caption", "frameleft", "frameright", "framebottom"))

    def fit(img: Image.Image, axis: str, sizingtype: str) -> Image.Image:
        return img if stretch_borders else _prestretch(img, axis, sizingtype)

    # Classic-style themes ship a few-pixel caption 9-slice that XP stretches
    # to CaptionBarHeight (+ the frame edge); at-size art is used as-is.
    cap_h, _cap_w = _caption_metrics(theme)
    stretch_caption = fh < cap_h
    fh_eff = fh

    def corner_crops(frame: Image.Image) -> dict[str, Image.Image]:
        # FrameSvg paints corners into (side-border-width x top-height)
        # rects, so a corner crop wider than the side border gets squashed
        # and its rounding collapses: crop corners to exactly the border
        # width. The stretched top must come from the caption's own
        # stretch region (between its sizing margins) -- anything from the
        # fixed end-caps smears when stretched. The cap pixels between the
        # border width and the sizing margin are dropped; in practice
        # that's a column or two of edge shading.
        bw_l, bw_r = left_active.width, right_active.width
        if stretch_caption:
            frame = _nine_slice_resize(frame, (0, 0, top_m, bottom_m), fw, fh_eff)
        return {
            "topleft": frame.crop((0, 0, bw_l, fh_eff)),
            "topright": frame.crop((fw - bw_r, 0, fw, fh_eff)),
            "top": fit(frame.crop((left_m, 0, fw - right_m, fh_eff)), "width", cap_sizingtype),
        }

    def edge_frame(part: str, key: str, axis: str) -> Image.Image:
        eff = theme.effective("window", part)
        img = theme.get_background_bitmap("window", part)
        n = int(eff.get("imagecount", 2))
        vertical = eff.get("imagelayout", "vertical").lower() == "vertical"
        frame = _slice_strip(img, n, vertical)[key]
        return fit(frame, axis, eff.get("sizingtype", "tile"))

    left_active = edge_frame("frameleft", ACTIVE, "height")
    left_inactive = edge_frame("frameleft", INACTIVE, "height")
    right_active = edge_frame("frameright", ACTIVE, "height")
    right_inactive = edge_frame("frameright", INACTIVE, "height")
    if stretch_caption:
        # caption height + the frame's own edge (same thickness as the sides)
        fh_eff = cap_h + left_active.width

    # The bottom frame bitmap carries the bottom corners in its left/right
    # sizing margins; only the middle stretches.
    fb = theme.effective("window", "framebottom")
    fb_img = theme.get_background_bitmap("window", "framebottom")
    fb_frames = _slice_strip(fb_img, int(fb.get("imagecount", 2)), fb.get("imagelayout", "vertical").lower() == "vertical")
    fb_w = fb_frames[0].width
    fb_l, fb_r = parse_margins(fb["sizingmargins"])[:2] if "sizingmargins" in fb else (0, 0)
    if fb_l + fb_r >= fb_w or fb_l == 0 or fb_r == 0:
        fb_l = fb_r = max(1, fb_w // 4)
    # FrameSvg paints each corner at its own element width, so a bottom corner
    # narrower than the side border leaves an unpainted notch there (Blackcomb
    # declares 1px bottom margins against a 4px frame).
    fb_l = max(fb_l, left_active.width)
    fb_r = max(fb_r, right_active.width)
    if fb_l + fb_r >= fb_w:
        fb_l = fb_r = max(1, fb_w // 4)

    def bottom_pieces(frame: Image.Image) -> dict[str, Image.Image]:
        w, h = frame.size
        return {
            "bottomleft": frame.crop((0, 0, fb_l, h)),
            "bottom": fit(frame.crop((fb_l, 0, w - fb_r, h)), "width", fb.get("sizingtype", "tile")),
            "bottomright": frame.crop((w - fb_r, 0, w, h)),
        }

    # Maximized windows use the dedicated MaxCaption strip (no corners);
    # Aurorae only reads the "center" element for the maximized prefix and
    # stretches it across the whole title area.
    maxcap = theme.effective("window", "maxcaption")
    if "imagefile" in maxcap and "sizingmargins" in maxcap:
        m_img = theme.get_background_bitmap("window", "maxcaption")
        m_frames = _slice_strip(m_img, int(maxcap.get("imagecount", 2)), maxcap.get("imagelayout", "vertical").lower() == "vertical")
        m_l, m_r = parse_margins(maxcap["sizingmargins"])[:2]

        def max_center(frame: Image.Image) -> Image.Image:
            w, h = frame.size
            if m_l + m_r >= w:
                return frame
            return fit(frame.crop((m_l, 0, w - m_r, h)), "width", maxcap.get("sizingtype", "tile"))

        max_active = max_center(m_frames[ACTIVE])
        max_inactive = max_center(m_frames[min(INACTIVE, len(m_frames) - 1)])
    else:
        max_active = corner_crops(frames[ACTIVE])["top"]
        max_inactive = corner_crops(frames[INACTIVE])["top"]

    elements: list[str] = []

    def emit_edge_set(prefix: str, active: bool):
        frame = frames[ACTIVE if active else INACTIVE]
        crops = corner_crops(frame)
        bottoms = bottom_pieces(fb_frames[ACTIVE if active else min(INACTIVE, len(fb_frames) - 1)])
        left = left_active if active else left_inactive
        right = right_active if active else right_inactive
        elements.append(_svg_image_element(f"{prefix}-topleft", crops["topleft"]))
        elements.append(_svg_image_element(f"{prefix}-top", crops["top"]))
        elements.append(_svg_image_element(f"{prefix}-topright", crops["topright"]))
        elements.append(_svg_image_element(f"{prefix}-left", left))
        elements.append(_svg_image_element(f"{prefix}-right", right))
        elements.append(_svg_image_element(f"{prefix}-bottomleft", bottoms["bottomleft"]))
        elements.append(_svg_image_element(f"{prefix}-bottom", bottoms["bottom"]))
        elements.append(_svg_image_element(f"{prefix}-bottomright", bottoms["bottomright"]))
        elements.append(_svg_image_element(f"{prefix}-center", _solid(1, 1, (0, 0, 0, 0))))

    emit_edge_set("decoration", active=True)
    emit_edge_set("decoration-inactive", active=False)
    if stretch_borders:
        elements.append(_svg_image_element("hint-stretch-borders", _solid(1, 1, (0, 0, 0, 0))))
    elements.append(_svg_image_element("decoration-maximized-center", max_active))
    elements.append(_svg_image_element("decoration-maximized-inactive-center", max_inactive))

    canvas_w = fw * 4
    canvas_h = fh + fb_frames[0].height + max(left_active.height, right_active.height)
    svg = _wrap_svg(elements, canvas_w, canvas_h)

    # --- Geometry, derived from the theme's own button offsets/margins ---
    # Caption buttons are positioned via OffsetType=TopRight, Offset=x,y:
    # y is the gap above the buttons, |x| - width is the gap to the right
    # edge, and the pitch between adjacent buttons gives the spacing.
    btn_w, btn_h = _button_target_size(theme)

    def offset_of(part: str) -> tuple[int, int] | None:
        eff = theme.effective("window", part)
        if "offset" not in eff:
            return None
        try:
            x, y = [int(v.strip()) for v in eff["offset"].split(",")[:2]]
        except ValueError:
            return None
        return x, y

    close_off = offset_of("closebutton")
    max_off = offset_of("maxbutton")
    blank_buttons = False
    close_bg = theme.effective("window", "closebutton")
    if "imagefile" in close_bg:
        close_img = theme.get_background_bitmap("window", "closebutton")
        if close_img is not None:
            blank_buttons = _is_blank(_slice_strip(close_img, int(close_bg.get("imagecount", 8)),
                                                   close_bg.get("imagelayout", "vertical").lower() == "vertical"))
    strip = caption_button_strip(theme) if blank_buttons else None
    if strip is not None:
        # The cluster art *is* the buttons (see caption_button_strip): edge to
        # edge slots, so the caption's right end is reproduced exactly.
        _slots, btn_w, btn_h = strip
        title_edge_top = 0
        title_edge_right = 0
        spacing = 0
    elif stretch_caption or close_off is None:
        # Metrics-driven: buttons centred in the caption below the frame edge.
        title_edge_top = left_active.width + max(0, (fh_eff - left_active.width - btn_h) // 2)
        title_edge_right = left_active.width + 2
        spacing = 2
    else:
        title_edge_top = max(0, close_off[1])
        title_edge_right = max(0, -close_off[0] - btn_w)
        spacing = max(0, abs(max_off[0]) - abs(close_off[0]) - btn_w) if max_off else 3
    title_height = btn_h
    title_edge_bottom = max(0, fh_eff - title_edge_top - title_height)

    content_l = parse_margins(cap["contentmargins"])[0] if "contentmargins" in cap else 1
    caption_l = parse_margins(cap["captionmargins"])[0] if "captionmargins" in cap else 4
    title_edge_left = content_l + caption_l

    m = theme.effective("sysmetrics")

    def rgba(key: str, default: tuple[int, int, int]) -> str:
        r, g, b = parse_color(m[key]) if key in m else default
        return f"{r},{g},{b},255"

    active_shadow = caption_shadow(theme, True)
    inactive_shadow = caption_shadow(theme, False)
    shadow_off = (active_shadow or inactive_shadow or ((0, 0, 0), (1, 1)))[1]

    def shadow_colour(shadow):
        return ",".join(map(str, (*shadow[0], 255))) if shadow else "0,0,0,0"

    rc = {
        "General": {
            "TitleAlignment": "Left",
            "LeftButtons": "M",
            "RightButtons": "IAX",
            "Shadow": "false",
            "ActiveTextColor": rgba("captiontext", (255, 255, 255)),
            "InactiveTextColor": rgba("inactivecaptiontext", (216, 228, 248)),
            "UseTextShadow": "true" if (active_shadow or inactive_shadow) else "false",
            "ActiveTextShadowColor": shadow_colour(active_shadow),
            "InactiveTextShadowColor": shadow_colour(inactive_shadow),
            "TextShadowOffsetX": shadow_off[0],
            "TextShadowOffsetY": shadow_off[1] if len(shadow_off) > 1 else shadow_off[0],
        },
        "Layout": {
            "BorderLeft": left_active.width,
            "BorderRight": right_active.width,
            "BorderBottom": fb_frames[0].height,
            "BorderTop": 0,
            "TitleEdgeTop": title_edge_top,
            "TitleEdgeBottom": title_edge_bottom,
            "TitleEdgeLeft": title_edge_left,
            "TitleEdgeRight": title_edge_right,
            "TitleEdgeTopMaximized": title_edge_top,
            "TitleEdgeBottomMaximized": title_edge_bottom,
            "TitleEdgeLeftMaximized": title_edge_left,
            "TitleEdgeRightMaximized": title_edge_right,
            "TitleBorderLeft": 4,
            "TitleBorderRight": 4,
            "TitleHeight": title_height,
            "ButtonWidth": btn_w,
            "ButtonHeight": btn_h,
            "ButtonSpacing": spacing,
            "ButtonMarginTop": 0,
            "ExplicitButtonSpacer": btn_w,
        },
    }
    return svg, rc


def _button_frame_size(theme: MsStyle, part: str) -> tuple[int, int] | None:
    eff = theme.effective("window", part)
    if "imagefile" not in eff:
        return None
    img = theme.get_background_bitmap("window", part)
    n = int(eff.get("imagecount", 8))
    vertical = eff.get("imagelayout", "vertical").lower() == "vertical"
    return _slice_strip(img, n, vertical)[0].size


CAPTION_BUTTON_ORDER = ("minbutton", "maxbutton", "restorebutton", "closebutton")


def caption_button_strip(theme: MsStyle) -> tuple[dict[str, list[Image.Image]], int, int] | None:
    """Split the caption's fixed right end into one image per caption button.

    Some styles (Blackcomb and other Longhorn-era ports) ship button art that
    is a single fully transparent pixel and paint the whole cluster -- buttons
    and the decoration around them -- into the caption bitmap's right sizing
    margin instead. uxtheme composites the two, so that art is what the user
    sees. Aurorae can't show it in the caption itself: the title bar's middle
    is stretched and its corners are clamped to the frame's border width
    (verified by rendering a 111px-wide corner: KWin drew it at 4px). Buttons,
    though, are drawn over the title bar at their own size -- so the right end
    is cut into equal button-sized slots, which reproduces it exactly, with the
    rightmost slot (close) where XP puts it.

    -> ({part: [active, inactive]}, slot width, slot height), or None.
    """
    cap = theme.effective("window", "caption")
    cap_img = theme.get_background_bitmap("window", "caption")
    if cap_img is None or "sizingmargins" not in cap:
        return None
    right_margin = parse_margins(cap["sizingmargins"])[1]
    parts = [p for p in CAPTION_BUTTON_ORDER
             if p in theme.parts("window") and "offset" in theme.effective("window", p)]
    # restore sits where maximize does, so it doesn't get a slot of its own
    slots = [p for p in parts if p != "restorebutton"]
    if not slots or right_margin < 3 * len(slots):
        return None
    frames = [f.convert("RGBA") for f in
              _slice_strip(cap_img, int(cap.get("imagecount", 2)),
                           cap.get("imagelayout", "vertical").lower() == "vertical")]
    width = right_margin // len(slots)
    height = frames[0].height
    out: dict[str, list[Image.Image]] = {}
    for i, part in enumerate(slots):
        x0 = frames[0].width - right_margin + i * width
        out[part] = [f.crop((x0, 0, x0 + width, height)) for f in frames]
    if "restorebutton" in parts and "maxbutton" in out:
        out["restorebutton"] = out["maxbutton"]
    return out, width, height


def _is_blank(frames: list[Image.Image]) -> bool:
    return all(f.convert("RGBA").getchannel("A").getextrema()[1] == 0 for f in frames)


def build_button_svg(theme: MsStyle, part: str) -> str | None:
    if part not in theme.parts("window"):
        return None
    eff = theme.effective("window", part)
    if "imagefile" not in eff:
        return None
    bg_img = theme.get_background_bitmap("window", part)
    n = int(eff.get("imagecount", 8))
    vertical = eff.get("imagelayout", "vertical").lower() == "vertical"
    bg_frames = _slice_strip(bg_img, n, vertical)
    if _is_blank(bg_frames):
        strip = caption_button_strip(theme)
        if strip is None or part not in strip[0]:
            return None                       # genuinely buttonless
        art = strip[0][part]
        active, inactive = art[0], art[min(1, len(art) - 1)]
        return _wrap_svg([_svg_image_element("active-center", active),
                          _svg_image_element("inactive-center", inactive),
                          _svg_image_element("hover-center", active),
                          _svg_image_element("pressed-center", active)],
                         active.width, active.height)
    target = _button_target_size(theme)
    if bg_frames[0].size != target and "sizingmargins" in eff:
        margins = parse_margins(eff["sizingmargins"])
        bg_frames = [_nine_slice_resize(f.convert("RGBA"), margins, *target) for f in bg_frames]

    glyph_key = eff.get("imagefile3") or eff.get("imagefile1")
    glyph_frames = None
    if glyph_key:
        glyph_img = theme.get_bitmap(glyph_key)
        glyph_frames = _slice_strip(glyph_img, n, vertical)

    elements = []
    for state, idx in BUTTON_STATE_INDEX.items():
        if idx >= len(bg_frames):
            continue
        bg = bg_frames[idx]
        composed = _composite_center(bg, glyph_frames[idx]) if glyph_frames else bg.convert("RGBA")
        elements.append(_svg_image_element(f"{state}-center", composed))

    fw, fh = bg_frames[0].size
    return _wrap_svg(elements, fw, fh)


METADATA_DESKTOP = """[Desktop Entry]
Name={name}
Comment=Ported from the Windows XP visual style "{name}" by XPLasma
X-KDE-PluginInfo-Name={plugin_id}
X-KDE-PluginInfo-Author=XPLasma
X-KDE-PluginInfo-Version=1.0
X-KDE-PluginInfo-License=Unknown
"""

METADATA_JSON = {
    "KPackageStructure": "Aurorae",
    "KPlugin": {
        "Authors": [{"Name": "XPLasma"}],
        "Id": "",
        "Name": "",
        "Version": "1.0",
    },
}


def generate_theme_package(msstyle_path: str, out_dir: str, theme_name: str,
                           ini_resource_name: str | None = None) -> str:
    theme = MsStyle.load(msstyle_path, ini_resource_name=ini_resource_name)
    plugin_id = "".join(c if c.isalnum() else "" for c in theme_name) or "XPTheme"
    pkg_dir = os.path.join(out_dir, plugin_id)
    os.makedirs(pkg_dir, exist_ok=True)

    decoration_svg, rc = build_decoration_svg(theme)
    with open(os.path.join(pkg_dir, "decoration.svg"), "w", encoding="utf-8") as f:
        f.write(decoration_svg)

    for part, button_name in AURORAE_BUTTON_NAMES.items():
        svg = build_button_svg(theme, part)
        if svg:
            with open(os.path.join(pkg_dir, f"{button_name}.svg"), "w", encoding="utf-8") as f:
                f.write(svg)

    with open(os.path.join(pkg_dir, "metadata.desktop"), "w", encoding="utf-8") as f:
        f.write(METADATA_DESKTOP.format(name=theme_name, plugin_id=plugin_id))

    meta_json = json.loads(json.dumps(METADATA_JSON))
    meta_json["KPlugin"]["Id"] = plugin_id
    meta_json["KPlugin"]["Name"] = theme_name
    with open(os.path.join(pkg_dir, "metadata.json"), "w", encoding="utf-8") as f:
        json.dump(meta_json, f, indent=2)

    rc_lines: list[str] = []
    for section, keys in rc.items():
        rc_lines.append(f"[{section}]")
        rc_lines += [f"{k}={v}" for k, v in keys.items()]
        rc_lines.append("")
    with open(os.path.join(pkg_dir, f"{plugin_id}rc"), "w", encoding="utf-8") as f:
        f.write("\n".join(rc_lines))

    return pkg_dir
