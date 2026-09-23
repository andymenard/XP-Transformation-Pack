"""Private, static archive utilities shared by the installer and native app.

Only the two explicitly named utilities are intercepted. KDE commands and
their environments remain untouched.

They are statically linked against musl, so they carry no libc dependency and
run on any distribution regardless of its glibc. That matters more than it
sounds: they are what unpacks the XP install media and the fan-collection
archives, and cabextract in particular is not installed on most desktops.

The executables are used in place, straight out of whatever tree this module
lives in -- the source checkout, the installed copy under ~/.local, or the
AppImage mount. Inside the old Flatpak they had to be copied into
~/.cache first, because the sandbox's /app was invisible to the host commands
that ran them; an AppImage mount is an ordinary path on the real filesystem, so
that indirection is gone.
"""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import platform

import paths

TOOLS = frozenset({"cabextract", "bsdtar"})


def root() -> Path:
    return Path(paths._shipped("bundled-tools"))


def _verified(file: Path, digest: str) -> bool:
    return file.is_file() and not file.is_symlink() and hashlib.sha256(file.read_bytes()).hexdigest() == digest


def executable(name: str) -> str | None:
    if name not in TOOLS:
        return None
    architecture = platform.machine()
    directory = root() / architecture
    manifest_file = directory / "manifest.json"
    if not manifest_file.exists():
        return None  # A source checkout can still use host-installed tools.
    manifest = json.loads(manifest_file.read_text())
    if manifest.get("architecture") != architecture:
        raise RuntimeError("Bundled utilities have the wrong CPU architecture")
    digest = manifest["tools"][name]["sha256"]
    if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
        raise RuntimeError(f"Invalid bundled {name} checksum")
    source = directory / name
    if not _verified(source, digest) or not os.access(source, os.X_OK):
        raise RuntimeError(f"Bundled {name} is missing or damaged; reinstall xp2plasma")
    return str(source)


def verify_bundle() -> None:
    """Release gate: never silently publish a package without both tools."""
    for name in sorted(TOOLS):
        path = executable(name)
        if path is None:
            raise RuntimeError(f"No bundled {name} for {platform.machine()}; run bundled-tools/build.sh")
        print(f"Verified bundled {name}: {path}")


if __name__ == "__main__":
    verify_bundle()
