"""Extract original XP icon resources and build a native icon-theme overlay.

Artwork comes only from the user's local install media, never this repository.
RT_GROUP_ICON stores 14-byte entries pointing to RT_ICON resources; an ICO
stores 16-byte entries pointing to file offsets. Resource IDs are not indexes.
Format: https://devblogs.microsoft.com/oldnewthing/20120720-00/?p=7083
"""
from __future__ import annotations

from dataclasses import dataclass
from io import BytesIO
import json
from pathlib import Path
import shutil
import struct

import pefile
from PIL import Image, ImageDraw, ImageFont

import hostexec as host
import paths

BINARY_NAMES = ("shell32.dll", "explorer.exe", "mydocs.dll", "netshell.dll",
                "sndvol32.exe", "mmsys.cpl", "main.cpl", "powercfg.cpl",
                "hotplug.dll", "user32.dll", "comctl32.dll", "moricons.dll")
BINARY_NAMES += ("batmeter.dll", "bthprops.cpl", "systray.exe", "notepad.exe")
BINARY_NAMES += ("xpsp2res.dll", "bthci.dll", "fsquirt.exe")
PACKED_MEMBERS = tuple(name.upper()[:-1] + "_" for name in BINARY_NAMES)
ASSET_DIR = Path(paths.ASSETS) / "icons"
THEME_ID = "xp2plasma-icons"
THEME_DIR = Path(paths.REAL_HOME) / ".local/share/icons" / THEME_ID
MARKER = "X-Xp2plasma-Icon-Theme"

# Deliberate semantic mappings, using resource IDs verified in the catalog.
# No modern application branding is replaced. Missing names inherit normally.
# (binary, group ID, freedesktop context, space-separated icon names)
ICON_MAP = (
    ("shell32.dll", 4, "Places", "folder inode-directory folder-yellow"),
    ("shell32.dll", 5, "Places", "folder-open"),
    ("shell32.dll", 37, "Places", "folder-programs"),
    ("shell32.dll", 21, "Places", "folder-recent-files"),
    ("shell32.dll", 235, "Places", "user-home folder-home folder-documents"),
    ("shell32.dll", 236, "Places", "folder-pictures"),
    ("shell32.dll", 237, "Places", "folder-music"),
    ("shell32.dll", 238, "Places", "folder-videos"),
    ("shell32.dll", 35, "Places", "user-desktop folder-desktop"),
    ("shell32.dll", 257, "Places", "folder-remote folder-network"),
    ("shell32.dll", 44, "Places", "user-bookmarks favorites"),
    ("shell32.dll", 32, "Places", "user-trash user-trash-empty"),
    ("shell32.dll", 33, "Places", "user-trash-full"),
    ("shell32.dll", 16, "Devices", "computer computer-desktop"),
    ("shell32.dll", 8, "Devices", "drive-harddisk drive-harddisk-root drive-harddisk-system"),
    ("shell32.dll", 9, "Devices", "drive-network"),
    ("shell32.dll", 12, "Devices", "drive-optical media-optical"),
    ("shell32.dll", 7, "Devices", "media-floppy drive-removable-media-floppy"),
    ("shell32.dll", 306, "Devices", "drive-removable-media drive-removable-media-usb drive-harddisk-usb"),
    ("shell32.dll", 314, "Devices", "media-flash media-flash-sd"),
    ("shell32.dll", 17, "Devices", "printer printer-network"),
    ("shell32.dll", 248, "Devices", "camera-photo"),
    ("shell32.dll", 317, "Devices", "camera-video"),
    ("shell32.dll", 316, "Devices", "scanner"),
    ("shell32.dll", 302, "Devices", "media-optical-audio"),
    ("shell32.dll", 18, "Places", "network-server network-workgroup network-network"),
    ("shell32.dll", 19, "Categories", "applications-internet"),
    ("shell32.dll", 22, "Categories", "preferences-system systemsettings"),
    ("shell32.dll", 274, "Categories", "applications-system preferences-other"),
    ("shell32.dll", 270, "Categories", "applications-graphics preferences-desktop-theme"),
    ("shell32.dll", 268, "Categories", "preferences-desktop-accessibility"),
    ("shell32.dll", 269, "Categories", "system-users preferences-system-users"),
    ("shell32.dll", 250, "Categories", "preferences-desktop-display"),
    ("shell32.dll", 39, "Categories", "preferences-desktop-font"),
    ("shell32.dll", 275, "Categories", "preferences-system-time"),
    ("shell32.dll", 4, "Applications", "system-file-manager"),
    ("shell32.dll", 152, "MimeTypes", "text-plain text-x-generic"),
    ("shell32.dll", 1, "MimeTypes", "application-x-executable application-octet-stream unknown"),
    ("shell32.dll", 224, "MimeTypes", "video-x-generic"),
    ("shell32.dll", 225, "MimeTypes", "audio-x-generic"),
    ("shell32.dll", 226, "MimeTypes", "image-x-generic"),
    ("shell32.dll", 155, "MimeTypes", "font-x-generic"),
    ("shell32.dll", 156, "MimeTypes", "application-x-font-ttf"),
    ("shell32.dll", 154, "MimeTypes", "application-x-sharedlib"),
    ("shell32.dll", 319, "Actions", "folder-new"),
    ("shell32.dll", 23, "Actions", "edit-find system-search"),
    ("shell32.dll", 160, "Actions", "system-run"),
    ("shell32.dll", 240, "Actions", "edit-delete"),
    ("shell32.dll", 133, "Actions", "edit-copy"),
    ("shell32.dll", 133, "Status", "klipper clipboard"),
    ("shell32.dll", 30, "Emblems", "emblem-symbolic-link emblem-symbolic-link-symbolic"),
    ("shell32.dll", 137, "Actions", "document-properties"),
    ("shell32.dll", 17, "Actions", "document-print"),
    ("shell32.dll", 24, "Actions", "help-contents help-browser"),
    ("user32.dll", 101, "Status", "dialog-warning"),
    ("user32.dll", 102, "Status", "dialog-question"),
    ("user32.dll", 103, "Status", "dialog-error"),
    ("user32.dll", 104, "Status", "dialog-information"),
    ("main.cpl", 100, "Devices", "input-mouse"),
    ("main.cpl", 200, "Devices", "input-keyboard"),
    ("mmsys.cpl", 4361, "Devices", "input-gaming"),
    ("mmsys.cpl", 4378, "Devices", "audio-input-microphone"),
    ("sndvol32.exe", 304, "Devices", "audio-card audio-speakers"),
    ("sndvol32.exe", 304, "Status", "audio-volume-high audio-volume-medium audio-volume-low"),
    ("sndvol32.exe", 303, "Status", "audio-volume-muted"),
    ("hotplug.dll", 100, "Status", "device-notifier"),
    # XP SP2 moved Bluetooth artwork into its shared resource DLL. Use the
    # brighter glossy glyph, including its native 16px tray image. Inactive/blocked
    # names deliberately inherit, rather than depicting a disabled radio as on.
    ("xpsp2res.dll", 300, "Status", "network-bluetooth network-bluetooth-activated bluetooth-active"),
    ("xpsp2res.dll", 300, "Categories", "preferences-system-bluetooth preferences-system-bluetooth-symbolic"),
    ("xpsp2res.dll", 300, "Devices", "bluetooth"),
    ("netshell.dll", 190, "Status", "network-wired network-wired-activated network-wireless-connected network-wireless-available"),
    ("netshell.dll", 195, "Status", "network-wired-unavailable network-wireless-disconnected network-wireless-off"),
    ("netshell.dll", 196, "Status", "network-wired-activated-limited"),
)


def icon_mappings():
    result = {}
    for binary, rid, context, names in ICON_MAP:
        for name in names.split():
            result[name] = (binary, str(rid), context)
            if context == "Status":
                result[name + "-symbolic"] = result[name]
    # SP2's six strength glyphs. Keep the limited-connectivity warning distinct.
    for level in (0, 20, 25, 40, 50, 60, 75, 80, 100):
        for name in (f"network-wireless-{level}", f"network-wireless-connected-{level:02d}"):
            for suffix in ("", "-locked", "-limited"):
                for symbolic in ("", "-symbolic"):
                    source = (("netshell.dll", "196") if suffix == "-limited" else
                              ("xpsp2res.dll", str(2010 + (level + 10) // 20)))
                    result[name + suffix + symbolic] = (*source, "Status")
    # Plasma supplies decile names; use XP's >66%, >33%, >9%, critical states.
    for level in range(0, 101, 10):
        frame = 0 if level > 66 else 1 if level > 33 else 2 if level > 9 else 3
        for charging in ("", "-charging"):
            for symbolic in ("", "-symbolic"):
                result[f"battery-{level:03d}{charging}{symbolic}"] = (
                    "batmeter.dll", f"bitmap-frame-{frame}{charging}", "Status")
    for name, frame in (("battery-missing", 5), ("battery-caution", 3),
                        ("battery-low", 2), ("battery-good", 1), ("battery-full", 0),
                        ("battery-empty", 3), ("battery-unknown", 4), ("ac-adapter", 6)):
        for symbolic in ("", "-symbolic"):
            result[name + symbolic] = ("batmeter.dll", f"bitmap-frame-{frame}", "Status")
    return result


def battery_frames(binary: Path, destination: Path) -> dict:
    """Split batmeter's native image lists, retaining their source coordinates.

    XP loads #300 with cx=32, #301 with cx=16, RGB(255,0,255) as the
    transparency key, and frame 7 as overlay 1 (charging). Each strip has
    ten frames and one unused trailing column. No resizing or recoloring.
    """
    from xp_assets import pe_bitmap
    manifest = {}
    for size, rid in ((16, 301), (32, 300)):
        strip = pe_bitmap(str(binary), str(rid))
        if strip is None or strip.size != (10 * size + 1, size):
            raise ValueError(f"Unexpected batmeter.dll bitmap #{rid} layout")
        destination.mkdir(parents=True, exist_ok=True)
        strip.save(destination / f"bitmap-{rid}.png")
        frames = []
        for frame in range(10):
            im = strip.crop((frame * size, 0, (frame + 1) * size, size)).convert("RGBA")
            im.putdata([(r, g, b, 0 if (r, g, b) == (255, 0, 255) else 255)
                        for r, g, b, _ in im.getdata()])
            frames.append(im)
        for frame, im in enumerate(frames):
            for charging in ((False, True) if frame < 4 else (False,)):
                key = f"bitmap-frame-{frame}" + ("-charging" if charging else "")
                folder = destination / key
                folder.mkdir(parents=True, exist_ok=True)
                rendered = Image.alpha_composite(im, frames[7]) if charging else im
                rendered.save(folder / f"{size}.png")
                spec = manifest.setdefault(key, {"sizes": [], "source": {
                    "resource_type": "RT_BITMAP", "frames": [frame, 7] if charging else [frame],
                    "transparent_rgb": [255, 0, 255], "resources": {}}})
                spec["sizes"].append(size)
                spec["source"]["resources"][str(size)] = {
                    "resource_id": rid,
                    "rectangles": [[f * size, 0, (f + 1) * size, size]
                                   for f in spec["source"]["frames"]]}
    return manifest


@dataclass
class IconGroup:
    resource_id: str
    ico: bytes
    images: dict[int, Image.Image]
    depths: dict[int, int]


def group_to_ico(group: bytes, bitmaps: dict[int, bytes]) -> bytes:
    """Reassemble an ICO without changing the original image payloads."""
    if len(group) < 6:
        raise ValueError("Truncated icon group")
    reserved, kind, count = struct.unpack_from("<HHH", group)
    if reserved != 0 or kind != 1 or not 0 < count <= 256 or len(group) < 6 + count * 14:
        raise ValueError("Invalid icon group directory")
    offset = 6 + count * 16
    entries, payloads = [], []
    for i in range(count):
        width, height, colors, zero, planes, depth, size, rid = struct.unpack_from("<BBBBHHIH", group, 6 + i * 14)
        data = bitmaps.get(rid)
        if data is None or len(data) != size:
            raise ValueError(f"Missing or truncated RT_ICON #{rid}")
        entries.append(struct.pack("<BBBBHHII", width, height, colors, zero, planes, depth, size, offset))
        payloads.append(data)
        offset += size
    return struct.pack("<HHH", 0, 1, count) + b"".join(entries + payloads)


def read_icons(binary: str | Path) -> list[IconGroup]:
    """Keep each native size at its highest bit depth, including its alpha/mask."""
    resources = {}
    with pefile.PE(str(binary), fast_load=True) as pe:
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
        for kind in getattr(getattr(pe, "DIRECTORY_ENTRY_RESOURCE", None), "entries", []):
            if kind.id not in (3, 14):
                continue
            for entry in kind.directory.entries:
                # Prefer US English, then the first language in the directory.
                langs = entry.directory.entries
                lang = next((v for v in langs if v.id == 1033), langs[0])
                resources[kind.id, entry.id if entry.name is None else str(entry.name)] = pe.get_data(
                    lang.data.struct.OffsetToData, lang.data.struct.Size)
    bitmaps = {rid: data for (kind, rid), data in resources.items() if kind == 3}
    result = []
    for (kind, rid), data in resources.items():
        if kind != 14:
            continue
        ico = group_to_ico(data, bitmaps)
        with Image.open(BytesIO(ico)) as image:
            images, depths = {}, {}
            for width, height in sorted(image.ico.sizes()):
                if width != height:
                    continue
                depths[width] = max(e[5] for e in (
                    struct.unpack_from("<BBBBHHIH", data, 6 + i * 14)
                    for i in range(struct.unpack_from("<H", data, 4)[0]))
                    if (e[0] or 256) == width and (e[1] or 256) == height)
                # Pillow's same-size ordering may pick a 4-bit variant first.
                # Request the best native depth explicitly, not just the size.
                images[width] = image.ico.getimage((width, height), depths[width]).convert("RGBA")
        result.append(IconGroup(str(rid), ico, images, depths))
    return result


def extract_binaries(destination: str | Path, media: str | None = None) -> dict[str, Path]:
    """Unpack only icon-bearing binaries, using existing local media/ISO caches."""
    destination = Path(destination)
    destination.mkdir(parents=True, exist_ok=True)
    candidates = [Path(media)] if media else []
    for folder in (paths.iso_i386(), str(Path(paths.HERE) / "iso/extracted/I386")):
        if folder:
            candidates.append(Path(folder))
    folders = [p / "I386" if (p / "I386").is_dir() else p for p in candidates if p.is_dir()]
    isos = [p for p in candidates if p.is_file() and p.suffix.lower() == ".iso"]
    isos += sorted((Path(paths.CACHE) / "downloads").glob("*.iso"))
    isos += sorted((Path(paths.HERE) / "iso").glob("*.iso"))
    found = {}
    for name, packed in zip(BINARY_NAMES, PACKED_MEMBERS):
        host.check_cancelled()
        target = destination / name
        if target.is_file():
            found[name] = target
            continue
        member = None
        for folder in folders:
            entries = {p.name.upper(): p for p in folder.iterdir() if p.is_file()}
            if name.upper() in entries:
                target.write_bytes(entries[name.upper()].read_bytes())
                break
            if packed in entries:
                member = entries[packed]
                break
        else:
            import pycdlib
            for iso in dict.fromkeys(isos):
                cd = pycdlib.PyCdlib()
                try:
                    cd.open(str(iso))
                    payload = BytesIO()
                    # XP discs exist both with and without ISO9660 ;1 suffixes.
                    # Resolve the recorded identifier instead of assuming one.
                    ident = next((entry.file_identifier().decode("ascii")
                                  for entry in cd.list_children(iso_path="/I386")
                                  if not entry.is_dir() and entry.file_identifier().decode("ascii").split(";")[0].upper() == packed), None)
                    if ident is None:
                        continue
                    cd.get_file_from_iso_fp(payload, iso_path=f"/I386/{ident}")
                    member = destination / packed
                    member.write_bytes(payload.getvalue())
                    break
                except pycdlib.pycdlibexception.PyCdlibException:
                    continue
                finally:
                    cd.close()
        if member:
            host.run(["cabextract", "-q", "-d", str(destination), str(member)], check=True, capture=True)
            # Some cabextract versions preserve uppercase names.
            actual = next((p for p in destination.iterdir() if p.name.lower() == name), None)
            if actual and actual != target:
                actual.rename(target)
        if target.is_file():
            found[name] = target
    return found


def build_catalog(out_dir: str | Path, media: str | None = None) -> dict:
    """Export ICOs, native PNG sizes, manifest and labelled reference sheets."""
    out = Path(out_dir)
    binaries = extract_binaries(out / "binaries", media)
    manifest = {}
    font = ImageFont.load_default(size=12)
    for name, binary in binaries.items():
        host.check_cancelled()
        groups = read_icons(binary)
        manifest[name] = {}
        for group in groups:
            if not group.resource_id.isdecimal():
                # Do not use arbitrary PE resource names as filesystem paths.
                continue
            folder = out / name / group.resource_id
            folder.mkdir(parents=True, exist_ok=True)
            (folder / "original.ico").write_bytes(group.ico)
            for size, image in group.images.items():
                image.save(folder / f"{size}.png")
            manifest[name][group.resource_id] = {"sizes": sorted(group.images), "depths": group.depths}
        groups = [g for g in groups if g.resource_id in manifest[name]]
        for page, start in enumerate(range(0, len(groups), 96), 1):
            chunk = groups[start:start + 96]
            sheet = Image.new("RGB", (960, 32 + ((len(chunk) + 11) // 12) * 80), "#ece9d8")
            draw = ImageDraw.Draw(sheet)
            draw.text((8, 8), f"{name} — resource IDs (not icon indexes) — page {page}", font=font, fill="black")
            for index, group in enumerate(chunk):
                if not group.images:
                    continue
                size = min(group.images, key=lambda s: (abs(s - 48), -s))
                icon = group.images[size]
                x, y = (index % 12) * 80, 32 + (index // 12) * 80
                sheet.paste(icon, (x + (80 - size) // 2, y + (48 - size) // 2), icon)
                draw.text((x + 5, y + 51), f"#{group.resource_id}", font=font, fill="black")
                draw.text((x + 5, y + 65), "/".join(map(str, sorted(group.images))), font=font, fill="#555555")
            sheet.save(out / f"{name}-{page}.png")
    if "batmeter.dll" in binaries:
        manifest["batmeter.dll"].update(battery_frames(binaries["batmeter.dll"], out / "batmeter.dll"))
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2))
    return manifest


def build_theme(catalog: str | Path, destination: str | Path, inherit="breeze") -> dict:
    """Native PNGs only: never scale a 48px icon down over its proper 16px art."""
    catalog, dest = Path(catalog), Path(destination)
    manifest = json.loads((catalog / "manifest.json").read_text())
    directories, provenance = {}, {}
    for name, (binary, rid, context) in icon_mappings().items():
        spec = manifest.get(binary, {}).get(rid)
        if not spec or not spec["sizes"]:
            raise ValueError(f"Missing mapped icon {binary} resource {rid}")
        # Shell32's 96px folder image is a thumbnail backplate, not the normal
        # folder glyph. Keep it in the catalog but don't let KDE prefer it for
        # large desktop icons over the actual 16/24/32/48px shell artwork.
        sizes = [size for size in spec["sizes"] if size <= 48]
        if not sizes:
            raise ValueError(f"No standard shell sizes for {binary} resource {rid}")
        provenance[name] = {"binary": binary, "sizes": sizes,
                            **spec.get("source", {"resource_id": rid})}
        for size in sizes:
            folder = f"{size}x{size}/{context.lower()}"
            directories[folder] = (size, context)
            target = dest / folder / f"{name}.png"
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(catalog / binary / rid / f"{size}.png", target)
    # Ensure inheritance is a single safe theme identifier, never an INI value.
    if not inherit or any(c in inherit for c in "\n\r,/") or inherit == THEME_ID:
        inherit = "breeze"
    # KDE tries shortened names in the current theme before inheritance, so
    # merely omitting "...-inactive-symbolic" can resolve to our enabled glyph.
    # Pin exact native SVGs for radio-off and modern power-profile badges, which
    # XP has no equivalent for. Otherwise suffix shortening loses their state.
    fallback_icons = {}
    import configparser
    native_names = {"network-bluetooth-inactive-symbolic"}
    native_names.update(f"battery-{level:03d}{charging}-profile-{profile}"
                        for level in range(0, 101, 10)
                        for charging in ("", "-charging")
                        for profile in ("powersave", "balanced", "performance"))
    for theme_name in dict.fromkeys((inherit, "breeze")):
        for base in (Path(paths.REAL_HOME) / ".local/share/icons", Path("/usr/share/icons")):
            theme = base / theme_name
            index = configparser.ConfigParser(interpolation=None, strict=False)
            index.read(theme / "index.theme")
            for folder in index.get("Icon Theme", "Directories", fallback="").split(","):
                for name in sorted(native_names - fallback_icons.keys()):
                    source = theme / folder / f"{name}.svg"
                    if not source.is_file():
                        continue
                    for size in (16, 24, 32, 48):
                        directory = f"{size}x{size}/status"
                        directories[directory] = (size, "Status")
                        target = dest / directory / f"{name}.svg"
                        target.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(source, target)
                    fallback_icons[name] = str(source)
    text = ("[Icon Theme]\nName=Windows XP\n"
            "Comment=Original Windows XP icons extracted locally by xp2plasma\n"
            f"Inherits={inherit},hicolor\nDirectories={','.join(sorted(directories))}\n")
    for folder, (size, context) in sorted(directories.items()):
        text += f"\n[{folder}]\nSize={size}\nContext={context}\nType=Fixed\n"
    (dest / "index.theme").write_text(text)
    (dest / "xp2plasma-icons.json").write_text(json.dumps(
        {MARKER: True, "icons": provenance, "native_fallbacks": fallback_icons}, indent=2))
    return provenance


def owned_theme(path: Path) -> bool:
    if path.is_symlink():
        return False
    try:
        return json.loads((path / "xp2plasma-icons.json").read_text()).get(MARKER) is True
    except (OSError, ValueError):
        return False


def current_theme() -> str:
    result = host.run(["kreadconfig6", "--file", "kdeglobals", "--group", "Icons", "--key", "Theme",
                       "--default", "breeze"], check=True, capture=True)
    return result.stdout.strip() or "breeze"


def install_theme(catalog: str | Path, destination: Path | None = None, inherit="breeze") -> dict:
    dest = destination if destination is not None else THEME_DIR
    if dest.exists() and not owned_theme(dest):
        raise RuntimeError(f"Refusing to replace an unowned icon theme: {dest}")
    # Generate and validate fully before touching the current generated theme.
    import tempfile
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".xp2plasma-icons-", dir=dest.parent) as staging:
        staged = Path(staging) / THEME_ID
        report = build_theme(catalog, staged, inherit)
        if dest.exists():
            shutil.rmtree(dest)  # generated package only; original icon themes remain intact
        staged.rename(dest)
    return report


def apply_theme() -> None:
    previous = current_theme()
    if previous != THEME_ID:
        paths.save_config(icons_previous_theme=previous)
    helper = next((p for p in ("/usr/lib/plasma-changeicons", "/usr/lib64/plasma-changeicons",
                              "/usr/libexec/plasma-changeicons") if host.exists(p)), None)
    if not helper:
        raise RuntimeError("Plasma's icon-theme activation helper was not found")
    if previous == THEME_ID:
        # The helper otherwise treats a rebuild of the selected theme as a
        # no-op, leaving newly mapped icons in the running processes' caches.
        fallback = paths.load_config().get("icons_previous_theme", "breeze")
        host.run([helper, fallback if fallback != THEME_ID else "breeze"], check=True, capture=True)
    host.run([helper, THEME_ID], check=True, capture=True)


def remove_theme() -> bool:
    if owned_theme(THEME_DIR):
        shutil.rmtree(THEME_DIR)
        return True
    return False


def setup(progress=print, *, activate=True) -> None:
    progress("Extracting XP icon resources from your local install media ...")
    manifest = build_catalog(ASSET_DIR)
    if not manifest.get("shell32.dll"):
        raise RuntimeError("No XP shell icons found. Import your XP ISO or extracted I386 folder first.")
    progress(f"Catalogued {sum(len(groups) for groups in manifest.values())} icon groups and bitmap states.")
    previous = current_theme()
    fallback = paths.load_config().get("icons_previous_theme", "breeze") if previous == THEME_ID else previous
    host.check_cancelled()
    report = install_theme(ASSET_DIR, inherit=fallback)
    progress(f"Installed {len(report)} icon names, retaining the native artwork at each size.")
    host.check_cancelled()
    if activate:
        apply_theme()
        progress("Windows XP icons applied. Unmapped icons retain their previous theme.")
    else:
        progress("Windows XP icons prepared; they will be activated when you apply a theme.")


def apply_prepared_theme() -> None:
    if owned_theme(THEME_DIR):
        apply_theme()
