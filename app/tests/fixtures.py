"""Where the tests find Microsoft's files, which never live in this repo.

The official visual styles come from the user's imported asset library
(``paths.THEMES``) unless ``XPLASMA_CORPUS`` names another directory laid out
the same way (``Luna/luna.msstyles``, ``Royale/Royale.msstyles``, ...). The XP
install media comes from wherever setup recorded it, and the ISO from the
download cache. Tests that need a missing file skip rather than fail: the
suite has to run on a machine that has never imported anything.
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import paths  # noqa: E402

CORPUS = Path(os.environ.get("XPLASMA_CORPUS") or paths.THEMES)
LUNA = CORPUS / "Luna" / "luna.msstyles"
OFFICIAL = {
    "Luna": LUNA,
    "Royale": CORPUS / "Royale" / "Royale.msstyles",
    "Royale Noir": CORPUS / "Royale Noir" / "Royale Noir.msstyles",
    "Zune": CORPUS / "Zune" / "Zune.msstyles",
    "Embedded": CORPUS / "Embedded" / "Embedded.msstyles",
}


def xp_i386() -> Path | None:
    """The extracted XP media's I386 directory, if setup has one on record."""
    found = paths.iso_i386()
    return Path(found) if found else None


def xp_iso() -> Path | None:
    """The XP install ISO, if one was downloaded."""
    candidates = sorted(Path(paths.DOWNLOADS).glob("*.iso")) if os.path.isdir(paths.DOWNLOADS) else []
    return candidates[0] if candidates else None
