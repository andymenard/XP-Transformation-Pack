"""Local .theme snapshots: visual style/classic metrics plus desktop settings."""
import configparser
import os
from pathlib import Path
from urllib.parse import unquote, urlparse

import classic_gen as cg
import hostexec as host
import paths
import theme_file as tf


def current_desktop():
    """Read Plasma's saved image/solid-color desktop without changing it."""
    config = configparser.ConfigParser(interpolation=None, strict=False)
    filename = os.path.join(paths.REAL_HOME, ".config/plasma-org.kde.plasma.desktop-appletsrc")
    config.read_string(host.read_text(filename) or "")
    desktops = []
    for section in config.sections():
        if not section.startswith("Containments][") or section.count("][") != 1:
            continue
        if config.get(section, "plugin", fallback="") not in ("org.kde.desktop", "org.kde.plasma.folder"):
            continue
        if config.getint(section, "lastScreen", fallback=0) < 0:
            continue
        plugin = config.get(section, "wallpaperplugin", fallback="org.kde.image")
        settings = section + "][Wallpaper][" + plugin + "][General"
        if plugin not in ("org.kde.image", "org.kde.color"):
            raise ValueError("Saving this wallpaper plugin is not supported yet.")
        image = config.get(settings, "Image", fallback="") if plugin == "org.kde.image" else ""
        if image.startswith("file:"):
            image = unquote(urlparse(image).path)
        if plugin == "org.kde.image" and not os.path.isfile(image):
            raise ValueError("The current wallpaper is not a readable image file.")
        value = config.get(settings, "Color", fallback="#3a6ea5")
        if value.startswith("#") and len(value) in (7, 9):
            rgb = tuple(int(value[i:i + 2], 16) for i in range(len(value) - 6, len(value), 2))
        else:
            rgb = tuple(int(v) for v in value.split(",")[:3])
        desktops.append((image or None, config.getint(settings, "FillMode", fallback=2), rgb))
    if not desktops:
        raise ValueError("Could not read the current Plasma desktop settings.")
    if any(d != desktops[0] for d in desktops):
        raise ValueError("Different wallpapers on multiple desktops cannot be saved as one .theme yet.")
    image, fill, color = desktops[0]
    return {"image": image, "fill": fill, "color": color}


def destination(name):
    slug = "".join(c if c.isalnum() else "-" for c in name).strip("-") or "Theme"
    return Path(paths.THEMES) / "Saved" / (slug + ".theme")


def owned(path):
    path = Path(path)
    return (not path.is_symlink() and path.parent == Path(paths.THEMES) / "Saved"
            and path.suffix == ".theme"
            and tf.load_theme_file(str(path)).get("xp2plasma", {}).get("saved") == "true")


def save(name, style, colour, size, scheme, desktop, base=None):
    """Save a complete supported preset; never overwrite an existing file."""
    if any(c in name for c in "\r\n"):
        raise ValueError("Theme names cannot contain line breaks.")
    sections = tf.load_theme_file(base) if base else {}
    # Rebase known asset paths when copying an existing bundle to Saved/.
    if base:
        for values in sections.values():
            for key, value in list(values.items()):
                if "\\" in value or "/" in value:
                    resolved = tf.resolve_asset(base, value)
                    if resolved:
                        values[key] = resolved
    sections["theme"] = {"displayname": name}
    sections["xp2plasma"] = {"saved": "true"}
    if style.is_classic:
        sections.pop("visualstyles", None)
        sections.update(cg.theme_sections(scheme))
    else:
        sections["visualstyles"] = {"path": os.path.abspath(style.path),
                                   "colorstyle": style.colour_name(colour),
                                   "size": style.size_name(size)}
    sections.setdefault("control panel\\colors", {})["background"] = " ".join(map(str, desktop["color"]))
    sections["control panel\\desktop"] = {
        "wallpaper": desktop["image"] or "",
        "wallpaperstyle": {0: "2", 1: "6", 2: "10", 3: "0", 6: "0"}.get(desktop["fill"], "10"),
        "tilewallpaper": "1" if desktop["fill"] == 3 else "0"}
    result = destination(name)
    result.parent.mkdir(parents=True, exist_ok=True)
    text = "\r\n".join("[" + section + "]\r\n" + "\r\n".join(k + "=" + str(v) for k, v in values.items())
                           + "\r\n" for section, values in sections.items())
    with result.open("x", encoding="utf-16") as output:
        output.write(text)
    return str(result)
