"""Remember what was last applied to the session, so the editor reopens on it.

Plasma has no single "current theme" setting to read back: an applied theme is
a decoration + colour scheme + panel theme + widget style, and a visual style's
variant (Blue vs Silver, Normal vs Large Fonts) isn't recoverable from any of
them. So each apply records what it was, here.

Written by both `xp2plasma.apply_msstyles_live` and `apply_scheme_live`, which
means the command line and the editor stay in step with each other. Losing this
file only costs the editor its starting selection.
"""
from __future__ import annotations

import json
import os

STATE_PATH = os.path.expanduser("~/.local/share/xp2plasma/applied.json")


def save(**fields) -> None:
    """Record the applied theme. Never fatal: a failed write just means the
    editor opens on its default next time."""
    try:
        os.makedirs(os.path.dirname(STATE_PATH), exist_ok=True)
        tmp = f"{STATE_PATH}.tmp"
        with open(tmp, "w", encoding="utf-8") as fh:
            json.dump(fields, fh, indent=2)
        os.replace(tmp, STATE_PATH)
    except OSError:
        pass


def load() -> dict:
    try:
        with open(STATE_PATH, encoding="utf-8") as fh:
            state = json.load(fh)
        return state if isinstance(state, dict) else {}
    except (OSError, ValueError):
        return {}
