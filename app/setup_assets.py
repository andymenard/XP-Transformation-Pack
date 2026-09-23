"""First-run setup: put the assets xp2plasma needs onto this machine.

None of it ships with the app. The XP install media, the visual styles and the
fonts are all Microsoft's, so xp2plasma can point at a copy you already have or
fetch the theme packs from archive.org -- but it cannot carry them itself.

Two sources, usable in either order and any combination:

* **Your own install media.** An XP ISO, or an already-extracted ``I386``
  directory. The fonts, all theme presets, the Luna visual style and wallpapers come out
  of it with cabextract. Nothing is copied wholesale: an extracted XP ISO is
  ~600 MB and only a handful of members are actually wanted.

* **archive.org.** The official theme packs (Royale, Royale Noir, Zune,
  Embedded) and, optionally, the large fan collection. The file list is read
  from the item's metadata API, so nothing but the item id is hardcoded.

Called by ``xp2plasma setup`` and by the editor's Setup dialog, so both paths
share one implementation.
"""
from __future__ import annotations

import json
import http.client
import os
import re
import shutil
import tempfile
import urllib.error
import urllib.parse
import urllib.request
import zipfile
from dataclasses import dataclass

import hostexec as host
import paths
import widget_style


class AssetError(RuntimeError):
    """An asset download/import did not complete; never a successful job."""

# --- what setup needs from the system ---------------------------------------

# (tool, what it's for, alternatives, when it's needed). "install": blocks
# `xp2plasma install` itself (today, only cabextract -- the XP install media
# is downloaded and unpacked as part of installing). "apply": not needed to
# install, but needed once you actually apply a theme; xp2plasma setup gates
# on this set. Neither: informational only, never blocks anything.
_TOOLS: tuple[tuple[str, str, tuple[str, ...], frozenset[str]], ...] = (
    ("kwriteconfig6", "write KDE settings", (), frozenset({"apply"})),
    ("kreadconfig6", "read KDE settings", (), frozenset({"apply"})),
    ("qdbus6", "control KWin and plasmashell", ("qdbus",), frozenset({"apply"})),
    ("plasma-apply-colorscheme", "apply the colour scheme", (), frozenset({"apply"})),
    ("plasma-apply-desktoptheme", "apply the panel theme", (), frozenset({"apply"})),
    ("plasma-apply-wallpaperimage", "set the wallpaper", (), frozenset()),
    ("systemctl", "restart plasmashell", (), frozenset({"apply"})),
    ("fc-match", "resolve fonts", (), frozenset()),
    ("fc-cache", "make the XP fonts visible", (), frozenset()),
    ("cabextract", "unpack XP install media (bundled with the installer)", (),
     frozenset({"install", "apply"})),
    # The official packs are plain .zip (stdlib); only the fan collection is a
    # .rar, and RAR5 support is patchy, so it's offered as a fallback chain.
    ("bsdtar", "unpack archives (bundled with the installer)", ("unrar", "7z"), frozenset()),
)
_TOOL_KINDS = {name: kind for name, _purpose, _alt, kind in _TOOLS}


# --- is this a Plasma 6 session? ---------------------------------------------
#
# "plasmashell 6.3.5" and "kwin 6.3.5" both lead with the version.
_PLASMA_VERSION_RE = re.compile(r"(\d+)\.(\d+)(?:\.(\d+))?")


def _plasma_version() -> str | None:
    """The running Plasma's version as "6.3.5", or None if it can't be read."""
    for argv in (["plasmashell", "--version"], ["kwin_wayland", "--version"],
                 ["kwin_x11", "--version"]):
        if not host.have(argv[0]):
            continue
        match = _PLASMA_VERSION_RE.search(host.run(argv, capture=True).stdout)
        if match:
            return match.group(0)
    return None


def plasma_session() -> tuple[bool, str]:
    """(usable, detail): is this a live KDE Plasma 6 session?

    xp2plasma configures a session that is *running* -- it restarts plasmashell,
    drives KWin over D-Bus, and writes settings that the desktop in front of the
    user then reads. So the question is never what is installed on the disk: a
    machine with Plasma 6 available but a GNOME session running cannot be
    themed by this, and the installer has nothing to offer there.

    ``XDG_CURRENT_DESKTOP`` is the primary signal, since the session itself sets
    it. It is only consulted when present, because it is legitimately absent
    when the wizard is started from outside a session (over SSH, or by a script
    with a scrubbed environment) -- there, a running plasmashell is better
    evidence than an unset variable, and refusing to start on an unset variable
    would lock out a user who is on Plasma after all.
    """
    desktop = os.environ.get("XDG_CURRENT_DESKTOP", "").strip()
    if desktop and "kde" not in desktop.lower():
        return False, desktop
    version = _plasma_version()
    if version is not None:
        # Plasma 5 calls itself KDE too, so the version is what separates the
        # desktop this supports from the one it does not.
        return int(version.split(".")[0]) >= 6, version
    # A KDE-named session whose version we could not read is still a KDE
    # session; blocking it because a probe failed is worse than the rare wrong
    # answer, and the tools it actually needs are checked separately.
    return bool(desktop), desktop or "no desktop session detected"


@dataclass(frozen=True)
class Check:
    name: str
    purpose: str
    found: str | None

    @property
    def ok(self) -> bool:
        return self.found is not None


def preflight() -> list[Check]:
    """Which of the tools this app drives are actually present on the host."""
    checks = []
    for name, purpose, alternatives, _kind in _TOOLS:
        found = host.first_available(name, *alternatives)
        checks.append(Check(name, purpose, found))
    return checks


def _missing(kind: str) -> list[Check]:
    return [c for c in preflight() if not c.ok and kind in _TOOL_KINDS.get(c.name, frozenset())]


def missing_required() -> list[Check]:
    """Missing tools that block actually applying a theme."""
    return _missing("apply")


def missing_for_install() -> list[Check]:
    """Missing tools that block `xp2plasma install` itself."""
    return _missing("install")


# --- pulling assets out of the user's own XP media ---------------------------

# Visual-style binaries are separate from .theme presets. Discover presets
# from the media instead of maintaining a Luna-only allowlist.
# Icon-bearing binaries and the Start flag are extracted from the user's media.
_STYLE_MEMBERS = ("LUNA.MS_",)
from xp_icons import PACKED_MEMBERS
# PACKED_MEMBERS is the icon harvester's list of XP binaries. themeui.dll is
# added on top of it rather than to it, because what we want from that DLL is a
# *bitmap*, not an icon group: it carries the monitor Display Properties
# previews a desktop in (xp_assets). Naming it here is what gets it out of the
# ISO and into the unpacked media, which is where xp_assets then looks for it.
_BINARY_MEMBERS = PACKED_MEMBERS + ("THEMEUI.DL_",)
# The compressed members only record the last character of the real extension,
# so cabextract can only guess an 8.3 name ("luna.mst"). These are the names
# the theme scanner (and the .theme's own [VisualStyles] Path) expect.
_STYLE_RENAMES = {"LUNA.MS_": "luna.msstyles"}
_THEME_SUFFIXES = (".TH_", ".THE", ".THEME")
_FONT_GLOB = "*.TT_"
_IMAGE_GLOBS = ("*.BM_", "*.JP_")


def _wanted(name: str) -> bool:
    upper = name.upper()
    return (upper.endswith((".TT_", ".BM_", ".JP_"))
            or upper.endswith(_THEME_SUFFIXES)
            or upper in _STYLE_MEMBERS or upper in _BINARY_MEMBERS)

# XP's I386 carries its wallpapers as ordinary compressed members sitting
# alongside toolbar bitmaps (BACKOFF.JP_ is 16x16). Size is what separates
# them, so anything below VGA is dropped after unpacking.
_MIN_WALLPAPER = (640, 480)


class _NewFiles:
    """Snapshot of directories before a job, so what it added can be removed
    again -- and only that: files that were there before are left alone."""

    def __init__(self, roots: tuple[str, ...]) -> None:
        self.roots = roots
        self.before = {root: self._walk(root) for root in roots}

    @staticmethod
    def _walk(root: str) -> set[str]:
        found: set[str] = set()
        for dirpath, dirnames, filenames in os.walk(root):
            for name in dirnames + filenames:
                found.add(os.path.relpath(os.path.join(dirpath, name), root))
        return found

    def remove_new(self) -> int:
        removed = 0
        for root in self.roots:
            if not os.path.isdir(root):
                continue
            existed = os.path.isdir(root) and root in self.before and self.before[root] is not None
            new = self._walk(root) - self.before.get(root, set())
            # Files first, then directories deepest-first, then the root itself
            # if the job created it.
            for rel in sorted(new, key=lambda r: (-r.count(os.sep), r)):
                full = os.path.join(root, rel)
                try:
                    if os.path.isdir(full) and not os.path.islink(full):
                        os.rmdir(full)
                    else:
                        os.remove(full)
                    removed += 1
                except OSError:
                    pass
            if not existed or not self.before[root]:
                try:
                    if not os.listdir(root) and root not in self._pre_existing_roots:
                        os.rmdir(root)
                except OSError:
                    pass
        return removed

    @property
    def _pre_existing_roots(self) -> set[str]:
        return {root for root, files in self.before.items() if files is not None and os.path.isdir(root)}


def _find_i386(path: str) -> str | None:
    """The I386 directory inside whatever the user pointed us at."""
    if not os.path.isdir(path):
        return None
    if any(os.path.isfile(os.path.join(path, n)) for n in ("EXPLORER.EX_", "LUNA.MS_")):
        return path
    nested = os.path.join(path, "I386")
    if os.path.isdir(nested):
        return nested
    return None


def _extract_iso_pycdlib(iso: str, dest: str, progress) -> bool:
    """Read the ISO in-process. XP media are plain ISO9660 with 8.3 names, so
    identifiers come back as e.g. ``LUNA.MS_;1``."""
    try:
        import pycdlib
    except ImportError:
        return False
    paths.ensure(os.path.join(dest, "I386"))
    cd = pycdlib.PyCdlib()
    try:
        cd.open(iso)
    except Exception as exc:                                  # pycdlib raises its own hierarchy
        progress(f"  {os.path.basename(iso)} is not readable as an ISO image: {exc}")
        return False
    try:
        count = 0
        for directory, _dirs, files in cd.walk(iso_path="/"):
            for ident in files:
                name = ident.split(";")[0]
                if not ((directory.upper() == "/I386" and _wanted(name))
                        or name.upper().endswith(_THEME_SUFFIXES)):
                    continue
                relative = directory.strip("/")
                folder = paths.ensure(os.path.join(dest, relative))
                cd.get_file_from_iso(os.path.join(folder, name),
                                     iso_path=f"{directory.rstrip('/')}/{ident}")
                count += 1
    except Exception as exc:
        progress(f"  couldn't read the assets from the ISO: {exc}")
        return False
    finally:
        cd.close()
    progress(f"  read {count} files from the ISO")
    return count > 0


def _extract_iso(iso: str, dest: str, progress) -> bool:
    """Pull just the wanted members out of an ISO, never the whole thing."""
    paths.ensure(dest)
    if _extract_iso_pycdlib(iso, dest, progress):
        return True
    members = [f"I386/{p}" for p in (_FONT_GLOB, *_STYLE_MEMBERS, *_BINARY_MEMBERS, *_IMAGE_GLOBS)]
    members += ["*.TH_", "*.THE", "*.THEME", "*.theme", "*.Theme"]
    if host.have("7z"):
        argv = ["7z", "x", "-r", "-y", f"-o{dest}", iso, *members]
    elif host.have("bsdtar"):
        argv = ["bsdtar", "-xf", iso, "-C", dest, *members]
    else:
        progress("  couldn't read the ISO (and no 7z/bsdtar to fall back on); "
                 "extract it yourself and point setup at the I386 folder")
        return False
    result = host.run(argv, capture=True)
    if result.returncode != 0 and not _find_i386(dest):
        progress(f"  couldn't read {os.path.basename(iso)}: {(result.stderr or '').strip()[:200]}")
        return False
    return True


def _cabextract(files: list[str], dest: str, progress) -> int:
    """Unpack a batch of individually-compressed XP members.

    One cabextract call for the whole batch rather than one per file: there are
    ~130 fonts and each member is tiny, so process startup would dominate.
    """
    if not files:
        return 0
    paths.ensure(dest)
    host.run(["cabextract", "-q", "-d", dest, *files], capture=True, check=True)
    return len(files)


def _prune_non_wallpapers(directory: str, progress) -> int:
    """Drop the toolbar bitmaps that came out alongside the wallpapers."""
    from PIL import Image
    dropped = 0
    for name in sorted(os.listdir(directory)):
        full = os.path.join(directory, name)
        if not os.path.isfile(full):
            continue
        try:
            with Image.open(full) as im:
                w, h = im.size
        except Exception:
            os.remove(full)
            dropped += 1
            continue
        if w < _MIN_WALLPAPER[0] or h < _MIN_WALLPAPER[1]:
            os.remove(full)
            dropped += 1
    if dropped:
        progress(f"  dropped {dropped} non-wallpaper image(s)")
    return dropped


def _import_theme_presets(media: str, progress) -> int:
    """Import every actual preset, including compressed and OEM/custom ones.

    SZDD's .TH_ suffix is ambiguous (NOISE.TH_ is noise.tha, not a theme).
    Check the decoded INI before installing; never rename arbitrary .TH_ data
    into the Themes list. Isolated unpacking also makes repeat imports reliable.
    """
    from theme_file import load_theme_file
    count = 0
    for directory, dirs, names in os.walk(media):
        dirs.sort()
        for name in sorted(names):
            if not name.upper().endswith(_THEME_SUFFIXES):
                continue
            host.check_cancelled()
            source = os.path.join(directory, name)
            with tempfile.TemporaryDirectory(prefix="theme-unpack-", dir=paths.ensure(paths.CACHE)) as scratch:
                if name.upper().endswith(".TH_"):
                    _cabextract([source], scratch, progress)
                    candidates = [os.path.join(scratch, n) for n in sorted(os.listdir(scratch))]
                else:
                    candidates = [source]
                for candidate in candidates:
                    if not os.path.isfile(candidate) or "theme" not in load_theme_file(candidate):
                        continue
                    stem = os.path.splitext(os.path.basename(candidate))[0]
                    relative = os.path.relpath(directory, media)
                    if relative.upper() in (".", "I386") and stem.casefold() == "luna":
                        target = os.path.join(paths.THEMES, "Luna", "Luna.theme")
                    elif relative.upper() in (".", "I386") and stem.casefold() == "classic":
                        target = os.path.join(paths.THEMES, "Windows Classic.theme")
                    else:
                        target = os.path.join(paths.THEMES, "Media", relative, stem + ".theme")
                    paths.ensure(os.path.dirname(target))
                    shutil.copyfile(candidate, target)
                    count += 1
                    progress(f"  imported preset: {os.path.basename(target)}")
    return count


def import_media(path: str, progress=print) -> dict:
    """Pull fonts, all presets, the Luna style and wallpapers from XP media.

    `path` may be an ISO or an already-extracted directory (either the I386
    folder itself or its parent). Cancelled or failed part-way, it removes
    whatever it had added and puts the recorded media path back.
    """
    result = {"fonts": 0, "styles": 0, "wallpapers": 0, "themes": 0}
    if not host.have("cabextract"):
        raise AssetError("cabextract isn't installed, so the media can't be read.")

    staging_dir = os.path.join(paths.CACHE, "media-unpack")
    roots = tuple(r for r in (paths.FONTS_XP, paths.THEMES, paths.WALLPAPERS, staging_dir))
    pre_existing = {r for r in roots if os.path.isdir(r)}
    tracker = _NewFiles(roots)
    previous_media = paths.load_config().get("media")
    try:
        counts = _import_media(path, progress, result, staging_dir)
        missing = [kind for kind, count in counts.items() if count <= 0]
        if missing:
            raise AssetError("XP media import is incomplete: no " + ", ".join(missing)
                             + " extracted. Choose valid XP install media and retry.")
        progress(f"XP media imported: {counts['fonts']} fonts, {counts['styles']} style files, "
                 f"{counts['themes']} theme presets, {counts['wallpapers']} wallpaper files.")
        return counts
    except BaseException:
        progress("Rolling back the import ...")
        tracker.remove_new()
        for root in roots:
            if root not in pre_existing and os.path.isdir(root):
                shutil.rmtree(root, ignore_errors=True)
        paths.save_config(media=previous_media)
        progress("Rolled back.")
        raise


def _import_media(path: str, progress, result: dict, staging_dir: str) -> dict:
    staging = paths.ensure(staging_dir)
    media = path
    if os.path.isfile(path) and path.lower().endswith((".iso", ".img")):
        progress(f"Reading {os.path.basename(path)} ...")
        if not _extract_iso(path, staging, progress):
            return result
        i386 = _find_i386(staging)
        media = os.path.dirname(i386) if i386 else staging
    else:
        i386 = _find_i386(path)
    if not i386:
        progress(f"  {path} doesn't look like XP install media (no I386 folder)")
        return result

    paths.save_config(media=os.path.abspath(media))
    host.check_cancelled()

    # Fonts land where fontconfig will find them, not in our own asset library:
    # the generators look them up through fc-match as well as by path.
    font_files = [os.path.join(i386, n) for n in sorted(os.listdir(i386))
                  if n.upper().endswith(".TT_")]
    if font_files:
        progress(f"Unpacking {len(font_files)} fonts into {paths.FONTS_XP} ...")
        result["fonts"] = _cabextract(font_files, paths.FONTS_XP, progress)
        if host.have("fc-cache"):
            host.run(["fc-cache", "-f", paths.FONTS_XP], capture=True)
    host.check_cancelled()

    style_files = [os.path.join(i386, n) for n in _STYLE_MEMBERS
                   if os.path.exists(os.path.join(i386, n))]
    if style_files:
        progress("Unpacking the Luna visual style ...")
        # Into its own folder, both so the .theme finds its .msstyles beside it
        # and so the guessed 8.3 names can be put right without ambiguity.
        luna_dir = paths.ensure(os.path.join(paths.THEMES, "Luna"))
        for member in style_files:
            before = set(os.listdir(luna_dir))
            _cabextract([member], luna_dir, progress)
            produced = sorted(set(os.listdir(luna_dir)) - before)
            wanted = _STYLE_RENAMES.get(os.path.basename(member).upper())
            if produced and wanted:
                os.replace(os.path.join(luna_dir, produced[0]), os.path.join(luna_dir, wanted))
        result["styles"] = len(style_files)
    host.check_cancelled()

    progress("Importing all theme presets from the media ...")
    result["themes"] = _import_theme_presets(media, progress)
    host.check_cancelled()

    image_files = [os.path.join(i386, n) for n in sorted(os.listdir(i386))
                   if n.upper().endswith((".BM_", ".JP_"))]
    if image_files:
        progress(f"Unpacking {len(image_files)} images (wallpapers) ...")
        dest = paths.ensure(paths.WALLPAPERS)
        result["wallpapers"] = _cabextract(image_files, dest, progress)
        host.check_cancelled()
        result["wallpapers"] -= _prune_non_wallpapers(dest, progress)

    return result


# --- fetching theme packs from archive.org ----------------------------------

@dataclass(frozen=True)
class Download:
    item: str
    filename: str          # exact name, or a "*.ext" pattern (largest match wins)
    label: str
    optional: bool = False
    kind: str = "pack"     # "pack": unpack into the theme library; "iso": import_media


# XP's own install media: fonts, the Luna style, the wallpapers and the Start
# flag all come from it, and nowhere else. ~589 MB, of which a few MB are kept.
ISO_DOWNLOAD = Download("WinXPProSP3x86", "*.iso",
                        "Windows XP Professional SP3 install media (589 MB)",
                        optional=True, kind="iso")

DOWNLOADS: tuple[Download, ...] = (
    Download("windows-xp-official-themes", "XP_Themes.zip",
             "official theme packs (Royale, Royale Noir, Zune, Embedded)"),
    Download("windows-xp-official-themes", "Winter Fun Packs 2003.zip",
             "official wallpaper packs", optional=True),
    Download("ultimate-windows-xp-visual-theme-collection",
             "Ultimate Windows XP Visual Themes Collection V1.0.rar",
             "the fan theme collection", optional=True),
)

# Automatic installation includes Microsoft's packs only. The third-party
# collection is available solely through an explicit CLI --all-packs request.
INSTALL_DOWNLOADS = (ISO_DOWNLOAD, *(pack for pack in DOWNLOADS
                                   if pack.item == "windows-xp-official-themes"))

_METADATA = "https://archive.org/metadata/{item}"
_DOWNLOAD = "https://archive.org/download/{item}/{file}"
_UA = {"User-Agent": "xp2plasma (KDE theme converter)"}


def _http_json(url: str):
    request = urllib.request.Request(url, headers=_UA)
    with urllib.request.urlopen(request, timeout=30) as response:
        return json.load(response)


def remote_name(download: Download) -> str | None:
    """The item's actual file name, matched case-insensitively.

    Read from the metadata API rather than hardcoded: archive.org renames
    uploads often enough that a literal URL rots.
    """
    meta = _http_json(_METADATA.format(item=download.item))
    wanted = download.filename.lower()
    files = meta.get("files", [])
    if wanted.startswith("*."):
        matches = [f for f in files if str(f.get("name", "")).lower().endswith(wanted[1:])]
        if matches:
            return max(matches, key=lambda f: int(f.get("size") or 0))["name"]
        return None
    for entry in files:
        if str(entry.get("name", "")).lower() == wanted:
            return entry["name"]
    return None


def _cached_archive(download: Download) -> str | None:
    """A copy of this download already sitting in the cache, found without
    touching the network. Exact filenames match by name; a "*.ext" pattern
    matches by suffix -- there is only ever one of each such download, so
    "some .iso is already here" is an unambiguous answer.
    """
    directory = os.path.join(paths.CACHE, "downloads")
    if not os.path.isdir(directory):
        return None
    pattern = download.filename.lower()
    for entry in sorted(os.listdir(directory)):
        name = entry.lower()
        if (pattern.startswith("*.") and name.endswith(pattern[1:])) or name == pattern:
            return os.path.join(directory, entry)
    return None


def _fetch(download: Download, name: str, dest: str, progress, on_bytes=None) -> bool:
    """Download to `dest`. `on_bytes(done, total)` gets byte-level progress
    (total is 0 when unknown); the log gets a line every 10%. Checks for
    cancellation every chunk -- this runs in-process, so the host-command
    cancellation in hostexec cannot reach it."""
    url = _DOWNLOAD.format(item=download.item, file=urllib.parse.quote(name))
    tmp = dest + ".part"
    request = urllib.request.Request(url, headers=_UA)
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            total = int(response.headers.get("Content-Length") or 0)
            done = step = 0
            if on_bytes:
                on_bytes(0, total)
            with open(tmp, "wb") as fh:
                while True:
                    host.check_cancelled()
                    chunk = response.read(1 << 16)
                    if not chunk:
                        break
                    fh.write(chunk)
                    done += len(chunk)
                    if on_bytes:
                        on_bytes(done, total)
                    if total and done * 10 // total > step:
                        step = done * 10 // total
                        progress(f"  {step * 10}% of {total // 1048576} MB")
            if not done or (total and done != total):
                raise OSError(f"Incomplete download: received {done} of {total or 'unknown'} bytes")
    except host.Cancelled:
        if os.path.exists(tmp):
            os.remove(tmp)
        raise
    except (urllib.error.URLError, OSError, TimeoutError, http.client.HTTPException) as exc:
        progress(f"  download failed: {exc}")
        if os.path.exists(tmp):
            os.remove(tmp)
        raise AssetError(f"Couldn't download {download.label}: {exc}. Please retry.") from exc
    os.replace(tmp, dest)
    return True


def _unpack(archive: str, dest: str, progress) -> bool:
    """Unpack a theme pack. .zip is stdlib; .rar needs a tool that may be absent."""
    paths.ensure(dest)
    if archive.lower().endswith(".zip"):
        with zipfile.ZipFile(archive) as zf:
            zf.extractall(dest)
        return True
    for argv in (["bsdtar", "-xf", archive, "-C", dest],
                 ["unrar", "x", "-o+", archive, dest + os.sep],
                 ["7z", "x", "-y", f"-o{dest}", archive]):
        if host.have(argv[0]):
            if host.run(argv, capture=True).returncode == 0:
                return True
    progress(f"  can't unpack {os.path.basename(archive)}: install bsdtar, unrar or p7zip")
    return False


def download_pack(download: Download, progress=print, on_bytes=None) -> bool:
    """Fetch and unpack one archive.org item. Cancelled or failed part-way,
    anything it unpacked is removed again; a finished download is kept in the
    cache so a retry doesn't fetch it twice -- and, importantly, so a repeat
    run with the file already cached never touches the network at all, not
    even to resolve the file name (a plain 589 MB fetch would be forgivable to
    redo by mistake; a metadata lookup that silently fails offline every time
    would not).
    """
    archive = _cached_archive(download)
    if archive:
        progress(f"Using the cached {download.label} ({os.path.basename(archive)}) ...")
    else:
        progress(f"Fetching {download.label} ...")
        try:
            name = remote_name(download)
        except (urllib.error.URLError, ValueError, OSError, http.client.HTTPException) as exc:
            raise AssetError(f"Couldn't contact archive.org for {download.label}: {exc}. "
                             "Check your connection and retry.") from exc
        if not name:
            raise AssetError(f"{download.filename} was not found in archive.org item {download.item}. "
                             "Retry later or choose local install media.")
        archive = os.path.join(paths.ensure(os.path.join(paths.CACHE, "downloads")),
                               os.path.basename(name))
        if not os.path.exists(archive):
            if not _fetch(download, name, archive, progress, on_bytes):
                raise AssetError(f"Download of {download.label} did not complete. Please retry.")
    host.check_cancelled()
    if download.kind == "iso":
        if on_bytes:
            on_bytes(0, 0)                       # back to "working", not bytes
        counts = import_media(archive, progress)
        if not all(counts.get(kind, 0) > 0 for kind in ("fonts", "styles", "wallpapers")):
            raise AssetError("XP media extraction did not complete. Please retry.")
        return True
    progress(f"Unpacking into {paths.THEMES} ...")
    themes = paths.ensure(paths.THEMES)
    tracker = _NewFiles((themes,))
    try:
        if not _unpack(archive, themes, progress):
            raise AssetError(f"Couldn't unpack {download.label}. Check the Details log and retry.")
        return True
    except BaseException:
        progress("Rolling back ...")
        tracker.remove_new()
        raise


# --- the widget style --------------------------------------------------------

def install_widget_style(progress=print) -> str | None:
    """Build and install the Kvantum plugin, or explain why we can't."""
    try:
        return widget_style.install_kvantum(progress)
    except RuntimeError as exc:
        progress(f"  widget style skipped: {exc}")
        return None


def prepare_install_assets(progress=print, on_bytes=None) -> None:
    """One setup pass; failures propagate to the wizard's Details and Retry."""
    for download in INSTALL_DOWNLOADS:
        host.check_cancelled()
        if not download_pack(download, progress, on_bytes):
            raise AssetError(f"Setup of {download.label} did not complete. Please retry.")
    progress("Preparing the XP widget style ...")
    widget_style.install_kvantum(progress)
    host.check_cancelled()
    import xp_icons
    xp_icons.setup(progress, activate=False)
    progress("XP assets, theme packs, icons and widget style are ready.")


# --- the whole flow ----------------------------------------------------------

def needs_setup() -> bool:
    """True when there is nothing to theme with yet."""
    return not paths.theme_roots() or not os.path.isdir(paths.FONTS_XP)


def run(media: str | None = None, packs: bool = True, include_optional: bool = False,
        widget: bool = True, iso: bool = False, progress=print) -> int:
    """Do the whole first-run setup. Returns 0 unless something required failed."""
    problems = missing_required()
    if problems:
        for check in problems:
            progress(f"missing: {check.name} ({check.purpose})")
        progress("Install the packages above and run setup again.")
        return 1

    if media:
        counts = import_media(media, progress)
        progress(f"From your media: {counts['fonts']} fonts, {counts['styles']} style files, "
                 f"{counts['wallpapers']} images.")
    elif iso:
        download_pack(ISO_DOWNLOAD, progress)
    if packs:
        for download in DOWNLOADS:
            if download.optional and not include_optional:
                progress(f"skipping optional: {download.label} (pass --all-packs to fetch it)")
                continue
            download_pack(download, progress)
    if widget:
        if install_widget_style(progress):
            progress("Widget style built and installed.")
    paths.save_config(setup_done=True)
    progress("Setup finished.")
    return 0
