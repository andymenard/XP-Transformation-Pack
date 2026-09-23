#!/usr/bin/env python3
"""CLI: point at a Windows XP .msstyles file, get a matching KWin Aurorae
decoration + Plasma color scheme installed and applied on the live session."""
from __future__ import annotations

import argparse
import os
import sys

from aurorae_gen import generate_theme_package
from colorscheme_gen import build_colorscheme
from fonts_gen import apply_font_specs, apply_fonts
from msstyles_parse import MsStyle
import paths
import xp_icons
from panel_layout import apply_xp_layout, set_start_icon
from plasmatheme_gen import PANEL_PADDING, START_OVERHANG_LEFT, build_start_button_pngs, generate_plasma_theme
from start_applet import APPLET_ID as START_APPLET_ID, install_start_applet
from theme_file import (
    FILL_CENTER, FILL_CROP, FILL_FIT, FILL_STRETCH, FILL_TILE,
    find_theme_file_for, theme_msstyles_path, theme_wallpaper_fill_mode, theme_wallpaper_path,
)
import applied_state
import classic_gen
import setup_assets
import widget_style
from kvantum_gen import generate_kvantum_theme

STOCK_CLASSIC_THEME = None   # replaced by paths.stock_classic_theme(); see run_classic


LIVE_THEME_NAME = "Windows Classic"   # stable id, so live re-applies just reload
UNSET = object()                       # "caller has no opinion" sentinel


def apply_scheme_live(scheme, name: str, *, out_dir: str | None = None, theme_file: str | None = None,
                      panel: bool = True, layout: bool = True, wallpaper=UNSET,
                      panel_height: int | None = None, widgets: bool = True, progress=print,
                      preserve_wallpaper: bool = False, apply_icons: bool = True) -> int:
    """Generate, install and apply a Windows Classic scheme on the live session.

    Shared by `xp2plasma.py classic` and the Appearance editor, so both produce
    byte-identical output. For `wallpaper`: UNSET (the default) uses the bundle's
    own wallpaper if it has one, a path forces that image, and None forces the
    scheme's flat desktop colour.
    """
    out_dir = out_dir or paths.BUILD
    os.makedirs(out_dir, exist_ok=True)

    progress("Generating window decoration ...")
    pkg_dir = classic_gen.write_aurorae_package(scheme, out_dir, name)
    plugin_id = os.path.basename(pkg_dir)
    colors_text = classic_gen.build_colorscheme(scheme, name)

    panel_pkg = panel_id = start_png = None
    bar_height = scheme.bar_height
    if panel:
        progress("Generating taskbar and Start button ...")
        panel_pkg, bar_height = classic_gen.build_panel_theme(scheme, out_dir, name)
        panel_id = os.path.basename(panel_pkg)
        start_png = classic_gen.build_start_button_pngs(
            scheme, os.path.expanduser("~/.local/share/xp2plasma"), f"start-{panel_id}")

    apply_mod.install_aurorae_theme(pkg_dir, plugin_id)
    apply_mod.install_colorscheme(colors_text, name)
    if panel_pkg:
        apply_mod.install_desktoptheme(panel_pkg, panel_id)
        if layout:
            install_start_applet(PANEL_PADDING["top"], START_OVERHANG_LEFT)

    progress("Applying ...")
    apply_mod.apply_aurorae_theme(plugin_id)
    apply_mod.apply_colorscheme(name)
    apply_font_specs(scheme.caption_font, scheme.ui_font)
    if widgets:
        progress("Applying widget style ...")
        widget_style.apply_classic_style()
    if panel_pkg:
        apply_mod.apply_desktoptheme(panel_id)
        apply_mod.apply_panel_settings(panel_height or bar_height)
        if layout:
            for line in apply_xp_layout(start_png, START_APPLET_ID).splitlines():
                progress("  " + line)
        elif start_png:
            progress("  " + set_start_icon(start_png, START_APPLET_ID))

    if wallpaper is UNSET:
        image = theme_wallpaper_path(theme_file) if theme_file else None
    else:
        image = wallpaper
    if preserve_wallpaper:
        pass  # Appearance changes windows/colors/fonts, never the desktop.
    elif image:
        fill = theme_wallpaper_fill_mode(theme_file) if theme_file else None
        progress(f"Applying wallpaper {os.path.basename(image)} ...")
        apply_mod.apply_wallpaper(image, fill)
    else:
        rgb = classic_gen.scheme_wallpaper_color(scheme)
        progress(f"Desktop colour {rgb} (flat classic desktop)")
        apply_mod.apply_wallpaper_color(rgb)
    if apply_icons:
        xp_icons.apply_prepared_theme()
    applied_state.save(kind="classic", name=name, scheme=scheme.name,
                       theme_file=os.path.abspath(theme_file) if theme_file else None)
    return 0


def apply_msstyles_live(msstyles: str, name: str, *, theme_file: str | None = None,
                        out_dir: str | None = None, panel: bool = True, layout: bool = True,
                        wallpaper=UNSET, fill_mode=UNSET, panel_height: int | None = None,
                        ini_resource_name: str | None = None, apply_theme: bool = True,
                        widgets: bool = True, progress=print, apply_icons: bool = True) -> int:
    """Parse a .msstyles, generate every package, and apply it.

    `ini_resource_name` picks one size/colour variant of the style (XP ships
    them as separate INI resources, e.g. NORMALBLUE_INI); None takes the
    style's own default. For `wallpaper`: UNSET uses the bundle's own image,
    a path forces that image, and None leaves the wallpaper alone.
    """
    out_dir = out_dir or paths.BUILD
    os.makedirs(out_dir, exist_ok=True)

    progress(f"Parsing {os.path.basename(msstyles)} ...")
    theme = MsStyle.load(msstyles, ini_resource_name=ini_resource_name)

    progress("Generating window decoration ...")
    pkg_dir = generate_theme_package(msstyles, out_dir, name, ini_resource_name)
    plugin_id = os.path.basename(pkg_dir)
    colors_text = build_colorscheme(theme, name)

    panel_pkg = panel_id = start_png = None
    panel_h = 0
    if panel:
        progress("Generating taskbar theme ...")
        try:
            panel_pkg, panel_h = generate_plasma_theme(theme, out_dir, name, panel_height)
            panel_id = os.path.basename(panel_pkg)
            start_png = build_start_button_pngs(
                theme, os.path.expanduser("~/.local/share/xp2plasma"), f"start-{panel_id}", panel_h)
        except ValueError as exc:
            progress(f"  skipped: {exc}")

    kv_name = None
    if widgets:
        progress("Generating widget style ...")
        kv_name = widget_style.kvantum_theme_name(name)
        _dir, report = generate_kvantum_theme(theme, kv_name)
        flat = sorted(k for k, v in report.items() if v in ("flat", "missing"))
        if flat:
            progress(f"  not in this style, drawn flat: {', '.join(flat)}")

    apply_mod.install_aurorae_theme(pkg_dir, plugin_id)
    apply_mod.install_colorscheme(colors_text, name)
    if panel_pkg:
        import panel_widgets
        panel_widgets.install()
        apply_mod.install_desktoptheme(panel_pkg, panel_id)
        if layout:
            install_start_applet(PANEL_PADDING["top"], START_OVERHANG_LEFT, theme)

    if not apply_theme:
        progress("Installed but not applied (--no-apply). "
                 "Enable manually in System Settings, or re-run without --no-apply.")
        return 0

    progress("Applying ...")
    apply_mod.apply_aurorae_theme(plugin_id)
    apply_mod.apply_colorscheme(name)
    apply_fonts(theme)
    if kv_name:
        # Before the panel step: plasmashell's restart is what hands the new
        # QT_PLUGIN_PATH to apps launched from here on. Best-effort, because
        # building the plugin needs Qt development files or a container on this
        # machine -- and everything else in the theme should still apply
        # without it rather than aborting the run half-way.
        try:
            widget_style.apply_kvantum_theme(kv_name, progress)
        except RuntimeError as exc:
            progress(f"  widget style skipped: {exc}")
            progress("  the rest of the theme still applies; the widget style will "
                     "reach newly started apps after the next login once it can be built")
        else:
            if not panel_pkg:
                progress("  widget style reaches newly started apps after the next plasmashell restart or login")
    if panel_pkg:
        apply_mod.apply_desktoptheme(panel_id)
        previous = apply_mod.apply_panel_settings(panel_height or panel_h)
        from panel_layout import apply_clock_font
        from fonts_gen import _parse_font_spec
        apply_clock_font(*_parse_font_spec(theme.effective("traynotify::clock").get("font", "Tahoma, 8")))
        if previous:
            progress("  previous panel settings: " + previous.replace("\n", "; "))
        if layout:
            for line in apply_xp_layout(start_png, START_APPLET_ID).splitlines():
                progress("  " + line)
        elif start_png:
            progress("  " + set_start_icon(start_png, START_APPLET_ID))

    image = (theme_wallpaper_path(theme_file) if theme_file else None) if wallpaper is UNSET else wallpaper
    if theme_file and wallpaper is UNSET and not image:
        from theme_file import load_theme_file
        sections = load_theme_file(theme_file)
        if sections.get("control panel\\desktop", {}).get("wallpaper") == "":
            value = sections.get("control panel\\colors", {}).get("background")
            if value:
                apply_mod.apply_wallpaper_color(tuple(int(v) for v in value.split()[:3]))
    if image:
        mode = fill_mode if fill_mode is not UNSET else (
            theme_wallpaper_fill_mode(theme_file) if theme_file else None)
        progress(f"Applying wallpaper {os.path.basename(image)} "
                 f"({next((k for k, v in POSITIONING.items() if v == mode), 'current positioning')}) ...")
        apply_mod.apply_wallpaper(image, mode)
    if apply_icons:
        xp_icons.apply_prepared_theme()
    applied_state.save(kind="style", name=name, msstyles=os.path.abspath(msstyles),
                       ini=ini_resource_name, theme_file=os.path.abspath(theme_file) if theme_file else None)
    progress("Done.")
    return 0


def run_classic(args) -> int:
    """The real Windows Classic look (visual styles off): synthesized from a
    colour scheme instead of parsed from a .msstyles."""
    theme_file = args.theme if args.theme.lower().endswith(".theme") else paths.stock_classic_theme()
    if args.scheme and args.scheme.lower() == "list":
        print("\n".join(classic_gen.list_schemes()))
        return 0
    scheme = classic_gen.load_scheme(theme_file, args.scheme)
    name = args.name or "".join(ch for ch in scheme.name if ch.isalnum()) or "WindowsClassic"
    print(f"Windows Classic: scheme '{scheme.name}' (caption {scheme.caption}px, frame {scheme.frame}px, "
          f"{scheme.caption_font[0]} {scheme.caption_font[1]}{' bold' if scheme.caption_font[2] else ''})")
    if args.no_apply:
        out_dir = args.out or paths.BUILD
        classic_gen.write_aurorae_package(scheme, out_dir, name)
        print("Generated but not applied (--no-apply).")
        return 0
    rc = apply_scheme_live(
        scheme, name, out_dir=args.out, theme_file=theme_file, panel=not args.no_panel,
        layout=not args.no_layout,
        wallpaper=None if args.no_wallpaper else (args.wallpaper or UNSET),
        panel_height=args.panel_height, widgets=not args.no_widgets)
    print("Done.")
    return rc


POSITIONING = {
    "scaled": FILL_STRETCH,
    "fit": FILL_FIT,
    "crop": FILL_CROP,
    "center": FILL_CENTER,
    "tile": FILL_TILE,
}
import apply as apply_mod


def run_setup(argv) -> int:
    """`xp2plasma setup`: get the assets a fresh machine needs onto it.

    Nothing Microsoft-copyrighted ships with the app, so this is how a new
    install becomes usable: point it at your own XP media, or let it fetch the
    theme packs from archive.org.
    """
    ap = argparse.ArgumentParser(prog="xp2plasma setup", description=run_setup.__doc__)
    ap.add_argument("--media", help="Path to an XP ISO, an extracted I386 directory, or its parent")
    ap.add_argument("--all-packs", action="store_true",
                    help="Also fetch the optional downloads (the ~218 MB fan collection)")
    ap.add_argument("--iso", action="store_true",
                    help="Download XP's install media from archive.org (~589 MB) and import the "
                         "fonts, Luna style, wallpapers and Start flag from it")
    ap.add_argument("--no-packs", action="store_true", help="Don't download anything")
    ap.add_argument("--no-widgets", action="store_true", help="Skip building the Kvantum widget style")
    ap.add_argument("--check", action="store_true", help="Only report what's present and missing")
    args = ap.parse_args(argv)

    if args.check:
        for check in setup_assets.preflight():
            print(f"{'ok  ' if check.ok else 'MISS'}  {check.name:<28} {check.purpose}")
        missing = setup_assets.missing_required()
        return 1 if missing else 0

    return setup_assets.run(media=args.media, packs=not args.no_packs,
                            include_optional=args.all_packs, widget=not args.no_widgets,
                            iso=args.iso)


def run_install(argv) -> int:
    """`xp2plasma install`: install the app natively on the host.

    The AppImage is just the installer; this is the headless equivalent. It
    creates a venv from a host Python, installs the third-party dependencies,
    and writes launchers plus a desktop entry.
    """
    import install
    ap = argparse.ArgumentParser(prog="xp2plasma install", description=run_install.__doc__)
    ap.add_argument("--python", help="Python interpreter to build the venv with (default: auto-detect)")
    ap.add_argument("--no-deps", action="store_true", help="Skip pip-installing the dependencies")
    ap.add_argument("--no-iso", action="store_true",
                    help="Skip the bundled asset-setup pass (XP media, theme packs, icons and widget style)")
    ap.add_argument("--widgets", action="store_true", help="Also build the Kvantum widget style")
    args = ap.parse_args(argv)
    try:
        install.install(python=args.python, install_deps=not args.no_deps, fetch_iso=not args.no_iso)
    except install.InstallError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    if args.widgets:
        try:
            widget_style.install_kvantum()
        except RuntimeError as exc:
            print(f"warning: widget style skipped: {exc}", file=sys.stderr)
    return 0


def run_uninstall(argv) -> int:
    """`xp2plasma uninstall`: remove the native install."""
    import install
    ap = argparse.ArgumentParser(prog="xp2plasma uninstall", description=run_uninstall.__doc__)
    ap.add_argument("--assets", action="store_true",
                    help="Also delete imported XP assets and saved schemes")
    ap.add_argument("--keep-desktop", action="store_true",
                    help="Leave the desktop as it is (default: restore Breeze, the default "
                         "panel layout and wallpaper, and remove the generated theme packages)")
    args = ap.parse_args(argv)
    install.uninstall(remove_assets=args.assets, reset_desktop=not args.keep_desktop)
    return 0


def run_reset(argv) -> int:
    """`xp2plasma reset`: put the desktop back to Breeze without uninstalling."""
    import revert
    ap = argparse.ArgumentParser(prog="xp2plasma reset", description=run_reset.__doc__)
    ap.add_argument("--keep-packages", action="store_true",
                    help="Keep the generated theme packages (only switch the desktop back)")
    args = ap.parse_args(argv)
    revert.reset_desktop(remove_packages=not args.keep_packages)
    return 0


def main() -> int:
    # `edit`, `setup`, `install` and `uninstall` take their own flags, so they
    # are dispatched before argparse sees the command line.
    if len(sys.argv) > 1 and sys.argv[1].lower() in ("edit", "appearance"):
        from appearance_editor import main as editor_main
        return editor_main(sys.argv[2:])
    if len(sys.argv) > 1 and sys.argv[1].lower() == "setup":
        return run_setup(sys.argv[2:])
    if len(sys.argv) > 1 and sys.argv[1].lower() == "install":
        return run_install(sys.argv[2:])
    if len(sys.argv) > 1 and sys.argv[1].lower() == "uninstall":
        return run_uninstall(sys.argv[2:])
    if len(sys.argv) > 1 and sys.argv[1].lower() == "reset":
        return run_reset(sys.argv[2:])

    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("theme", nargs="?", help="Path to a .msstyles visual style, a .theme bundle that references one, "
                                  "or 'classic' / 'edit' / 'setup' / 'install' / 'uninstall' / 'reset'")
    ap.add_argument("--name", help="Theme name (default: derived from filename)")
    ap.add_argument("--no-apply", action="store_true", help="Generate + install only, don't switch the live session")
    ap.add_argument("--out", default=None, help="Directory to write the generated Aurorae package into")
    ap.add_argument("--wallpaper", default=None,
                    help="Wallpaper image to apply (default: the one referenced by the .theme bundle, if any)")
    ap.add_argument("--no-wallpaper", action="store_true", help="Never touch the wallpaper")
    ap.add_argument("--scheme", default=None,
                    help="Windows Classic only: one of XP's stock appearance schemes (e.g. 'Rainy Day', "
                         "'Windows Standard (large)'), or 'list' to print them")
    ap.add_argument("--no-panel", action="store_true",
                    help="Don't generate/apply the panel (taskbar) theme or change panel geometry")
    ap.add_argument("--no-layout", action="store_true",
                    help="Keep the panel's current widgets; don't rearrange it into the XP taskbar layout")
    ap.add_argument("--no-widgets", action="store_true",
                    help="Leave the Qt widget style alone (no Kvantum theme for visual styles, "
                         "no switch to the Windows style for Classic)")
    ap.add_argument("--remove-widget-style", action="store_true",
                    help="Undo the widget style: restore the previous one and remove the Kvantum plugin + env hook")
    ap.add_argument("--panel-height", type=int, default=None,
                    help="Panel thickness in px (default: the theme's taskbar bitmap height, usually 30)")
    ap.add_argument("--positioning", choices=sorted(POSITIONING),
                    help="Wallpaper positioning override (default: the .theme's WallpaperStyle/TileWallpaper "
                         "if it has one, else your current Plasma setting is left as-is)")
    args = ap.parse_args()

    if args.remove_widget_style:
        previous = widget_style.remove_kvantum()
        print(f"Widget style restored to {previous}; Kvantum plugin and env hook removed.")
        return 0
    if not args.theme:
        ap.error("a theme is required")
    if args.theme.lower() != "classic" and not os.path.exists(args.theme):
        print(f"error: {args.theme}: no such file", file=sys.stderr)
        return 1

    theme_file = None
    is_bundle = args.theme.lower().endswith(".theme")
    # A bundle whose .msstyles can't be found still carries a colour scheme and
    # a wallpaper, so it applies as Windows Classic -- which is exactly what the
    # editor does with it. Test the *resolved* path, not the raw [VisualStyles]
    # Path= string, or a bundle pointing at a file that isn't on disk would take
    # the visual-style route and fail there instead.
    if args.theme.lower() == "classic" or (is_bundle and not theme_msstyles_path(args.theme)):
        return run_classic(args)
    if is_bundle:
        theme_file = args.theme
        msstyles = theme_msstyles_path(theme_file)
        print(f"{args.theme} -> {msstyles}")
    else:
        msstyles = args.theme
        theme_file = find_theme_file_for(msstyles)
        if theme_file:
            print(f"Found bundle: {theme_file}")

    # A bundle names the variant it wants ([VisualStyles] ColorStyle/Size);
    # honour it, the way XP does, instead of always taking the style's default.
    ini = None
    if theme_file:
        import theme_index
        entry = theme_index.ThemeEntry(os.path.basename(theme_file), msstyles, theme_file=theme_file)
        ini = theme_index.bundle_variant(entry, theme_index.visual_style_from(msstyles))
        if ini:
            print(f"Bundle asks for variant {ini}")

    name = args.name or os.path.splitext(os.path.basename(msstyles))[0]
    return apply_msstyles_live(
        msstyles, name, theme_file=theme_file, out_dir=args.out,
        panel=not args.no_panel, layout=not args.no_layout,
        wallpaper=None if args.no_wallpaper else (args.wallpaper or UNSET),
        fill_mode=POSITIONING[args.positioning] if args.positioning else UNSET,
        panel_height=args.panel_height, ini_resource_name=ini, apply_theme=not args.no_apply,
        widgets=not args.no_widgets)


if __name__ == "__main__":
    sys.exit(main())
