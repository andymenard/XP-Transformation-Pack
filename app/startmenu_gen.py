"""Theme-derived assets for the XP Start menu.

No desktop writes or bundled Microsoft artwork. Every image is extracted from
the supplied .msstyles; measurements and provisional metrics are kept separate.
"""
from pathlib import Path
import json
import re
from msstyles_parse import MsStyle, apply_colorkey, parse_color


def numbers(value):
    return [int(v) for v in re.split(r"[,\s]+", value.strip()) if v]


def color(value):
    return "#%02x%02x%02x" % parse_color(value)


def font(value):
    fields = [v.strip() for v in value.split(",")]
    return {"family": fields[0], "pixels": round(int(fields[1]) * 96 / 72),
            "bold": "bold" in value.lower(), "italic": "italic" in value.lower()}


def export(theme, destination):
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    parts = {}
    for part in sorted(theme.parts("startpanel")):
        for state in ("", "hot"):
            if state and ("startpanel", part, state) not in theme.text_sections:
                continue
            effective = theme.effective("startpanel", part, state)
            if "imagefile" not in effective:
                continue
            key = part + ("-hot" if state else "")
            image = theme.get_bitmap(effective["imagefile"])
            if not image.info.get("has_alpha") and effective.get("transparent", "").lower() == "true":
                image = apply_colorkey(image, parse_color(effective.get("transparentcolor", "255 0 255")))
            output = destination / (key + ".png")
            image.save(output)
            spec = {"url": output.as_uri(), "width": image.width, "height": image.height,
                    "sizing": numbers(effective.get("sizingmargins", "0 0 0 0")),
                    "content": numbers(effective.get("contentmargins", "0 0 0 0")),
                    "source": effective["imagefile"], "properties": effective}
            # DefaultPaneSize cascades from StartPanel, but that 380x440 value
            # is not the size of parts (e.g. the 55x55 picture frame).
            own = theme.text_sections[("startpanel", part, state)]
            spec["pane"] = numbers(own["defaultpanesize"])[2:] if "defaultpanesize" in own else None
            parts[key] = spec
            if part == "logoffbuttons":
                if image.width != 3 * image.height:
                    raise ValueError("Unexpected StartPanel footer button strip layout")
                for index, name in enumerate(("undock", "logoff", "shutdown")):
                    button_path = destination / ("button-" + name + ("-hot" if state else "") + ".png")
                    image.crop((index * image.height, 0, (index + 1) * image.height, image.height)).save(button_path)
                    spec[name] = button_path.as_uri()

    required = {"userpane", "userpicture", "proglist", "proglistseparator", "moreprograms",
                "moreprogramsarrow", "placeslist", "placeslistseparator", "logoff", "logoffbuttons"}
    if not required.issubset(parts):
        raise ValueError("Theme is missing StartPanel artwork: " + ", ".join(sorted(required - parts.keys())))
    submenu = theme.effective("startmenu::toolbar")
    if "imagefile" in submenu:
        image = theme.get_background_bitmap("startmenu::toolbar", "")
        output = destination / "submenu.png"
        image.save(output)
        parts["submenu"] = {"url": output.as_uri(), "sizing": numbers(submenu.get("sizingmargins", "0 0 0 0"))}
    # User-measured Luna footer: symmetric fixed caps around a 1px stretch.
    # Keep the original 49/47 INI values in provenance, and never impose this
    # Luna-specific correction on Royale/Zune/etc.
    if parts["logoff"]["source"].replace("\\", "/").lower() == "blue/startpanellogoffbackground.bmp":
        parts["logoff"]["sizing"][:2] = [48, 48]

    metrics = theme.effective("sysmetrics")
    palette = {
        "programText": color(parts["proglist"]["properties"]["textcolor"]),
        # StartPanel can be dark while its All Programs flyouts stay white.
        # Their normal/hot text colors belong to StartMenu, not StartPanel.
        "submenuText": color(submenu.get("textcolor", metrics.get("menutext", "0 0 0"))),
        "submenuHighlightText": color(theme.effective("startmenu::toolbar(hot)").get(
            "textcolor", metrics.get("highlighttext", "255 255 255"))),
        "secondaryText": color(parts["proglist"]["properties"].get("captiontext", "128 128 128")),
        "placesText": color(parts["placeslist"]["properties"]["textcolor"]),
        "userText": color(parts["userpane"]["properties"]["textcolor"]),
        "userShadow": color(parts["userpane"]["properties"].get("textshadowcolor", "0 0 0")),
        "footerText": color(parts["logoff"]["properties"]["textcolor"]),
        "highlight": color(metrics.get("highlight", "49 106 197")),
        "highlightText": color(metrics.get("highlighttext", "255 255 255")),
    }
    result = {
        "source": str(Path(theme.path).resolve()), "variant": theme._ini_resource_name,
        "parts": parts, "palette": palette,
        "font": font(metrics.get("menufont", "Tahoma, 8")),
        "userFont": font(parts["userpane"]["properties"]["font"]),
        "moreFont": font(parts["moreprograms"]["properties"].get("font", "Tahoma, 8, Bold")),
        "userShadowOffset": numbers(parts["userpane"]["properties"].get("textshadowoffset", "0 0")),
        "layout": {"width": 380, "height": 478,
                   "headerHeight": parts["userpane"]["pane"][1],
                   "footerHeight": parts["logoff"]["pane"][1],
                   "leftWidth": parts["proglist"]["pane"][0],
                   "moreHeight": parts["moreprograms"]["pane"][1] + 14},
        "provisional": {"pinnedRowHeight": 38, "recentRowHeight": 38,
                        "moreExtraHeight": 14, "moreTopGap": 2, "moreBottomExtra": 6,
                        "placesRowHeight": 30, "separatorGap": 8,
                        "userPictureX": 7, "userPictureY": 4, "userNameGap": 8},
        "measurements": {"overall": "380x478 (user measured)",
                         "footer": "Luna blue: 48 / stretch 1 / 48 (user measured)",
                         "separators": "Native size, centered in their respective columns (user confirmed)"},
    }
    (destination / "layout.json").write_text(json.dumps(result, indent=2) + "\n")
    return result
