"""Draw the Windows 2000 "Display Properties -> Appearance" preview pane.

This is not an approximation: it draws with the same primitives the theme
generator uses (DrawEdge bevels, caption gradient, Marlett glyphs, the
scheme's own metrics), so what the editor shows is what the generated
decoration will put on screen.

Layout follows the Win2000 preview: an Inactive Window behind an Active
Window whose client shows a tab strip, a sunken list box with a scrollbar,
and a Message Box sitting on top of it.
"""
from __future__ import annotations

from PIL import Image, ImageDraw

import classic_gen as cg
import theme_file as tf
import xp_assets

CANVAS = (347, 219)      # the real Display Properties dialog's sample box, measured
# Matches [Layout] ButtonSpacing in the generated Aurorae rc, so the preview
# predicts the live window rather than the original GDI window.
BUTTON_SPACING = 2
GLYPH_ORDER = ("minimize", "maximize", "close")   # close is rightmost
SCROLL_UP, SCROLL_DOWN = "t", "u"                 # Marlett arrowheads


def _bevel(s: cg.Scheme, size: tuple[int, int], sunken: bool = False) -> Image.Image:
    """A standalone 3D box, e.g. one caption button or scrollbar arrow."""
    w, h = size
    img = Image.new("RGBA", (w, h), cg._rgba(s.c("buttonface")))
    (cg.button_edge_sunken if sunken else cg.button_edge)(img_draw(img), (0, 0, w - 1, h - 1), s)
    return img


def img_draw(img: Image.Image) -> ImageDraw.ImageDraw:
    return ImageDraw.Draw(img)


def _glyph_box(s: cg.Scheme, glyph: str, size: tuple[int, int], sunken: bool = False) -> Image.Image:
    """A 3D button with a centred Marlett glyph -- caption or scrollbar arrow."""
    w, h = size
    img = _bevel(s, size, sunken=sunken)
    px = max(6, min(w, h) - 4)
    font = cg._glyph_font(px)
    if font:
        cg._stamp_glyph(img, glyph, font, ((w - px) // 2, (h - px) // 2), s.c("buttontext"))
    return img


def _caption(s: cg.Scheme, w: int, active: bool, title: str,
             buttons: tuple[str, ...] = GLYPH_ORDER) -> Image.Image:
    """A classic caption bar: horizontal gradient, title text, Marlett buttons."""
    h = s.caption
    img = Image.new("RGBA", (w, h), cg._rgba(s.c("buttonface")))
    img.alpha_composite(cg._gradient(w, h, *cg._caption_colors(s, active)))
    colour = s.c("titletext") if active else s.c("inactivetitletext")
    font = cg._pil_font(s.caption_font)
    ImageDraw.Draw(img).text((4, h // 2), title, font=font, fill=cg._rgba(colour), anchor="lm")

    x = w - s.btn_w
    for name in reversed(buttons):        # close is rightmost
        if x >= 0:
            img.alpha_composite(_glyph_box(s, cg.GLYPHS[name], (s.btn_w, s.btn_h)), (x, 0))
        x -= s.btn_w + BUTTON_SPACING
    return img


def _window(img: Image.Image, s: cg.Scheme, rect: tuple[int, int, int, int], active: bool,
            title: str, dialog: bool = False,
            buttons: tuple[str, ...] = GLYPH_ORDER) -> tuple[int, int, int, int]:
    """Draw a complete classic window into `img`; returns its client rect."""
    x0, y0, x1, y1 = rect
    F = s.frame
    d = ImageDraw.Draw(img)
    d.rectangle(rect, fill=cg._rgba(s.c("buttonface")))
    cg.window_edge(d, rect, s)
    img.alpha_composite(_caption(s, (x1 - x0 + 1) - 2 * F, active, title, buttons), (x0 + F, y0 + F))

    client = (x0 + F, y0 + F + s.caption, x1 - F, y1 - F)
    if client[2] > client[0] and client[3] > client[1]:
        if dialog:
            d.rectangle(client, fill=cg._rgba(s.c("buttonface")))
            cg.button_edge(d, client, s)
        else:
            d.rectangle(client, fill=cg._rgba(s.c("window")))
            cg.button_edge_sunken(d, client, s)
    return client


def _scrollbar(img: Image.Image, s: cg.Scheme, box: tuple[int, int, int, int]) -> None:
    """A classic vertical scrollbar: dithered trough, arrows, raised thumb."""
    x0, y0, x1, y1 = box
    w = x1 - x0 + 1
    if w < 6 or y1 - y0 < 3 * w:
        return
    d = ImageDraw.Draw(img)
    face, light = cg._rgba(s.c("buttonface")), cg._rgba(s.c("buttonhilight"))
    d.rectangle(box, fill=face)
    for y in range(y0, y1 + 1):            # 50% dither, the classic trough
        for x in range(x0, x1 + 1):
            if (x + y) % 2 == 0:
                img.putpixel((x, y), light)
    grip = max(3, w)                       # square arrow buttons at each end
    img.alpha_composite(_glyph_box(s, SCROLL_UP, (w, grip)), (x0, y0))
    img.alpha_composite(_glyph_box(s, SCROLL_DOWN, (w, grip)), (x0, y1 - grip + 1))
    thumb_top, thumb_h = y0 + grip + 2, max(grip, (y1 - y0) // 3)
    if thumb_top + thumb_h < y1 - grip:
        img.alpha_composite(_bevel(s, (w, thumb_h)), (x0, thumb_top))


def _tabs(img: Image.Image, s: cg.Scheme, box: tuple[int, int, int, int]) -> int:
    """The preview's Normal/Disabled/Selected tab strip. Returns its height."""
    x, y = box[0], box[1]
    font = cg._pil_font(s.ui_font)
    tab_h = max(16, round(s.ui_font[1] * 96 / 72) + 10)
    d = ImageDraw.Draw(img)
    for label, enabled, active in (("Normal", True, True), ("Disabled", False, False),
                                   ("Selected", True, False)):
        tw = int(font.getlength(label)) + 16
        h = tab_h + (2 if active else 0)
        rect = (x, y + tab_h - h, x + tw, y + tab_h)
        if rect[2] > box[2]:
            break
        d.rectangle(rect, fill=cg._rgba(s.c("buttonface")))
        cg.button_edge(d, rect, s)
        if active:                          # merge the selected tab into the page
            d.line([(rect[0] + 2, rect[3]), (rect[2] - 2, rect[3])], fill=cg._rgba(s.c("buttonface")))
        colour = s.c("buttontext") if enabled else s.c("graytext")
        d.text((rect[0] + tw // 2, rect[1] + h // 2), label, font=font, fill=cg._rgba(colour), anchor="mm")
        x = rect[2] + 1
    return tab_h


def _list_box(img: Image.Image, s: cg.Scheme, box: tuple[int, int, int, int], text: str) -> None:
    """The sunken white list area, its text, and the scrollbar down its right."""
    x0, y0, x1, y1 = box
    d = ImageDraw.Draw(img)
    d.rectangle(box, fill=cg._rgba(s.c("window")))
    cg.button_edge_sunken(d, box, s)
    d.text((x0 + 5, y0 + 4), text, font=cg._pil_font(s.ui_font), fill=cg._rgba(s.c("windowtext")))
    bar = max(8, s.scroll)
    _scrollbar(img, s, (x1 - bar - 1, y0 + 1, x1 - 1, y1 - 1))


def _push_button(s: cg.Scheme, label: str, default: bool = False) -> Image.Image:
    """A classic push button: raised bevel, centred label, optional default ring."""
    font = cg._pil_font(s.ui_font)
    h = max(20, round(s.ui_font[1] * 96 / 72) + 12)
    w = max(60, int(font.getlength(label)) + 24)
    img = Image.new("RGBA", (w, h), cg._rgba(s.c("buttonface")))
    d = ImageDraw.Draw(img)
    box = (0, 0, w - 1, h - 1)
    if default:
        d.rectangle(box, outline=cg._rgba((0, 0, 0)))
        box = (1, 1, w - 2, h - 2)
    cg.button_edge(d, box, s)
    d.text((w // 2, h // 2), label, font=font, fill=cg._rgba(s.c("buttontext")), anchor="mm")
    return img


def render(s: cg.Scheme, size: tuple[int, int] = CANVAS) -> Image.Image:
    """The whole preview pane for `s`."""
    W, H = size
    img = Image.new("RGBA", (W, H), cg._rgba(s.c("background")))
    m = round(W * 0.045)
    inner = (m, m, W - m - 1, H - m - 1)
    strip = s.caption + 2 * s.frame + 6

    # Inactive Window at the back, Active Window in front and offset down-right.
    back = (inner[0], inner[1], inner[2] - round(W * 0.04), inner[3] - round(H * 0.28))
    front = (inner[0] + round(W * 0.045), inner[1] + strip, inner[2], inner[3])
    _window(img, s, back, False, "Inactive Window")
    client = _window(img, s, front, True, "Active Window")

    if client[2] - client[0] > 40 and client[3] - client[1] > 30:
        tab_h = _tabs(img, s, (client[0] + 3, client[1] + 2, client[2] - 3, client[1] + 2))
        top = client[1] + 2 + tab_h
        if client[3] - top > 12:
            _list_box(img, s, (client[0] + 3, top, client[2] - 3, client[3] - 4), "Window Text")

        # Message Box over the bottom of the list, its own caption above it.
        box_h = max(50, round((client[3] - client[1]) * 0.52))
        box_w = max(120, round((client[2] - client[0]) * 0.66))
        mx, my = client[0] + 12, client[3] - box_h - 10
        mrect = (mx, my, mx + box_w, my + box_h)
        if mrect[2] < client[2] and mrect[3] <= client[3]:
            mclient = _window(img, s, mrect, True, "Message Box", dialog=True, buttons=("close",))
            ImageDraw.Draw(img).text((mclient[0] + 6, mclient[1] + 6), "Message Text",
                                     font=cg._pil_font(s.ui_font), fill=cg._rgba(s.c("buttontext")))
            ok = _push_button(s, "OK", default=True)
            if ok.width + 12 < mclient[2] - mclient[0]:
                img.alpha_composite(ok, (mclient[2] - ok.width - 7, mclient[3] - ok.height - 7))
    return img


def render_png(s: cg.Scheme, path: str, size: tuple[int, int] = CANVAS) -> str:
    render(s, size).convert("RGB").save(path)
    return path


# --- visual styles (real msstyles art) and the Themes desktop preview -------

def _shade(rgb, factor: float) -> tuple[int, int, int]:
    return tuple(max(0, min(255, round(v * factor))) for v in rgb)


def _cover(src: Image.Image, w: int, h: int) -> Image.Image:
    """Scale to fill (w, h) and centre-crop -- what WallpaperStyle=2 does."""
    scale = max(w / src.width, h / src.height)
    nw, nh = max(w, round(src.width * scale)), max(h, round(src.height * scale))
    out = src.resize((nw, nh), Image.LANCZOS)
    return out.crop(((nw - w) // 2, (nh - h) // 2, (nw - w) // 2 + w, (nh - h) // 2 + h))


def _place(src: Image.Image, w: int, h: int, fill: int,
           background: tuple[int, int, int]) -> Image.Image:
    """A wallpaper laid out in a (w, h) box according to its fill mode.

    The modes are Plasma's, which are Qt's ``Image.fillMode`` values -- the same
    integers ``theme_file`` maps Windows' WallpaperStyle onto. XP's own dialog
    offers only Center, Tile and Stretch; the rest are here because a .theme
    written by a later Windows can name them.

    ``background`` is the desktop colour, which shows wherever the image does
    not reach. It is not a detail: with Center or Tile it is most of what you
    see, which is exactly why XP puts a Color: control next to Position.
    """
    if fill == tf.FILL_STRETCH:
        return src.resize((w, h), Image.LANCZOS)
    if fill == tf.FILL_CROP:
        return _cover(src, w, h)
    out = Image.new("RGB", (w, h), background)
    if fill == tf.FILL_TILE:
        # Native size, repeated, clipped at the edges -- XP does not scale to fit.
        for y in range(0, h, src.height):
            for x in range(0, w, src.width):
                out.paste(src, (x, y))
        return out
    if fill == tf.FILL_FIT:
        scale = min(w / src.width, h / src.height)
        size = (max(1, round(src.width * scale)), max(1, round(src.height * scale)))
        out.paste(src.resize(size, Image.LANCZOS), ((w - size[0]) // 2, (h - size[1]) // 2))
        return out
    # Center, and anything unrecognised: native size, centred, clipped if it is
    # larger than the screen.
    out.paste(src, ((w - src.width) // 2, (h - src.height) // 2))
    return out


# The monitor bitmap with its magenta and its baked screen content punched out,
# decoded from the user's media at most once per process. False means "not
# tried yet"; None means "tried and the media isn't there".
_bezel_cache: Image.Image | None | bool = False


def _monitor_bezel() -> Image.Image | None:
    """Display Properties' monitor as RGBA, ready to sit over a wallpaper.

    Two things have to come out of the bitmap: the magenta surround, and the
    screen itself -- what is behind that rectangle is a canned XP desktop
    screenshot, and leaving it in would mean compositing our wallpaper over it
    and hoping the two line up.
    """
    global _bezel_cache
    if _bezel_cache is not False:
        return _bezel_cache
    bezel = None
    raw = xp_assets.monitor()
    if raw is not None:
        bezel = raw.convert("RGBA")
        left, top, right, bottom = xp_assets.MONITOR_SCREEN
        pixels = bezel.load()
        for y in range(bezel.height):
            for x in range(bezel.width):
                r, g, b, _a = pixels[x, y]
                if ((r, g, b) == xp_assets.MONITOR_TRANSPARENT
                        or (left <= x < right and top <= y < bottom)):
                    pixels[x, y] = (r, g, b, 0)
    _bezel_cache = bezel
    return bezel


def render_wallpaper(wallpaper: Image.Image | None, fill: int,
                     color: tuple[int, int, int],
                     size: tuple[int, int] = CANVAS,
                     face: tuple[int, int, int] = (236, 233, 216)) -> Image.Image:
    """The Desktop tab preview: the chosen wallpaper, on XP's monitor.

    Only this tab gets the monitor -- XP's Themes and Appearance tabs preview a
    *window*, and Screen Saver and Settings are the only other two that use it.

    Without the XP media there is no monitor bitmap to draw, and the fallback is
    the wallpaper alone in a screen-sized panel. Inventing a monitor would be
    worse: this app's whole claim is that it draws the real thing or nothing.
    """
    bezel = _monitor_bezel()
    if bezel is None:
        w, h = size
        return _place(wallpaper, w, h, fill, color) if wallpaper is not None \
            else Image.new("RGB", (w, h), color)

    bw, bh = bezel.size
    left, top, right, bottom = xp_assets.MONITOR_SCREEN
    # Composited at the bitmap's own resolution and scaled once at the end, so
    # the screen rectangle lands on exact pixels rather than on a rounded one.
    monitor = Image.new("RGB", (bw, bh), face)
    # With no wallpaper the screen is the desktop *colour*, not the dialog face
    # behind it -- "(None)" plus a Color: is a complete desktop in XP, and
    # showing the face would make the Color control look inert.
    screen = (_place(wallpaper, right - left, bottom - top, fill, color)
              if wallpaper is not None
              else Image.new("RGB", (right - left, bottom - top), color))
    monitor.paste(screen, (left, top))
    monitor = monitor.convert("RGBA")
    monitor.alpha_composite(bezel)
    monitor = monitor.convert("RGB")

    scale = min(size[0] / bw, size[1] / bh)
    mw, mh = max(1, round(bw * scale)), max(1, round(bh * scale))
    scene = Image.new("RGB", size, face)
    scene.paste(monitor.resize((mw, mh), Image.LANCZOS),
                ((size[0] - mw) // 2, (size[1] - mh) // 2))
    return scene


def _caption_bar(info, w: int, active: bool) -> Image.Image:
    """A title bar at the theme's own caption height: a visual style's art
    nine-sliced to width, or the Windows Classic gradient."""
    art = info.caption_img if active else info.caption_img_inactive
    if art is not None:
        import aurorae_gen as ag
        return ag._nine_slice_resize(art, info.caption_margins, w, info.caption_h)
    c0, c1 = info.gradient if active else info.gradient_inactive
    return cg._gradient(w, info.caption_h, c0, c1)


def _clip(draw, text: str, font, max_w: int) -> str:
    """Trim `text` with an ellipsis so it never runs under the buttons."""
    if max_w <= 0 or font.getlength(text) <= max_w:
        return text
    while text and font.getlength(text + "…") > max_w:
        text = text[:-1]
    return text + "…"


def _fit(img: Image.Image, w: int, h: int, tile: bool) -> Image.Image:
    """Size a frame strip to (w, h): tiled the way KSvg/uxtheme tile, or
    stretched, per the part's SizingType."""
    w, h = max(1, w), max(1, h)
    if img.size == (w, h):
        return img
    if not tile:
        return img.resize((w, h), Image.BILINEAR)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for y in range(0, h, img.height):
        for x in range(0, w, img.width):
            out.alpha_composite(img.crop((0, 0, min(img.width, w - x), min(img.height, h - y))), (x, y))
    return out


def _style_window(img: Image.Image, info, rect: tuple[int, int, int, int], active: bool,
                  title: str, buttons: bool = True, only_close: bool = False) -> tuple[int, int, int, int]:
    """A window drawn with the theme's own art -- the caption strip across the
    whole width (its rounded corners left transparent, as on the desktop), the
    FrameLeft/Right/Bottom strips down the sides, the buttons at the theme's
    own size. Returns the client rect."""
    import aurorae_gen as ag
    x0, y0, x1, y1 = rect
    W, H = x1 - x0 + 1, y1 - y0 + 1
    d = ImageDraw.Draw(img)
    face = cg._rgba(info.button_face)

    art = info.caption_img if active else info.caption_img_inactive
    left = info.frame_left if active else info.frame_left_inactive
    right = info.frame_right if active else info.frame_right_inactive
    bottom = info.frame_bottom if active else info.frame_bottom_inactive
    lw = left.width if left is not None else info.frame
    rw = right.width if right is not None else info.frame
    bh = bottom.height if bottom is not None else info.frame
    top_h = info.top_h or (info.caption_h + info.frame)

    if art is not None:
        img.alpha_composite(ag._nine_slice_resize(art, info.caption_margins, W, top_h), (x0, y0))
    else:
        d.rectangle((x0, y0, x1, y0 + top_h - 1), fill=face)
        c0, c1 = info.gradient if active else info.gradient_inactive
        img.alpha_composite(cg._gradient(W - lw - rw, info.caption_h, c0, c1), (x0 + lw, y0 + info.frame))

    body_h = H - top_h - bh
    if body_h > 0:
        if left is not None:
            img.alpha_composite(_fit(left, lw, body_h, info.frame_tile), (x0, y0 + top_h))
        else:
            d.rectangle((x0, y0 + top_h, x0 + lw - 1, y1 - bh), fill=face)
        if right is not None:
            img.alpha_composite(_fit(right, rw, body_h, info.frame_tile), (x1 - rw + 1, y0 + top_h))
        else:
            d.rectangle((x1 - rw + 1, y0 + top_h, x1, y1 - bh), fill=face)
    if bottom is not None:
        l, r = info.frame_bottom_margins
        img.alpha_composite(ag._nine_slice_resize(bottom, (l, r, 0, 0), W, bh), (x0, y1 - bh + 1))
    else:
        d.rectangle((x0, y1 - bh + 1, x1, y1), fill=face)
    if art is None:
        d.rectangle((x0, y0, x1, y1), outline=cg._rgba(_shade(info.button_face, 0.6)))

    # Buttons sit inside the title strip, just in from the right frame.
    bx = x1 - rw - 1
    if buttons and info.buttons:
        for b in reversed(info.buttons[-1:] if only_close else info.buttons):
            bx -= b.width
            if bx < x0 + lw:
                break
            by = y0 + max(0, (top_h + (lw if art is not None else 0) - b.height) // 2)
            img.alpha_composite(b, (bx, min(by, y0 + top_h - b.height)))
            bx -= BUTTON_SPACING
    font = cg._pil_font(info.caption_font)
    colour = info.caption_text if active else info.caption_text_inactive
    text_w = bx - (x0 + lw) - 8
    ty = y0 + (top_h + (lw if art is not None else info.frame)) // 2
    label = _clip(d, title, font, text_w)
    shadow = info.caption_shadow if active else info.caption_shadow_inactive
    if shadow:
        shadow_colour, (dx, dy) = shadow
        d.text((x0 + lw + 4 + dx, ty + dy), label, font=font,
               fill=cg._rgba(shadow_colour), anchor="lm")
    d.text((x0 + lw + 4, ty), label, font=font, fill=cg._rgba(colour), anchor="lm")

    client = (x0 + lw, y0 + top_h, x1 - rw, y1 - bh)
    if client[2] > client[0] and client[3] > client[1]:
        d.rectangle(client, fill=cg._rgba(info.window_bg))
    return client


def _style_scrollbar(img: Image.Image, info, box: tuple[int, int, int, int]) -> None:
    """A simple scrollbar in the theme's own colours (the real art lives in the
    widget style, which is a later phase)."""
    x0, y0, x1, y1 = box
    if x1 - x0 < 5 or y1 - y0 < 12:
        return
    d = ImageDraw.Draw(img)
    light = _shade(info.button_face, 1.18)
    d.rectangle(box, fill=cg._rgba(light))
    for y in range(y0 + 1, y1, 2):                 # 50% dither, the classic trough
        for x in range(x0 + 1, x1, 2):
            img.putpixel((x + (y // 2) % 2, y), cg._rgba(info.button_face))
    grip = min(x1 - x0, 12)
    for top in (y0, y1 - grip + 1):
        d.rectangle((x0, top, x1, top + grip - 1), outline=cg._rgba(_shade(info.button_face, 0.55)))
    thumb = (y0 + grip + 4, min(y1 - grip - 2, y0 + grip + 4 + max(10, (y1 - y0) // 3)))
    if thumb[1] > thumb[0]:
        d.rectangle((x0, thumb[0], x1, thumb[1]), fill=cg._rgba(info.button_face))
        d.rectangle((x0, thumb[0], x1, thumb[1]), outline=cg._rgba(_shade(info.button_face, 0.55)))


def _flat_button(info, label: str, default: bool = False) -> Image.Image:
    font = cg._pil_font(info.ui_font)
    h = max(18, round(info.ui_font[1] * 96 / 72) + 12)
    w = max(54, int(font.getlength(label)) + 24)
    img = Image.new("RGBA", (w, h), cg._rgba(info.button_face))
    d = ImageDraw.Draw(img)
    box = (0, 0, w - 1, h - 1)
    if default:
        d.rectangle(box, outline=cg._rgba((0, 0, 0)))
        box = (1, 1, w - 2, h - 2)
    d.rectangle(box, outline=cg._rgba(_shade(info.button_face, 0.55)))
    d.line([(box[0] + 1, box[1] + 1), (box[2] - 1, box[1] + 1)], fill=cg._rgba(_shade(info.button_face, 1.2)))
    d.text((w // 2, h // 2), label, font=font, fill=cg._rgba(info.window_text), anchor="mm")
    return img


def render_style(info, size: tuple[int, int] = CANVAS) -> Image.Image:
    """Chrome preview for a visual style: two overlapping windows carrying the
    style's real caption art, a list area and a message box."""
    W, H = size
    img = Image.new("RGBA", (W, H), cg._rgba(_shade(info.button_face, 0.88)))
    m = round(W * 0.045)
    inner = (m, m, W - m - 1, H - m - 1)
    strip = info.caption_h + 2 * info.frame + 6

    back = (inner[0], inner[1], inner[2] - round(W * 0.04), inner[3] - round(H * 0.28))
    front = (inner[0] + round(W * 0.045), inner[1] + strip, inner[2], inner[3])
    _style_window(img, info, back, False, "Inactive Window")
    client = _style_window(img, info, front, True, "Active Window")

    if client[2] - client[0] > 40 and client[3] - client[1] > 30:
        d = ImageDraw.Draw(img)
        inset = 3
        listbox = (client[0] + inset, client[1] + inset, client[2] - inset, client[3] - inset)
        d.rectangle(listbox, fill=cg._rgba(info.window_bg),
                    outline=cg._rgba(_shade(info.button_face, 0.55)))
        bar = max(9, round(info.caption_h * 0.55))
        d.text((listbox[0] + 5, listbox[1] + 4), "Window Text",
               font=cg._pil_font(info.ui_font), fill=cg._rgba(info.window_text))
        _style_scrollbar(img, info, (listbox[2] - bar - 1, listbox[1] + 1, listbox[2] - 1, listbox[3] - 1))

        box_h = max(46, round((client[3] - client[1]) * 0.52))
        box_w = max(110, round((client[2] - client[0]) * 0.66))
        mx, my = client[0] + 12, client[3] - box_h - 10
        mrect = (mx, my, mx + box_w, my + box_h)
        if mrect[2] < client[2] and mrect[3] <= client[3]:
            mclient = _style_window(img, info, mrect, True, "Message Box", only_close=True)
            ImageDraw.Draw(img).text((mclient[0] + 6, mclient[1] + 6), "Message Text",
                                     font=cg._pil_font(info.ui_font), fill=cg._rgba(info.window_text))
            ok = _flat_button(info, "OK", default=True)
            if ok.width + 12 < mclient[2] - mclient[0]:
                img.alpha_composite(ok, (mclient[2] - ok.width - 7, mclient[3] - ok.height - 7))
    return img


def _classic_start(s: cg.Scheme, h: int) -> Image.Image:
    """The classic Start button: a raised bevel, the flag, bold "Start"."""
    from xp_assets import start_flag
    font = cg._pil_font((s.ui_font[0], s.ui_font[1], True))
    flag = start_flag()
    flag = flag.convert("RGBA") if flag is not None else None
    label_w = int(font.getlength("Start"))
    w = 8 + (flag.width + 3 if flag is not None else 0) + label_w + 8
    img = _bevel(s, (w, h))
    x = 6
    if flag is not None:
        img.alpha_composite(flag, (x, max(0, (h - flag.height) // 2)))
        x += flag.width + 3
    ImageDraw.Draw(img).text((x, h // 2), "Start", font=font, fill=cg._rgba(s.c("buttontext")), anchor="lm")
    return img


def _taskbar(img: Image.Image, info, box: tuple[int, int, int, int]) -> None:
    """The bottom of the Themes preview: the theme's own taskbar art, Start
    pill and tray, or the classic raised bar with a bevelled Start button."""
    import aurorae_gen as ag
    x0, y0, x1, y1 = box
    W, bar_h = x1 - x0 + 1, y1 - y0 + 1
    d = ImageDraw.Draw(img)
    font = cg._pil_font(info.ui_font)
    clock = "10:24 AM"
    s = info.scheme

    if info.taskbar_img is not None:
        img.alpha_composite(ag._nine_slice_resize(info.taskbar_img, info.taskbar_margins, W, bar_h), (x0, y0))
    else:
        d.rectangle(box, fill=cg._rgba(info.button_face))
        if s is not None:
            hi, lo = cg._rgba(s.c("buttonhilight")), cg._rgba(s.c("buttonlight"))
            d.line([(x0, y0), (x1, y0)], fill=lo)
            d.line([(x0, y0 + 1), (x1, y0 + 1)], fill=hi)
        else:
            d.line([(x0, y0), (x1, y0)], fill=cg._rgba(_shade(info.button_face, 0.65)))
            d.line([(x0, y0 + 1), (x1, y0 + 1)], fill=cg._rgba(_shade(info.button_face, 1.2)))

    # Start
    if info.start_img is not None:
        img.alpha_composite(info.start_img, (x0, y0 + max(0, (bar_h - info.start_img.height) // 2)))
    elif s is not None:
        start = _classic_start(s, bar_h - 4)
        img.alpha_composite(start, (x0 + 2, y0 + 2 + max(0, (bar_h - 4 - start.height) // 2)))

    # Tray + clock, right-aligned
    clock_w = int(font.getlength(clock))
    if info.tray_img is not None:
        tw = clock_w + 24
        tray = ag._nine_slice_resize(info.tray_img, info.tray_margins, tw, bar_h)
        img.alpha_composite(tray, (x1 - tw + 1, y0))
        colour = info.clock_text or (255, 255, 255)
        d.text((x1 - 8, (y0 + y1) // 2), clock, font=font, fill=cg._rgba(colour), anchor="rm")
    elif s is not None:
        tray = (x1 - clock_w - 18, y0 + 3, x1 - 2, y1 - 2)
        cg.button_edge_sunken(d, tray, s)
        d.text((x1 - 10, (y0 + y1) // 2), clock, font=font, fill=cg._rgba(s.c("buttontext")), anchor="rm")
    else:
        d.text((x1 - 5, (y0 + y1) // 2), clock, font=font, fill=cg._rgba(info.window_text), anchor="rm")


def render_desktop(info, size: tuple[int, int] = CANVAS) -> Image.Image:
    """The Themes tab preview: the theme's wallpaper, a window, and the
    taskbar -- drawn at 1:1 like XP's own sample, so the chrome is real-size."""
    W, H = size
    img = Image.new("RGBA", (W, H), cg._rgba(info.desktop))
    if info.wallpaper is not None:
        img.alpha_composite(_cover(info.wallpaper, W, H))

    if info.taskbar_img is not None:
        bar_h = info.taskbar_img.height
    elif info.scheme is not None:
        bar_h = info.scheme.bar_height
    else:
        bar_h = 30
    bar_h = max(14, min(bar_h, round(H * 0.3)))
    left = round(W * 0.07)
    top = round(H * 0.11)
    rect = (left, top, W - left - round(W * 0.08), H - bar_h - round(H * 0.13))
    if rect[2] - rect[0] > 60 and rect[3] - rect[1] > 40:
        if info.scheme is not None:
            client = _window(img, info.scheme, rect, True, "Active Window")
        else:
            client = _style_window(img, info, rect, True, "Active Window")
        ImageDraw.Draw(img).text((client[0] + 6, client[1] + 5), "Window Text",
                                 font=cg._pil_font(info.ui_font), fill=cg._rgba(info.window_text))
    _taskbar(img, info, (0, H - bar_h, W - 1, H - 1))
    return img


def render_desktop_png(info, path: str, size: tuple[int, int] = CANVAS) -> str:
    render_desktop(info, size).convert("RGB").save(path)
    return path
