"""Index every theme xp2plasma can apply, and resolve one into the pieces a
preview needs in order to draw it.

Two kinds of input exist and they map onto XP's own Appearance tab:

  * the Windows Classic *style*, whose "colour scheme" list is XP's 36 stock
    schemes and whose "font size" list is the (large)/(extra large) variants
    those schemes already ship as;
  * a *visual style* (an .msstyles), whose colour and size variants are its
    own INI resources -- XP names them <SIZE><COLOUR>_INI, e.g. NORMALBLUE_INI,
    LARGEFONTSHOMESTEAD_INI, EXTRALARGEROYALE_INI. MsStyle.load already accepts
    one by name, so a variant is just a different INI resource.
"""
from __future__ import annotations

import json
import os
import re
from dataclasses import dataclass, field

from PIL import Image

import paths
from msstyles_parse import MsStyle, _parse_ini_text, parse_color, parse_margins
from theme_file import (find_theme_file_for, load_theme_file, theme_msstyles_path,
                        theme_wallpaper_path)

CLASSIC_LABEL = "Windows Classic style"

# INI resource size prefixes -> what XP calls the font size, and the suffix the
# equivalent stock classic scheme carries.
SIZES: tuple[tuple[str, str, str], ...] = (
    ("NORMAL", "Normal", ""),
    ("LARGEFONTS", "Large fonts", " (large)"),
    ("EXTRALARGE", "Extra large fonts", " (extra large)"),
)

# XP's own names for the Luna-era variants; anything else is prettified.
COLOUR_LABELS = {"HOMESTEAD": "Olive Green", "METALLIC": "Silver", "DARKROYALE": "Royale Noir"}


def _pretty(token: str) -> str:
    """ROYALE -> Royale; DARKROYALE -> Dark Royale."""
    if token.isupper():
        return token.capitalize()
    words = re.findall(r"[A-Z][a-z0-9]*|[A-Z]+(?![a-z])", token) or [token]
    return " ".join(w.capitalize() for w in words)


def _split_ini(name: str) -> tuple[str, str] | None:
    """'NORMALBLUE_INI' -> ('NORMAL', 'BLUE'); None if it isn't a variant."""
    if not name.endswith("_INI"):
        return None
    stem = name[:-4]
    for key, _label, _suffix in SIZES:
        if stem.startswith(key) and len(stem) > len(key):
            return key, stem[len(key):]
    return None


@dataclass(frozen=True)
class FontSizeOption:
    key: str      # "NORMAL" | "LARGEFONTS" | "EXTRALARGE"
    label: str    # XP's wording


@dataclass(frozen=True)
class ColourOption:
    label: str    # "Blue", "Olive Green", "Windows Standard"
    key: str      # colour token for a visual style; base scheme name for classic


@dataclass(frozen=True, eq=False)
class VisualStyle:
    """One entry of XP's "Windows and buttons" list."""
    label: str
    path: str | None                              # None => Windows Classic style
    variants: tuple[tuple[str, str], ...] = ()    # (size_key, colour_token)
    meta: dict = field(default_factory=dict)      # style_meta(); names + labels

    @property
    def is_classic(self) -> bool:
        return self.path is None

    def colours(self) -> list[ColourOption]:
        if self.is_classic:
            return [ColourOption(name, name) for name in classic_base_schemes()]
        # XP lists colour schemes in the style's own order (COLORNAMES), and
        # calls them what THEMES_INI says: "Default (blue)", not "Blue".
        info = self.meta.get("colours", {})
        order = list(self.meta.get("colour_order") or [])
        for _size, colour in self.variants:
            if colour not in order:
                order.append(colour)
        return [ColourOption(info.get(c, {}).get("label") or COLOUR_LABELS.get(c) or _pretty(c), c)
                for c in order]

    def sizes(self, colour_key: str | None = None) -> list[FontSizeOption]:
        """Only the sizes that actually exist for this colour -- most stock
        schemes ship Normal alone, so offering all three would be a lie."""
        out: list[FontSizeOption] = []
        info = self.meta.get("sizes", {})
        for key, label, _suffix in SIZES:
            if colour_key is None:
                ok = True
            elif self.is_classic:
                ok = classic_size_available(colour_key, key)
            else:
                ok = any(c == colour_key and s == key for s, c in self.variants)
            if ok:
                out.append(FontSizeOption(key, info.get(key, {}).get("label") or label))
        return out

    def colour_name(self, colour_key: str) -> str:
        """The name a .theme bundle's ColorStyle= uses for this colour."""
        return self.meta.get("colours", {}).get(colour_key, {}).get("name") or colour_key

    def size_name(self, size_key: str) -> str:
        """The name a .theme bundle's Size= uses for this size."""
        return self.meta.get("sizes", {}).get(size_key, {}).get("name") or size_key

    def resolve(self, colour_key: str, size_key: str) -> str:
        """-> the classic scheme name, or the visual style's INI resource."""
        if self.is_classic:
            suffix = next(s for k, _l, s in SIZES if k == size_key)
            return colour_key + suffix
        return f"{size_key}{colour_key}_INI"

    def supports(self, colour_key: str, size_key: str) -> bool:
        return any(s.key == size_key for s in self.sizes(colour_key))


# --- discovery --------------------------------------------------------------

# Enumerating a style's variants means opening its .msstyles, and the whole
# collection is about a gigabyte across ~130 files. The answer never changes
# unless the file does, so it is cached on disk against mtime + size; the first
# run pays for the parse and every run after it is instant.
_CACHE_PATH = paths.INDEX_CACHE


def _read_variant_cache() -> dict:
    try:
        with open(_CACHE_PATH, encoding="utf-8") as fh:
            data = json.load(fh)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


_variant_cache: dict = _read_variant_cache()
_variant_cache_dirty = False


def _themes_ini_labels(text: str | None) -> dict:
    """What XP shows for a style and its variants, from THEMES_INI:
    [documentation] DisplayName, [ColorScheme.X] DisplayName, [Size.X] DisplayName."""
    out = {"display": "", "colour": {}, "size": {}}
    if not text:
        return out
    try:
        sections = _parse_ini_text(text)
    except Exception:
        return out
    for (cls, part, _state), values in sections.items():
        name = values.get("displayname", "").strip()
        if not name:
            continue
        if cls == "documentation" and not part:
            out["display"] = name
        elif cls == "colorscheme" and part:
            out["colour"][part] = name
        elif cls == "size" and part:
            out["size"][part] = name
    return out


def _meta_from_table(table: dict) -> dict:
    """Turn the style's resource tables into the drop-down model.

    FILERESNAMES is colour-major: entry [ci * n_sizes + si] is the INI for
    COLORNAMES[ci] at SIZENAMES[si]. That is the only place the .theme-side
    scheme name ("NormalColor") is tied to the resource-side token ("BLUE"),
    and it is what makes a bundle's ColorStyle= resolvable exactly.
    """
    ini = set(table.get("ini_names") or ())
    variants = sorted(v for v in (_split_ini(n) for n in ini) if v)
    labels = _themes_ini_labels(table.get("themes_ini"))
    colours: dict[str, dict] = {}
    sizes: dict[str, dict] = {}
    order: list[str] = []
    cn, sn, fn = table.get("colour_names") or [], table.get("size_names") or [], table.get("file_names") or []
    if cn and sn and len(fn) >= len(cn) * len(sn):
        for ci, cname in enumerate(cn):
            for si, sname in enumerate(sn):
                f = fn[ci * len(sn) + si].upper()
                split = _split_ini(f)
                if not split or f not in ini:
                    continue
                size_key, token = split
                if token not in colours:
                    colours[token] = {"name": cname,
                                      "label": labels["colour"].get(cname.lower())
                                      or COLOUR_LABELS.get(token) or _pretty(token)}
                    order.append(token)
                if size_key not in sizes:
                    sizes[size_key] = {"name": sname,
                                       "label": labels["size"].get(sname.lower())
                                       or next(l for k, l, _s in SIZES if k == size_key)}
    for _size, token in variants:
        if token not in colours:
            colours[token] = {"name": token, "label": COLOUR_LABELS.get(token) or _pretty(token)}
            order.append(token)
    return {"variants": [list(v) for v in variants], "display": labels["display"],
            "colours": colours, "colour_order": order, "sizes": sizes}


def style_meta(path: str) -> dict:
    """Everything the Appearance tab needs to know about an .msstyles without
    decoding its layout: variants, XP's display names, the bundle-side scheme
    names. Cached on disk; an empty 'variants' list means unreadable."""
    global _variant_cache_dirty
    empty = {"variants": [], "display": "", "colours": {}, "colour_order": [], "sizes": {}}
    try:
        st = os.stat(path)
    except OSError:
        return empty
    key = os.path.abspath(path)
    hit = _variant_cache.get(key)
    if hit and hit.get("mtime") == st.st_mtime and hit.get("size") == st.st_size and "meta" in hit:
        return hit["meta"]
    try:
        meta = _meta_from_table(MsStyle.variant_table(path))
    except Exception:
        meta = empty
    _variant_cache[key] = {"mtime": st.st_mtime, "size": st.st_size, "meta": meta}
    _variant_cache_dirty = True
    return meta


def style_variants(path: str) -> tuple[tuple[str, str], ...]:
    """The (size_key, colour_token) pairs an .msstyles ships, which is what
    XP's "Color scheme" and "Font size" drop-downs are built from."""
    return tuple(tuple(v) for v in style_meta(path)["variants"])


def save_variant_cache() -> None:
    """Persist what this run had to parse. Failure is never fatal: the cache is
    an optimisation, and the next run just parses again."""
    global _variant_cache_dirty
    if not _variant_cache_dirty:
        return
    try:
        os.makedirs(os.path.dirname(_CACHE_PATH), exist_ok=True)
        tmp = f"{_CACHE_PATH}.tmp"
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(_variant_cache, fh)
        os.replace(tmp, _CACHE_PATH)
        _variant_cache_dirty = False
    except Exception:
        pass


def classic_base_schemes() -> list[str]:
    """The stock schemes that aren't a size variant of another one."""
    import classic_gen as cg
    names = cg.list_schemes()
    return [n for n in names if not re.search(r"\((large|extra large)\)$", n)]


def classic_split(name: str) -> tuple[str, str]:
    """'Windows Standard (large)' -> ('Windows Standard', 'LARGEFONTS').

    A base that only exists in a size variant ("Pumpkin (large)" has no plain
    "Pumpkin") comes back whole with size NORMAL, so picking it still resolves
    to itself."""
    import classic_gen as cg
    for key, _label, suffix in SIZES:
        if suffix and name.endswith(suffix):
            base = name[: -len(suffix)]
            if base in cg.list_schemes():
                return base, key
            return name, "NORMAL"
    return name, "NORMAL"


def classic_size_available(base: str, size_key: str) -> bool:
    import classic_gen as cg
    suffix = next(s for k, _l, s in SIZES if k == size_key)
    if not suffix:
        return True
    names = cg.list_schemes()
    return (base + suffix) in names


def _iter_files() -> list[str]:
    out: list[str] = []
    for root in paths.theme_roots():
        if not os.path.isdir(root):
            continue
        for dirpath, _dirs, files in os.walk(root):
            for fn in files:
                if fn.lower().endswith((".theme", ".msstyles")):
                    out.append(os.path.join(dirpath, fn))
    return sorted(out)


def _official(path: str) -> bool:
    """A style from one of the official pack downloads, rather than one the
    user dropped in. Matches on the pack folder's name so it holds wherever
    the assets live -- the old in-tree samples/ tree or the asset library."""
    low = path.lower()
    return "xp_themes" in low or "iso/cache" in low


def visual_style_from(path: str) -> VisualStyle | None:
    """Build a VisualStyle from an .msstyles, or from the .msstyles a .theme
    bundle points at. None if it isn't a style we can enumerate."""
    if not path:
        return None
    if path.lower().endswith(".theme"):
        path = theme_msstyles_path(path) or ""
    if not path or not os.path.exists(path):
        return None
    meta = style_meta(path)
    variants = tuple(tuple(v) for v in meta["variants"])
    if not variants:
        return None
    # XP names the style after [documentation] DisplayName ("Windows XP
    # style"); the file stem is the fallback for fan styles that left it out.
    label = meta.get("display") or os.path.splitext(os.path.basename(path))[0]
    return VisualStyle(label=label, path=path, variants=variants, meta=meta)


def list_visual_styles() -> list[VisualStyle]:
    """Every distinct .msstyles found, official ones first, then the classic.
    Two files with the same display name are told apart by their folder."""
    seen: dict[str, VisualStyle] = {}
    for path in _iter_files():
        style = visual_style_from(path)
        if style and style.path not in seen:
            seen[style.path] = style
    save_variant_cache()
    counts: dict[str, int] = {}
    for s in seen.values():
        counts[s.label.lower()] = counts.get(s.label.lower(), 0) + 1
    styles = []
    for s in seen.values():
        if counts[s.label.lower()] > 1:
            folder = os.path.basename(os.path.dirname(s.path))
            s = VisualStyle(f"{s.label} ({folder})", s.path, s.variants, s.meta)
        styles.append(s)
    styles.sort(key=lambda s: (not _official(s.path or ""), s.label.lower()))
    return [VisualStyle(CLASSIC_LABEL, None), *styles]


@dataclass(frozen=True)
class ThemeEntry:
    """A theme/style input; only .theme bundles belong in the Themes dropdown."""
    label: str
    path: str
    theme_file: str | None = None      # the bundle this came from, if any

    @property
    def kind(self) -> str:
        return "bundle" if self.theme_file else "style"

    @property
    def style_path(self) -> str | None:
        """The .msstyles this entry would be drawn from, if it has one."""
        if self.theme_file:
            return theme_msstyles_path(self.theme_file)
        return None if self.path.lower().endswith(".theme") else self.path

    @property
    def supported(self) -> bool:
        """Whether xp2plasma can actually read this theme.

        Vista/Longhorn-era .msstyles store their layout as binary VSTYLE
        records rather than plaintext INI, which this parser doesn't handle.
        They stay in the list so an unsupported style can be told apart from a
        broken one -- but say so, rather than failing only once you press
        Apply."""
        style = self.style_path
        if not style:
            return True                # a classic bundle needs no parsing
        return bool(style_variants(style))


# [VisualStyles] Size -> the INI resource size prefix. Windows spells these
# its own way; anything unrecognised means the style's default.
_BUNDLE_SIZES = {"NORMALSIZE": "NORMAL", "LARGESIZE": "LARGEFONTS",
                 "EXTRALARGESIZE": "EXTRALARGE", "EXTRALARGEFONTS": "EXTRALARGE"}


def _theme_values(path: str, section: str) -> dict[str, str]:
    try:
        sections = load_theme_file(path)
    except Exception:
        return {}
    want = section.lower()
    for name, values in sections.items():
        if name.lower() == want:
            return values
    return {}


def bundle_variant(entry: ThemeEntry, style: VisualStyle | None) -> str | None:
    """Which variant of `style` a .theme bundle asks for, as an INI resource
    name -- or None for the style's own default.

    A bundle names its variant the way the style's COLORNAMES/SIZENAMES do
    ("NormalColor", "Metallic", "NormalSize"), which FILERESNAMES ties to the
    resource token -- so with those tables the match is exact. Without them
    (some fan styles), only an unambiguous token match counts; anything else
    is the default rather than a guessed colour."""
    if style is None or style.is_classic:
        return None
    if not entry.theme_file:
        # A named bare style can contain several colours. In particular,
        # Royale Noir's first resource is blue Royale, not its namesake dark
        # variant. Honour a matching colour name without hiding other colours.
        matches = [c.key for c in style.colours() if c.label.casefold() == entry.label.casefold()]
        if len(matches) == 1 and style.supports(matches[0], "NORMAL"):
            return style.resolve(matches[0], "NORMAL")
        return None
    values = _theme_values(entry.theme_file, "VisualStyles")
    colour = (values.get("colorstyle") or "").strip().lower()
    size = (values.get("size") or "").strip().lower()
    if not colour:
        return None
    tokens = [c.key for c in style.colours()]
    token = next((t for t in tokens if style.colour_name(t).lower() == colour), None)
    if token is None:
        hits = [t for t in tokens if t.lower() == colour or t.lower().startswith(colour)]
        if not hits:
            hits = [t for t in tokens if colour.startswith(t.lower())]
        token = hits[0] if len(hits) == 1 else None
    if token is None:
        return None
    size_key = next((k for k, _l, _s in SIZES if style.size_name(k).lower() == size), None) \
        or _BUNDLE_SIZES.get(size.upper(), "NORMAL")
    for key in (size_key, "NORMAL"):
        if style.supports(token, key):
            return style.resolve(token, key)
    return None


def theme_label(path: str) -> str:
    """DisplayName belongs to the bundle; Luna is a filename, not its UI name."""
    name = _theme_values(path, "Theme").get("displayname", "").strip()
    # Stock XP uses an indirect resource string rather than a literal name.
    stock_names = {"@themeui.dll,-2017": "Windows XP",
                   "@themeui.dll,-2016": "Windows Classic"}
    if name.replace(" ", "").casefold() in stock_names:
        return stock_names[name.replace(" ", "").casefold()]
    if name and not name.startswith("@"):
        return name
    return os.path.splitext(os.path.basename(path))[0]


def entry_from_path(path: str) -> ThemeEntry:
    """One theme entry for an arbitrary file the user browsed to."""
    if path.lower().endswith(".theme"):
        return ThemeEntry(theme_label(path),
                          theme_msstyles_path(path) or path, theme_file=path)
    return ThemeEntry(os.path.splitext(os.path.basename(path))[0], path)


def list_themes() -> list[ThemeEntry]:
    """Complete .theme presets, distinct even when they share a visual style."""
    bundles: dict[str, ThemeEntry] = {}
    for path in _iter_files():
        if path.lower().endswith(".theme"):
            style = theme_msstyles_path(path)
            label = theme_label(path)
            # theme_file is set either way: a bundle is applied through its
            # bundle (wallpaper + fonts), whether or not it names an msstyles.
            entry = ThemeEntry(label, style or path, theme_file=path)
            bundles[os.path.realpath(path)] = entry
    entries = sorted(bundles.values(), key=lambda e: (not _official(e.path), e.label.lower()))
    return entries


# --- resolving a theme into something drawable ------------------------------

@dataclass
class ThemeInfo:
    """Everything a preview needs about a theme, resolved once. Built either
    from a Windows Classic scheme or from a visual style's real art, so the
    same drawing code serves both."""
    title: str
    desktop: tuple[int, int, int]
    wallpaper: Image.Image | None
    window_bg: tuple[int, int, int]
    window_text: tuple[int, int, int]
    button_face: tuple[int, int, int]
    caption_text: tuple[int, int, int]
    caption_text_inactive: tuple[int, int, int]
    caption_h: int
    frame: int
    caption_font: tuple[str, int, bool]
    ui_font: tuple[str, int, bool]
    # Windows Classic: the scheme itself, so the preview can use classic_gen's
    # own bevel/gradient/Marlett drawing rather than an approximation.
    scheme: object | None = None
    caption_shadow: tuple | None = None
    caption_shadow_inactive: tuple | None = None
    # A visual style ships caption art; Windows Classic is drawn as a gradient.
    caption_img: Image.Image | None = None
    caption_img_inactive: Image.Image | None = None
    caption_margins: tuple[int, int, int, int] = (0, 0, 0, 0)
    top_h: int = 0                 # the whole title strip, top edge included
    gradient: tuple | None = None
    gradient_inactive: tuple | None = None
    # The window frame's own art (FrameLeft/Right/Bottom), active + inactive.
    frame_left: Image.Image | None = None
    frame_left_inactive: Image.Image | None = None
    frame_right: Image.Image | None = None
    frame_right_inactive: Image.Image | None = None
    frame_bottom: Image.Image | None = None
    frame_bottom_inactive: Image.Image | None = None
    frame_bottom_margins: tuple[int, int] = (0, 0)
    frame_tile: bool = False
    # Caption buttons at their real size, close last.
    buttons: tuple = ()
    # Taskbar for the Themes preview: TaskBar.BackgroundBottom, the Start pill
    # exactly as the panel generator renders it, and the tray background.
    taskbar_img: Image.Image | None = None
    taskbar_margins: tuple[int, int, int, int] = (0, 0, 0, 0)
    start_img: Image.Image | None = None
    tray_img: Image.Image | None = None
    tray_margins: tuple[int, int, int, int] = (0, 0, 0, 0)
    clock_text: tuple[int, int, int] | None = None


def _theme_background(theme_file: str | None) -> tuple[int, int, int] | None:
    """XP keeps the flat desktop colour in [Control Panel\\Colors] Background."""
    if not theme_file or not os.path.exists(theme_file):
        return None
    try:
        sections = load_theme_file(theme_file)
    except Exception:
        return None
    for name, values in sections.items():
        if "control panel" not in name.lower():
            continue
        raw = values.get("background", "").strip()
        parts = raw.replace(",", " ").split()
        if len(parts) >= 3:
            try:
                return tuple(int(p) for p in parts[:3])
            except ValueError:
                continue
    return None


def _load_wallpaper(path: str | None) -> Image.Image | None:
    if not path or not os.path.exists(path):
        return None
    try:
        with Image.open(path) as im:
            return im.convert("RGBA")
    except Exception:
        return None


def _glyph_button(scheme, name: str, size: tuple[int, int]):
    """One classic caption button. `name` is the button ("minimize"), which
    classic_gen maps to its Marlett character -- _glyph_box wants the character."""
    import classic_gen as cg
    import preview_gen
    return preview_gen._glyph_box(scheme, cg.GLYPHS[name], size)


def info_from_scheme(scheme, title: str = "", theme_file: str | None = None) -> ThemeInfo:
    """`theme_file` is the .theme bundle the scheme came from, if any -- it is
    what carries the wallpaper and the desktop colour, which a bare scheme has
    no room for."""
    import classic_gen as cg
    btn_size = (scheme.btn_w, scheme.btn_h)
    return ThemeInfo(
        title=title or scheme.name,
        desktop=_theme_background(theme_file) or scheme.c("background"),
        wallpaper=_load_wallpaper(theme_wallpaper_path(theme_file) if theme_file else None),
        window_bg=scheme.c("window"),
        window_text=scheme.c("windowtext"),
        button_face=scheme.c("buttonface"),
        caption_text=scheme.c("titletext"),
        caption_text_inactive=scheme.c("inactivetitletext"),
        caption_h=scheme.caption,
        frame=scheme.frame,
        caption_font=scheme.caption_font,
        ui_font=scheme.ui_font,
        scheme=scheme,
        gradient=tuple(cg._caption_colors(scheme, True)),
        gradient_inactive=tuple(cg._caption_colors(scheme, False)),
        buttons=tuple(_glyph_button(scheme, g, btn_size) for g in ("minimize", "maximize", "close")),
    )


def _style_colours(theme) -> dict[str, tuple[int, int, int]]:
    """Read the colours back out of the generated Plasma colour scheme, so the
    preview can't drift from what an apply would actually install."""
    import configparser
    from colorscheme_gen import build_colorscheme
    cp = configparser.ConfigParser()
    cp.optionxform = str
    cp.read_string(build_colorscheme(theme, "preview"))

    def get(section: str, key: str, default):
        raw = cp.get(section, key, fallback="").strip()
        parts = raw.replace(",", " ").split()
        if len(parts) >= 3:
            try:
                return tuple(int(p) for p in parts[:3])
            except ValueError:
                pass
        return default

    return {
        "window_bg": get("Colors:View", "BackgroundNormal", (255, 255, 255)),
        "window_text": get("Colors:View", "ForegroundNormal", (0, 0, 0)),
        "button_face": get("Colors:Button", "BackgroundNormal", (212, 208, 200)),
    }


def _style_button(theme, part: str):
    """The normal-state image of one caption button, at its real size -- a PIL
    port of aurorae_gen.build_button_svg's slicing."""
    import aurorae_gen as ag
    eff = theme.effective("window", part)
    if "imagefile" not in eff:
        return None
    img = theme.get_background_bitmap("window", part)
    if img is None:
        return None
    n = int(eff.get("imagecount", 8))
    vertical = eff.get("imagelayout", "vertical").lower() == "vertical"
    frames = ag._slice_strip(img, n, vertical)
    if not frames:
        return None
    target = ag._button_target_size(theme)
    if frames[0].size != target and "sizingmargins" in eff:
        frames = [ag._nine_slice_resize(f.convert("RGBA"), parse_margins(eff["sizingmargins"]), *target)
                  for f in frames]
    glyph_key = eff.get("imagefile3") or eff.get("imagefile1")
    if glyph_key:
        glyph_img = theme.get_bitmap(glyph_key)
        glyphs = ag._slice_strip(glyph_img, n, vertical) if glyph_img else []
        if glyphs:
            return ag._composite_center(frames[0], glyphs[0])
    return frames[0].convert("RGBA")


def _part_frames(theme, cls: str, part: str):
    """(active, inactive, effective) for a two-state strip, or (None, None, eff)."""
    import aurorae_gen as ag
    eff = theme.effective(cls, part)
    if "imagefile" not in eff:
        return None, None, eff
    img = theme.get_background_bitmap(cls, part)
    if img is None:
        return None, None, eff
    n = max(1, int(eff.get("imagecount", 2)))
    vertical = eff.get("imagelayout", "vertical").lower() == "vertical"
    frames = ag._slice_strip(img, n, vertical)
    if not frames:
        return None, None, eff
    return frames[0].convert("RGBA"), frames[min(1, len(frames) - 1)].convert("RGBA"), eff


def _start_pill(theme, bar_h: int) -> Image.Image | None:
    """The Start button exactly as the panel generator bakes it (real flag,
    label, the theme's own pill), rendered into the preview cache."""
    try:
        from plasmatheme_gen import build_start_button_pngs
        out = paths.PREVIEW_CACHE
        path = build_start_button_pngs(theme, out, "start", bar_h)
        if path:
            with Image.open(path) as im:
                return im.convert("RGBA")
    except Exception:
        pass
    return None


def _caption_font(theme) -> tuple[str, int, bool]:
    m = theme.effective("sysmetrics")
    from fonts_gen import _parse_font_spec
    try:
        return _parse_font_spec(m["captionfont"]) if "captionfont" in m else ("Microsoft Sans Serif", 8, False)
    except (ValueError, IndexError):
        return ("Microsoft Sans Serif", 8, False)


def _ui_font(theme) -> tuple[str, int, bool]:
    m = theme.effective("sysmetrics")
    from fonts_gen import _parse_font_spec
    for key in ("menufont", "icontitlefont", "captionfont"):
        if key in m:
            try:
                return _parse_font_spec(m[key])
            except (ValueError, IndexError):
                continue
    return ("Microsoft Sans Serif", 8, False)


def info_from_style(path: str, ini_name: str | None = None,
                    theme_file: str | None = None, title: str = "") -> ThemeInfo:
    import aurorae_gen as ag
    theme = MsStyle.load(path, ini_resource_name=ini_name)
    cap_h = ag._caption_metrics(theme)[0]

    active, inactive, cap = _part_frames(theme, "window", "caption")
    margins = parse_margins(cap["sizingmargins"]) if "sizingmargins" in cap else (0, 0, 0, 0)

    left_a, left_i, left_eff = _part_frames(theme, "window", "frameleft")
    right_a, right_i, _right_eff = _part_frames(theme, "window", "frameright")
    bottom_a, bottom_i, fb = _part_frames(theme, "window", "framebottom")
    frame = left_a.width if left_a is not None else 3
    if left_a is None and "sizingmargins" in left_eff:
        frame = max(1, parse_margins(left_eff["sizingmargins"])[0])

    # Classic-style themes ship a few-pixel caption 9-slice that XP stretches
    # to CaptionBarHeight plus the frame's top edge; at-size art is used as-is.
    # Same rule as aurorae_gen.build_decoration_svg, so the preview matches.
    top_h = cap_h + frame
    if active is not None:
        if active.height >= cap_h:
            top_h = active.height
        else:
            active = ag._nine_slice_resize(active, (0, 0, margins[2], margins[3]), active.width, top_h)
            inactive = ag._nine_slice_resize(inactive, (0, 0, margins[2], margins[3]), inactive.width, top_h)

    fb_margins = (0, 0)
    if bottom_a is not None:
        l, r = parse_margins(fb["sizingmargins"])[:2] if "sizingmargins" in fb else (0, 0)
        w = bottom_a.width
        if l + r >= w or l == 0 or r == 0:
            l = r = max(1, w // 4)
        fb_margins = (l, r)

    m = theme.effective("sysmetrics")

    def sys_colour(key: str, default):
        return parse_color(m[key]) if key in m else default

    colours = _style_colours(theme)
    background = _theme_background(theme_file)
    buttons = tuple(b for b in (_style_button(theme, p) for p in ("minbutton", "maxbutton", "closebutton")) if b)

    # Themes-tab taskbar: the real art, clamped to a sane bar height.
    bar_a, _bar_i, bar_eff = _part_frames(theme, "taskbar", "backgroundbottom")
    bar_margins = parse_margins(bar_eff["sizingmargins"]) if "sizingmargins" in bar_eff else (0, 0, 0, 0)
    bar_h = bar_a.height if bar_a is not None and 16 <= bar_a.height <= 60 else 30
    tray_a, _tray_i, tray_eff = _part_frames(theme, "traynotify", "background")
    tray_margins = parse_margins(tray_eff["sizingmargins"]) if "sizingmargins" in tray_eff else (0, 0, 0, 0)
    clock = theme.effective("clock", "time")
    clock_text = parse_color(clock["textcolor"]) if "textcolor" in clock else None

    return ThemeInfo(
        title=title or os.path.splitext(os.path.basename(path))[0],
        # A visual style has no desktop colour of its own; XP takes it from the
        # bundle's [Control Panel\Colors]. Fall back to XP's default desktop.
        desktop=background or (58, 110, 165),
        wallpaper=_load_wallpaper(theme_wallpaper_path(theme_file) if theme_file else None),
        window_bg=colours["window_bg"],
        window_text=colours["window_text"],
        button_face=colours["button_face"],
        caption_text=sys_colour("captiontext", (255, 255, 255)),
        caption_text_inactive=sys_colour("inactivecaptiontext", (216, 228, 248)),
        caption_h=cap_h,
        frame=frame,
        caption_font=_caption_font(theme),
        caption_shadow=ag.caption_shadow(theme, True),
        caption_shadow_inactive=ag.caption_shadow(theme, False),
        ui_font=_ui_font(theme),
        caption_img=active,
        caption_img_inactive=inactive,
        caption_margins=margins,
        top_h=top_h,
        frame_left=left_a, frame_left_inactive=left_i,
        frame_right=right_a, frame_right_inactive=right_i,
        frame_bottom=bottom_a, frame_bottom_inactive=bottom_i,
        frame_bottom_margins=fb_margins,
        frame_tile=left_eff.get("sizingtype", "tile").lower() == "tile",
        buttons=buttons,
        taskbar_img=bar_a if bar_a is not None and bar_h == bar_a.height else None,
        taskbar_margins=bar_margins,
        start_img=_start_pill(theme, bar_h),
        tray_img=tray_a,
        tray_margins=tray_margins,
        clock_text=clock_text,
    )


def info_for_entry(entry: ThemeEntry) -> ThemeInfo:
    import classic_gen as cg
    if entry.theme_file:
        style = theme_msstyles_path(entry.theme_file)
        if style:
            ini = bundle_variant(entry, visual_style_from(style))
            return info_from_style(style, ini, entry.theme_file, entry.label)
        # A Windows Classic bundle: its whole content is the colour scheme,
        # plus the wallpaper the bundle carries.
        return info_from_scheme(cg.load_scheme(entry.theme_file), entry.label, entry.theme_file)
    return info_from_style(entry.path, bundle_variant(entry, visual_style_from(entry.path)),
                           None, entry.label)
