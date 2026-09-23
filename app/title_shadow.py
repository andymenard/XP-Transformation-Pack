"""Restore XP title shadows omitted by Aurorae's QtQuick renderer.

Use a user-local override based on the host's own QML. Other Aurorae themes
remain unchanged; only xp2plasma packages supply shadow settings. Supporting
components are imported from KDE, not copied or modified.
"""
from __future__ import annotations

import configparser
import json
from pathlib import Path

import hostexec as host
import paths

MARKER = "// xp2plasma: caption text shadow override"
OVERRIDE = Path(paths.REAL_HOME) / ".local/share/kwin/aurorae/aurorae.qml"

SHADOW_QML = """    Text {
        // A second native-rendered glyph mask, exactly offset, with no blur.
        property var spec: root.xpTitleShadows[auroraeTheme.themeName] || {}
        visible: color.a > 0
        x: caption.x + (spec.x || 0)
        y: caption.y + (spec.y || 0)
        width: caption.width
        height: caption.height
        text: caption.text
        textFormat: caption.textFormat
        font: caption.font
        renderType: caption.renderType
        horizontalAlignment: caption.horizontalAlignment
        verticalAlignment: caption.verticalAlignment
        elide: caption.elide
        color: (decoration.client.active ? spec.active : spec.inactive) || "transparent"
    }
"""


def patch_qml(source: str, system_dir: str, shadows: dict) -> str:
    anchor = "    Text {\n        id: caption"
    if source.count(anchor) != 1 or source.count("    id: root") != 1:
        raise RuntimeError("Unrecognised Aurorae caption layout; title-shadow override not installed")
    source = source.replace("import QtQuick", f'import "{Path(system_dir).as_uri()}" as Upstream\nimport QtQuick', 1)
    source = source.replace("\nDecoration {", "\nUpstream.Decoration {", 1)
    source = source.replace("    AuroraeButtonGroup {", "    Upstream.AuroraeButtonGroup {")
    source = source.replace("    id: root", "    id: root\n    property var xpTitleShadows: " + json.dumps(shadows), 1)
    return MARKER + "\n" + source.replace(anchor, SHADOW_QML + anchor, 1)


def theme_shadows(themes_dir: str) -> dict:
    shadows = {}
    for package in Path(themes_dir).iterdir():
        metadata = package / "metadata.desktop"
        if not metadata.is_file() or not any(m in metadata.read_text() for m in ("XPLasma", "xp2plasma")):
            continue
        cp = configparser.ConfigParser(interpolation=None)
        cp.read(package / f"{package.name}rc")
        if not cp.getboolean("General", "UseTextShadow", fallback=False):
            continue

        def colour(key):
            rgba = [int(v) for v in cp.get("General", key, fallback="0,0,0,0").split(",")]
            if len(rgba) == 3:
                rgba.append(255)
            r, g, b, a = rgba
            return f"#{a:02x}{r:02x}{g:02x}{b:02x}"

        shadows[package.name] = {
            "active": colour("ActiveTextShadowColor"),
            "inactive": colour("InactiveTextShadowColor"),
            "x": cp.getint("General", "TextShadowOffsetX", fallback=1),
            "y": cp.getint("General", "TextShadowOffsetY", fallback=1),
        }
    return shadows


def install(themes_dir: str) -> None:
    if OVERRIDE.exists() and MARKER not in OVERRIDE.read_text():
        raise RuntimeError(f"Refusing to replace your existing Aurorae override: {OVERRIDE}")
    for system_dir in ("/usr/local/share/kwin/aurorae", "/usr/share/kwin/aurorae"):
        source = host.read_text(f"{system_dir}/aurorae.qml")
        if source:
            break
    else:
        raise RuntimeError("Cannot find the host Aurorae renderer")
    patched = patch_qml(source, system_dir, theme_shadows(themes_dir))
    OVERRIDE.parent.mkdir(parents=True, exist_ok=True)
    staged = OVERRIDE.with_suffix(".qml.new")
    staged.write_text(patched)
    staged.replace(OVERRIDE)


def remove() -> None:
    if OVERRIDE.is_file() and MARKER in OVERRIDE.read_text():
        OVERRIDE.unlink()
