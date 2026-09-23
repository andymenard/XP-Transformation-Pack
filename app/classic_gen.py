"""Synthesize the *actual* Windows Classic look (XP with visual styles off):
no bitmaps exist for it -- GDI draws 3D bevels from the system colours --
so everything is generated from an appearance scheme: Aurorae decoration,
Plasma colour scheme, panel theme, Start button, solid desktop colour.

A scheme is colours + NONCLIENTMETRICS (frame, caption size, fonts). It
comes from one of XP's 36 stock schemes (decoded from HIVEDEF.INF into
schemes/xp_classic_schemes.json), from a .theme's [Control Panel\\Colors]
(+ its [Metrics] NonclientMetrics blob when present), or the built-in
"Windows Standard" values.
"""
from __future__ import annotations

import hashlib
import json
import os
import struct
from dataclasses import dataclass, field

from PIL import Image, ImageDraw, ImageFont

import hostexec as host
import paths
from aurorae_gen import METADATA_DESKTOP, METADATA_JSON, _solid, _svg_image_element, _wrap_svg
from theme_file import load_theme_file

WINDOWS_STANDARD = {
    "activetitle": (10, 36, 106), "gradientactivetitle": (166, 202, 240),
    "inactivetitle": (128, 128, 128), "gradientinactivetitle": (192, 192, 192),
    "titletext": (255, 255, 255), "inactivetitletext": (212, 208, 200),
    "buttonface": (212, 208, 200), "buttonhilight": (255, 255, 255),
    "buttonlight": (212, 208, 200), "buttonshadow": (128, 128, 128),
    "buttondkshadow": (64, 64, 64), "buttontext": (0, 0, 0),
    "window": (255, 255, 255), "windowtext": (0, 0, 0), "windowframe": (0, 0, 0),
    "hilight": (10, 36, 106), "hilighttext": (255, 255, 255),
    "graytext": (128, 128, 128), "menu": (212, 208, 200), "menutext": (0, 0, 0),
    "infowindow": (255, 255, 225), "infotext": (0, 0, 0),
    "background": (58, 110, 165), "appworkspace": (128, 128, 128),
}

SCHEMES_JSON = paths.SHIPPED_SCHEMES   # read-only: XP's stock schemes, transcribed
MARLETT = os.path.expanduser("~/.local/share/fonts/xp/marlett.ttf")
GLYPHS = {"close": "r", "minimize": "0", "maximize": "1", "restore": "2"}
Font = tuple[str, int, bool]  # (family, point size, bold)


@dataclass
class Scheme:
    name: str = "Windows Standard"
    colors: dict = field(default_factory=lambda: dict(WINDOWS_STANDARD))
    frame: int = 4          # SM_CXSIZEFRAME = iBorderWidth + 3
    caption: int = 18       # SM_CYCAPTION
    btn_w: int = 16         # caption buttons: iCaptionWidth - 2, iCaptionHeight - 4
    btn_h: int = 14
    caption_font: Font = ("Tahoma", 8, True)
    ui_font: Font = ("Tahoma", 8, False)
    menu: int = 18          # SM_CYMENU
    scroll: int = 16        # SM_CXVSCROLL
    icon_h: int = 75        # SM_CXICONSPACING
    icon_v: int = 75        # SM_CYICONSPACING

    @property
    def bar_height(self) -> int:
        # Classic taskbar: 28px at the standard 18px caption (22px task buttons).
        return max(28, self.caption + 10)

    def c(self, key: str) -> tuple[int, int, int]:
        return tuple(self.colors.get(key, WINDOWS_STANDARD.get(key, (0, 0, 0))))


def _metrics_from(scheme: Scheme, border: int, cap_w: int, cap_h: int,
                  caption_font: Font | None, menu_font: Font | None) -> None:
    scheme.frame = border + 3
    scheme.caption = cap_h
    scheme.btn_w, scheme.btn_h = max(10, cap_w - 2), max(8, cap_h - 4)
    if caption_font:
        scheme.caption_font = caption_font
    if menu_font:
        scheme.ui_font = menu_font


USER_SCHEMES_DIR = paths.USER_SCHEMES


def _scheme_table() -> dict:
    """Stock XP schemes, with anything in schemes/user/ layered on top."""
    table: dict = {}
    if os.path.exists(SCHEMES_JSON):
        with open(SCHEMES_JSON, encoding="utf-8") as f:
            table.update(json.load(f))
    if os.path.isdir(USER_SCHEMES_DIR):
        for fn in sorted(os.listdir(USER_SCHEMES_DIR)):
            if fn.endswith(".json") and not fn.startswith("."):
                with open(os.path.join(USER_SCHEMES_DIR, fn), encoding="utf-8") as f:
                    table.update(json.load(f))
    return table


def list_schemes() -> list[str]:
    return sorted(_scheme_table())


def scheme_to_entry(s: Scheme) -> dict:
    """The inverse of load_scheme's JSON branch, for saving edited schemes."""
    def f(spec: Font) -> dict:
        return {"face": spec[0], "pt": spec[1], "bold": spec[2], "italic": False}

    def size(v: int) -> int:
        return max(10, v)

    return {
        "metrics": {
            "border": max(0, s.frame - 3),
            "caption": [s.btn_w + 2, s.caption],
            "smcaption": [size(s.caption - 6), size(s.caption - 6)],
            "menu": [size(s.menu), size(s.menu)],
            "scroll": [size(s.scroll), size(s.scroll)],
            "icon": [s.icon_h, s.icon_v],
        },
        "fonts": {"caption": f(s.caption_font), "smcaption": f(s.caption_font), "menu": f(s.ui_font),
                  "status": f(s.ui_font), "message": f(s.ui_font), "icon": f(s.ui_font)},
        "colors": {k: list(v) for k, v in s.colors.items()},
    }


def user_scheme_path(name: str) -> str | None:
    """Path of a user-saved scheme, or None if it's a stock XP one."""
    slug = "".join(ch if ch.isalnum() else "-" for ch in name).strip("-").lower()
    path = os.path.join(USER_SCHEMES_DIR, f"{slug}.json")
    if not os.path.exists(path):
        return None
    with open(path, encoding="utf-8") as fh:
        return path if name in json.load(fh) else None


def save_scheme(s: Scheme, name: str) -> str:
    """Write an edited scheme to schemes/user/<slug>.json (merged on load)."""
    os.makedirs(USER_SCHEMES_DIR, exist_ok=True)
    slug = "".join(ch if ch.isalnum() else "-" for ch in name).strip("-").lower() or "scheme"
    path = os.path.join(USER_SCHEMES_DIR, f"{slug}.json")
    existing = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as fh:
            existing = json.load(fh)
    existing[name] = scheme_to_entry(s)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(existing, fh, indent=2)
    return path


def _logfont_a(buf: bytes, off: int) -> Font:
    h, _w, _e, _o, weight = struct.unpack_from("<iiiii", buf, off)
    face = buf[off + 28:off + 60].split(b"\0")[0].decode("latin1") or "Tahoma"
    return (face, round(-h * 72 / 96) if h < 0 else max(6, h), weight >= 600)


def _scheme_from_entry(name: str, entry: dict) -> Scheme:
    scheme = Scheme()
    scheme.name = name
    scheme.colors = {k: tuple(v) for k, v in entry["colors"].items()}
    cf, mf = entry["fonts"]["caption"], entry["fonts"]["menu"]
    _metrics_from(scheme, entry["metrics"]["border"], *entry["metrics"]["caption"],
                  (cf["face"], cf["pt"], cf["bold"]), (mf["face"], mf["pt"], mf["bold"]))
    m = entry.get("metrics", {})
    scheme.menu = m.get("menu", [18, 18])[1]
    scheme.scroll = m.get("scroll", [16, 16])[0]
    scheme.icon_h, scheme.icon_v = m.get("icon", [75, 75])[:2]
    return scheme


def applied_scheme_path() -> str:
    return os.path.join(USER_SCHEMES_DIR, ".applied.json")


def save_applied(s: Scheme) -> str:
    """Remember what is currently on the desktop. Kept in a dotfile so it never
    shows up as a pickable scheme -- it has no name of its own."""
    os.makedirs(USER_SCHEMES_DIR, exist_ok=True)
    with open(applied_scheme_path(), "w", encoding="utf-8") as f:
        json.dump(scheme_to_entry(s), f, indent=2)
    return applied_scheme_path()


def load_applied() -> Scheme | None:
    path = applied_scheme_path()
    if not os.path.exists(path):
        return None
    try:
        with open(path, encoding="utf-8") as f:
            return _scheme_from_entry("", json.load(f))
    except (ValueError, KeyError, OSError):
        return None


def load_scheme(theme_file: str | None = None, scheme_name: str | None = None) -> Scheme:
    scheme = Scheme()
    if scheme_name:
        data = _scheme_table()
        match = next((k for k in data if k.lower() == scheme_name.lower()), None)
        if match is None:
            raise ValueError(f"unknown scheme {scheme_name!r}; try one of: {', '.join(sorted(data))}")
        return _scheme_from_entry(match, data[match])
    if theme_file:
        sections = load_theme_file(theme_file)
        scheme.name = os.path.splitext(os.path.basename(theme_file))[0]
        for k, v in sections.get("control panel\\colors", {}).items():
            try:
                r, g, b = [int(p) for p in v.split()[:3]]
                scheme.colors[k.lower()] = (r, g, b)
            except ValueError:
                pass
        ncm = sections.get("metrics", {}).get("nonclientmetrics")
        if ncm:
            try:
                buf = bytes(int(x) for x in ncm.split())
                border, _sw, _sh, cap_w, cap_h = struct.unpack_from("<iiiii", buf, 4)
                _metrics_from(scheme, border, cap_w, cap_h, _logfont_a(buf, 24), _logfont_a(buf, 160))
                scheme.scroll = _sw
                scheme.menu = struct.unpack_from("<i", buf, 156)[0]
            except (ValueError, struct.error):
                pass
        icon_metrics = sections.get("metrics", {}).get("iconmetrics")
        if icon_metrics:
            try:
                buf = bytes(int(x) for x in icon_metrics.split())
                scheme.icon_h, scheme.icon_v = struct.unpack_from("<ii", buf, 4)
            except (ValueError, struct.error):
                pass
    return scheme


def theme_sections(scheme: Scheme) -> dict:
    """Serialize XP's native NONCLIENTMETRICSA/ICONMETRICSA and colors."""
    def logfont(font):
        face, points, bold = font
        name = face.encode("latin1", errors="replace")[:31].ljust(32, b"\0")
        return struct.pack("<iiiii8B", -round(points * 96 / 72), 0, 0, 0,
                           700 if bold else 400, 0, 0, 0, 1, 0, 0, 0, 0) + name
    ui = logfont(scheme.ui_font)
    caption = logfont(scheme.caption_font)
    ncm = (struct.pack("<6i", 340, scheme.frame - 3, scheme.scroll, scheme.scroll,
                       scheme.btn_w + 2, scheme.caption) + caption
           + struct.pack("<2i", scheme.btn_w + 2, scheme.caption) + caption
           + struct.pack("<2i", scheme.menu, scheme.menu) + ui + ui + ui)
    icon = struct.pack("<4i", 76, scheme.icon_h, scheme.icon_v, 1) + ui
    return {"control panel\\colors": {k: " ".join(map(str, v)) for k, v in scheme.colors.items()},
            "metrics": {"nonclientmetrics": " ".join(map(str, ncm)),
                        "iconmetrics": " ".join(map(str, icon))}}


def scheme_wallpaper_color(scheme: Scheme) -> tuple[int, int, int]:
    return scheme.c("background")


# --- fonts -----------------------------------------------------------------

def _ttf_for(font: Font) -> str | None:
    family, _pt, bold = font
    pattern = f"{family}:{'bold' if bold else 'regular'}"
    try:
        path = host.run(["fc-match", pattern, "-f", "%{file}"],
                        capture=True, check=True).stdout.strip()
    except host.HostCommandError:
        path = ""
    if path and host.exists(path):
        return path
    fallback = os.path.expanduser("~/.local/share/fonts/xp/tahomabd.ttf" if bold else "~/.local/share/fonts/xp/tahoma.ttf")
    return fallback if os.path.exists(fallback) else None


def _pil_font(font: Font, px: int | None = None):
    path = _ttf_for(font)
    size = px if px is not None else round(font[1] * 96 / 72)
    return ImageFont.truetype(path, size) if path else ImageFont.load_default()


def _glyph_font(size: int):
    # Marlett is a Microsoft "symbol" font: its cmap is the (3,0) symbol
    # table, so FreeType needs the symbol encoding to find 'r', '0', '1', '2'.
    return ImageFont.truetype(MARLETT, size, encoding="symb") if os.path.exists(MARLETT) else None


# --- drawing helpers -------------------------------------------------------

def _rgba(c):
    return (*c, 255)


def _edge(d: ImageDraw.ImageDraw, box, outer_tl, outer_br, inner_tl, inner_br):
    x0, y0, x1, y1 = box  # inclusive
    d.line([(x0, y0), (x1, y0)], fill=_rgba(outer_tl)); d.line([(x0, y0), (x0, y1)], fill=_rgba(outer_tl))
    d.line([(x0, y1), (x1, y1)], fill=_rgba(outer_br)); d.line([(x1, y0), (x1, y1)], fill=_rgba(outer_br))
    d.line([(x0 + 1, y0 + 1), (x1 - 1, y0 + 1)], fill=_rgba(inner_tl)); d.line([(x0 + 1, y0 + 1), (x0 + 1, y1 - 1)], fill=_rgba(inner_tl))
    d.line([(x0 + 1, y1 - 1), (x1 - 1, y1 - 1)], fill=_rgba(inner_br)); d.line([(x1 - 1, y0 + 1), (x1 - 1, y1 - 1)], fill=_rgba(inner_br))


def window_edge(d, box, s: Scheme):        # DrawEdge(EDGE_RAISED) as used for the window frame
    _edge(d, box, s.c("buttonlight"), s.c("buttondkshadow"), s.c("buttonhilight"), s.c("buttonshadow"))


def button_edge(d, box, s: Scheme):        # push button, raised
    _edge(d, box, s.c("buttonhilight"), s.c("buttondkshadow"), s.c("buttonlight"), s.c("buttonshadow"))


def button_edge_sunken(d, box, s: Scheme):
    _edge(d, box, s.c("buttonshadow"), s.c("buttonhilight"), s.c("buttondkshadow"), s.c("buttonlight"))


def _gradient(w, h, c0, c1) -> Image.Image:
    img = Image.new("RGBA", (w, h))
    px = img.load()
    for x in range(w):
        t = x / max(1, w - 1)
        col = tuple(round(c0[i] + (c1[i] - c0[i]) * t) for i in range(3)) + (255,)
        for y in range(h):
            px[x, y] = col
    return img


def _caption_colors(s: Scheme, active: bool):
    if active:
        return s.c("activetitle"), s.c("gradientactivetitle")
    return s.c("inactivetitle"), s.c("gradientinactivetitle")


def _reference_frame(s: Scheme, active: bool, size: int = 96) -> Image.Image:
    """A complete classic window frame drawn on a small canvas; corners and
    edges are cropped out of it so their bevel joins are right."""
    img = Image.new("RGBA", (size, size), _rgba(s.c("buttonface")))
    d = ImageDraw.Draw(img)
    window_edge(d, (0, 0, size - 1, size - 1), s)
    img.alpha_composite(_gradient(size - 2 * s.frame, s.caption, *_caption_colors(s, active)), (s.frame, s.frame))
    return img


def _stamp_glyph(img: Image.Image, glyph: str, font, xy, color) -> None:
    """Draw a Marlett glyph without antialiasing, the way GDI did."""
    mask = Image.new("1", img.size, 0)
    ImageDraw.Draw(mask).text(xy, glyph, font=font, fill=1)
    img.paste(_rgba(color), (0, 0), mask)


# --- Aurorae decoration ----------------------------------------------------

def build_decoration(s: Scheme) -> tuple[str, dict]:
    F, top_h = s.frame, s.frame + s.caption
    elements = []
    for prefix, active in (("decoration", True), ("decoration-inactive", False)):
        ref = _reference_frame(s, active)
        n = ref.width
        top = Image.new("RGBA", (1024, top_h), _rgba(s.c("buttonface")))
        d = ImageDraw.Draw(top)
        d.line([(0, 0), (1023, 0)], fill=_rgba(s.c("buttonlight")))
        d.line([(0, 1), (1023, 1)], fill=_rgba(s.c("buttonhilight")))
        top.alpha_composite(_gradient(1024, s.caption, *_caption_colors(s, active)), (0, F))
        elements += [
            _svg_image_element(f"{prefix}-topleft", ref.crop((0, 0, F, top_h))),
            _svg_image_element(f"{prefix}-top", top),
            _svg_image_element(f"{prefix}-topright", ref.crop((n - F, 0, n, top_h))),
            _svg_image_element(f"{prefix}-left", ref.crop((0, top_h, F, top_h + 1)).resize((F, 2048))),
            _svg_image_element(f"{prefix}-right", ref.crop((n - F, top_h, n, top_h + 1)).resize((F, 2048))),
            _svg_image_element(f"{prefix}-bottom", ref.crop((F, n - F, F + 1, n)).resize((2048, F))),
            _svg_image_element(f"{prefix}-bottomleft", ref.crop((0, n - F, F, n))),
            _svg_image_element(f"{prefix}-bottomright", ref.crop((n - F, n - F, n, n))),
            _svg_image_element(f"{prefix}-center", _solid(1, 1, (0, 0, 0, 0))),
            _svg_image_element(f"{prefix}-hint-stretch-borders", _solid(1, 1, (0, 0, 0, 0))),
        ]
    for prefix, active in (("decoration-maximized", True), ("decoration-maximized-inactive", False)):
        elements.append(_svg_image_element(f"{prefix}-center", _gradient(1024, s.caption, *_caption_colors(s, active))))
    svg = _wrap_svg(elements, 1024, top_h)

    tt, it = s.c("titletext"), s.c("inactivetitletext")
    pad_top = (s.caption - s.btn_h) // 2
    pad_bottom = s.caption - s.btn_h - pad_top
    rc = {
        "General": {
            "TitleAlignment": "Left", "LeftButtons": "M", "RightButtons": "IAX", "Shadow": "false",
            "ActiveTextColor": f"{tt[0]},{tt[1]},{tt[2]},255",
            "InactiveTextColor": f"{it[0]},{it[1]},{it[2]},255",
            "UseTextShadow": "false",
        },
        "Layout": {
            "BorderLeft": F, "BorderRight": F, "BorderBottom": F, "BorderTop": 0,
            "TitleEdgeTop": F + pad_top, "TitleEdgeBottom": pad_bottom,
            "TitleEdgeLeft": F + 2, "TitleEdgeRight": F + 2,
            "TitleEdgeTopMaximized": pad_top, "TitleEdgeBottomMaximized": pad_bottom,
            "TitleEdgeLeftMaximized": 2, "TitleEdgeRightMaximized": 2,
            "TitleBorderLeft": 3, "TitleBorderRight": 3,
            "TitleHeight": s.btn_h, "ButtonWidth": s.btn_w, "ButtonHeight": s.btn_h,
            "ButtonSpacing": 2, "ButtonMarginTop": 0, "ExplicitButtonSpacer": 2,
        },
    }
    return svg, rc


def build_button(s: Scheme, glyph: str) -> str:
    gsize = max(8, s.btn_h - 4)
    font = _glyph_font(gsize)
    elements = []
    for state in ("active", "inactive", "hover", "hover-inactive", "pressed", "pressed-inactive",
                  "deactivated", "deactivated-inactive"):
        img = Image.new("RGBA", (s.btn_w, s.btn_h), _rgba(s.c("buttonface")))
        d = ImageDraw.Draw(img)
        pressed = state.startswith("pressed")
        (button_edge_sunken if pressed else button_edge)(d, (0, 0, s.btn_w - 1, s.btn_h - 1), s)
        if font:
            gx = (s.btn_w - gsize) // 2 + (1 if pressed else 0)
            gy = (s.btn_h - gsize) // 2 + (1 if pressed else 0)
            if state.startswith("deactivated"):
                _stamp_glyph(img, glyph, font, (gx + 1, gy + 1), s.c("buttonhilight"))
                _stamp_glyph(img, glyph, font, (gx, gy), s.c("buttonshadow"))
            else:
                _stamp_glyph(img, glyph, font, (gx, gy), s.c("buttontext"))
        elements.append(_svg_image_element(f"{state}-center", img))
    return _wrap_svg(elements, s.btn_w, s.btn_h)


def write_aurorae_package(s: Scheme, out_dir: str, name: str) -> str:
    plugin_id = "".join(c if c.isalnum() else "" for c in name) or "WindowsClassic"
    pkg = os.path.join(out_dir, plugin_id)
    os.makedirs(pkg, exist_ok=True)
    svg, rc = build_decoration(s)
    with open(os.path.join(pkg, "decoration.svg"), "w", encoding="utf-8") as f:
        f.write(svg)
    for button, glyph in GLYPHS.items():
        with open(os.path.join(pkg, f"{button}.svg"), "w", encoding="utf-8") as f:
            f.write(build_button(s, glyph))
    with open(os.path.join(pkg, "metadata.desktop"), "w", encoding="utf-8") as f:
        f.write(METADATA_DESKTOP.format(name=name, plugin_id=plugin_id))
    meta = json.loads(json.dumps(METADATA_JSON))
    meta["KPlugin"]["Id"], meta["KPlugin"]["Name"] = plugin_id, name
    with open(os.path.join(pkg, "metadata.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)
    lines = []
    for section, keys in rc.items():
        lines += [f"[{section}]"] + [f"{k}={v}" for k, v in keys.items()] + [""]
    with open(os.path.join(pkg, f"{plugin_id}rc"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    return pkg


# --- colour schemes --------------------------------------------------------

def _kcolor(s: Scheme, key: str) -> str:
    return ",".join(str(v) for v in s.c(key))


def _color_groups(s: Scheme, groups: dict[str, tuple[str, str]]) -> list[str]:
    out = []
    for g, (bg, fg) in groups.items():
        out.append(f"[Colors:{g}]\nBackgroundNormal={_kcolor(s, bg)}\nBackgroundAlternate={_kcolor(s, bg)}\n"
                   f"ForegroundNormal={_kcolor(s, fg)}\nForegroundActive={_kcolor(s, fg)}\n"
                   f"ForegroundInactive={_kcolor(s, 'graytext')}\nDecorationFocus={_kcolor(s, 'hilight')}\n"
                   f"DecorationHover={_kcolor(s, 'hilight')}\n")
    out.append(f"[Colors:Selection]\nBackgroundNormal={_kcolor(s, 'hilight')}\nForegroundNormal={_kcolor(s, 'hilighttext')}\n")
    out.append(f"[Colors:Tooltip]\nBackgroundNormal={_kcolor(s, 'infowindow')}\nForegroundNormal={_kcolor(s, 'infotext')}\n")
    return out


def build_colorscheme(s: Scheme, name: str) -> str:
    out = _color_groups(s, {
        "Window": ("buttonface", "buttontext"), "Button": ("buttonface", "buttontext"),
        "View": ("window", "windowtext"), "Header": ("buttonface", "buttontext"),
        "Complementary": ("buttonface", "buttontext"),
    })
    out.append(f"[General]\nColorScheme={name}\nName={name}\nshadeSortColumn=true\n")
    out.append(f"[WM]\nactiveBackground={_kcolor(s, 'activetitle')}\nactiveForeground={_kcolor(s, 'titletext')}\n"
               f"inactiveBackground={_kcolor(s, 'inactivetitle')}\ninactiveForeground={_kcolor(s, 'inactivetitletext')}\n")
    return "\n".join(out)


# --- panel theme -----------------------------------------------------------

def _tasks_frame(s: Scheme, kind: str) -> Image.Image:
    img = Image.new("RGBA", (12, 12), _rgba(s.c("buttonface")))
    d = ImageDraw.Draw(img)
    if kind == "focus":
        button_edge_sunken(d, (0, 0, 11, 11), s)
        for y in range(2, 10):          # classic "checked" buttons get a hilight/face dither
            for x in range(2, 10):
                if (x + y) % 2 == 0:
                    img.putpixel((x, y), _rgba(s.c("buttonhilight")))
    elif kind == "attention":
        button_edge(d, (0, 0, 11, 11), s)
        d.rectangle((2, 2, 9, 9), fill=_rgba(s.c("hilight")))
    else:
        button_edge(d, (0, 0, 11, 11), s)
    return img


def build_panel_theme(s: Scheme, out_dir: str, name: str) -> tuple[str, int]:
    from plasmatheme_gen import _emit_frame, _nine_slice, PANEL_PADDING, TASK_PADDING, LOCATION_PREFIXES
    plugin_id = "".join(ch if ch.isalnum() else "" for ch in name).lower() + "-panel"
    pkg = os.path.join(out_dir, plugin_id)
    for sub in ("widgets", os.path.join("solid", "widgets")):
        os.makedirs(os.path.join(pkg, sub), exist_ok=True)

    bar_h = s.bar_height
    bar = Image.new("RGBA", (8, bar_h), _rgba(s.c("buttonface")))
    d = ImageDraw.Draw(bar)
    d.line([(0, 0), (7, 0)], fill=_rgba(s.c("buttonlight")))
    d.line([(0, 1), (7, 1)], fill=_rgba(s.c("buttonhilight")))
    pieces = _nine_slice(bar, 0, 0, 2, 1)
    elements = []
    for prefix in LOCATION_PREFIXES:
        _emit_frame(elements, prefix, pieces, PANEL_PADDING, stretch_horizontal=True)
    panel_svg = _wrap_svg(elements, 64, bar_h)
    for sub in ("widgets", os.path.join("solid", "widgets")):
        with open(os.path.join(pkg, sub, "panel-background.svg"), "w", encoding="utf-8") as f:
            f.write(panel_svg)

    elements = []
    for state, kind in (("normal", "normal"), ("hover", "normal"), ("minimized", "normal"),
                        ("focus", "focus"), ("attention", "attention")):
        pieces = _nine_slice(_tasks_frame(s, kind), 2, 2, 2, 2)
        for prefix in LOCATION_PREFIXES:
            _emit_frame(elements, f"{prefix}{state}-", pieces, TASK_PADDING, stretch_horizontal=False)
            if kind == "focus":
                elements.append(_svg_image_element(f"{prefix}{state}-hint-tile-center", _solid(1, 1, (0, 0, 0, 0))))
    with open(os.path.join(pkg, "widgets", "tasks.svg"), "w", encoding="utf-8") as f:
        f.write(_wrap_svg(elements, 64, 24))

    colors = _color_groups(s, {g: ("buttonface", "buttontext") for g in ("Window", "Button", "View", "Header", "Complementary")})
    colors.append(f"[General]\nName={name}\nColorScheme={name}\n")
    with open(os.path.join(pkg, "colors"), "w", encoding="utf-8") as f:
        f.write("\n".join(colors))

    meta = {"KPlugin": {"Id": plugin_id, "Name": f"{name} (taskbar)", "Description": "Windows Classic taskbar by XPLasma",
                        "Authors": [{"Name": "XPLasma"}], "Version": "1.0", "License": "Unknown"},
            "X-Plasma-API": "5.0"}
    with open(os.path.join(pkg, "metadata.json"), "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=2)
    return pkg, bar_h


# --- Start button ----------------------------------------------------------

def build_start_button_pngs(s: Scheme, out_dir: str, base_name: str, left_pad: int | None = None) -> str:
    """`left_pad` is transparent padding on the left of the image. The Start
    applet overhangs the panel's left margin so a rounded XP pill can sit flush
    with the screen edge; the classic button is a square bevel that XP insets a
    couple of pixels instead, so it pads by the overhang plus that gap -- or
    its raised left edge would be pulled off-screen."""
    from plasmatheme_gen import START_OVERHANG_LEFT
    from xp_assets import start_flag

    if left_pad is None:
        left_pad = START_OVERHANG_LEFT + 2
    bar_h = s.bar_height
    h = bar_h - 6
    inset = (bar_h - h) // 2
    flag = start_flag()
    if flag is not None:
        flag = flag.convert("RGBA")
        fh = min(16, h - 6)
        flag = flag.resize((round(flag.width * fh / flag.height), fh), Image.LANCZOS)
    font = _pil_font((s.caption_font[0], s.caption_font[1], True))
    label = "start"
    label_w = int(font.getlength(label)) if hasattr(font, "getlength") else 30
    label_h = font.getbbox(label)[3] if hasattr(font, "getbbox") else 12
    w = 6 + (flag.width + 4 if flag is not None else 0) + label_w + 8
    rendered = {}
    for state in ("normal", "hover", "pressed"):
        btn = Image.new("RGBA", (w, h), _rgba(s.c("buttonface")))
        d = ImageDraw.Draw(btn)
        off = 1 if state == "pressed" else 0
        (button_edge_sunken if state == "pressed" else button_edge)(d, (0, 0, w - 1, h - 1), s)
        x = 6 + off
        if flag is not None:
            btn.alpha_composite(flag, (x, (h - flag.height) // 2 + off))
            x += flag.width + 4
        d.text((x, (h - label_h) // 2 - 1 + off), label, font=font, fill=_rgba(s.c("buttontext")))
        padded = Image.new("RGBA", (w + left_pad, bar_h), (0, 0, 0, 0))
        padded.alpha_composite(btn, (left_pad, inset))
        rendered[state] = padded
    os.makedirs(out_dir, exist_ok=True)
    digest = hashlib.sha1(b"".join(im.tobytes() for im in rendered.values())).hexdigest()[:8]
    paths = {}
    for state, im in rendered.items():
        paths[state] = os.path.join(out_dir, f"{base_name}-{digest}-{state}.png")
        im.save(paths[state])
    return paths["normal"]
