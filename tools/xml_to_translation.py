#!/usr/bin/env python3
"""Convert ../GridCartographer-Mapping/Profiles/Nox_Archaist.xml into the
NAC automap's translation.json. Run once after pulling a fresh profile
from the upstream mapping repo. Output is checked into the NAC tree;
NAC itself never reads the XML at runtime.

The XML's `<packetview>` rules each describe: given the current peeked
state (mapID, region, maptype, xpos, ypos), if the rule's checks pass
and (xpos, ypos) lie in the rule's range, the player is in
(target_region, floor, xpos+dx, ypos+dy).

We only need three of the five peeked bytes for translation:
    offset 0 -> mapID  (the disambiguator most rules check on)
    offset 3 -> xpos
    offset 4 -> ypos
The XML peeks `region` and `maptype` too but no <check offset> in any
rule references them, so we drop them from the runtime model.
"""

import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

XML_PATH  = Path(__file__).resolve().parents[1].parent / "GridCartographer-Mapping" / "Profiles" / "Nox_Archaist.xml"
JSON_PATH = Path(__file__).resolve().parents[1] / "NoxArchaistCompanion" / "Assets" / "maps" / "translation.json"


def hexint(s, default=None):
    """Parse a hex literal like 'ff' or '0x42' or 'B'. Returns int or default."""
    if s is None:
        return default
    s = s.strip()
    try:
        return int(s, 16)
    except ValueError:
        return default


def parse_class_defaults(root):
    """Pull xpos/ypos defaults from the <class name="Standard"> block. We
    only end up using min/max — the offset is fixed (3, 4) by the peek
    declaration."""
    defaults = {"xmin": 0, "xmax": 0xFF, "ymin": 0, "ymax": 0xFF}
    for cls in root.findall(".//class"):
        if cls.get("name") != "Standard":
            continue
        x = cls.find("xpos")
        y = cls.find("ypos")
        if x is not None:
            defaults["xmin"] = hexint(x.get("min"), 0)
            defaults["xmax"] = hexint(x.get("max"), 0xFF)
        if y is not None:
            defaults["ymin"] = hexint(y.get("min"), 0)
            defaults["ymax"] = hexint(y.get("max"), 0xFF)
    return defaults


def parse_regions(root):
    """{ region_id: { name, width, height } } — drives map sizing.

    GC's <grid width=N height=N tilex=T tiley=T> uses width / height as
    the macro-grid cell count and tilex / tiley as the number of
    sub-tiles per macro cell. The actual tile count of the region is
    width * tilex by height * tiley — that's what NAC's map render
    needs (Wynmar is 16x16 macro × 16x16 sub = 256x256 tiles).
    """
    out = {}
    for r in root.findall(".//region"):
        rid = int(r.get("id"))
        grid = r.find("grid")
        if grid is None:
            out[rid] = {"name": r.get("name"), "width": 0, "height": 0}
            continue
        gw = int(grid.get("width"))
        gh = int(grid.get("height"))
        tx = int(grid.get("tilex", "1"))
        ty = int(grid.get("tiley", "1"))
        out[rid] = {
            "name":   r.get("name"),
            "width":  gw * tx,
            "height": gh * ty,
        }
    return out


def packetview_to_rule(pv, defaults):
    """Single <packetview> -> dict. Returns None for rules we can't
    represent without extending the schema (only the Depths of Vacous
    <floor> directive — handled separately by emit_special_floor)."""
    region = int(pv.get("region"))

    rule = {
        "region": region,
        "floor":  "",
        "checks": [],
        "xmin":   defaults["xmin"],
        "xmax":   defaults["xmax"],
        "ymin":   defaults["ymin"],
        "ymax":   defaults["ymax"],
        "dx":     0,
        "dy":     0,
    }

    for c in pv.findall("check"):
        offset = int(c.get("offset"))
        value  = hexint(c.get("value"), 0)
        op     = c.get("op", "EQ")
        rule["checks"].append({"offset": offset, "value": value, "op": op})

    x = pv.find("xpos")
    if x is not None:
        rule["xmin"] = hexint(x.get("min"), rule["xmin"])
        rule["xmax"] = hexint(x.get("max"), rule["xmax"])
    y = pv.find("ypos")
    if y is not None:
        rule["ymin"] = hexint(y.get("min"), rule["ymin"])
        rule["ymax"] = hexint(y.get("max"), rule["ymax"])

    move = pv.find("move")
    if move is not None:
        rule["dx"] = int(move.get("x", "0"))
        rule["dy"] = int(move.get("y", "0"))

    cf = pv.find("const_floor")
    if cf is not None:
        rule["floor"] = (cf.text or "").strip()

    # The one <floor> directive in the XML (Depths of Vacous) — we
    # expand it into five fixed-floor rules below so the runtime stays
    # simple. Returning None here lets the caller skip it.
    if pv.find("floor") is not None and not rule["floor"]:
        return None

    return rule


def emit_special_floor(pv, defaults):
    """Expand <floor offset=0 min=61 max=65 dir=down adjust=96> into five
    fixed-floor rules (B1..B5) so the runtime never has to interpret the
    <floor> directive."""
    f = pv.find("floor")
    if f is None or f.get("dir") != "down":
        return []
    lo = hexint(f.get("min"), 0)
    hi = hexint(f.get("max"), 0)
    region = int(pv.get("region"))

    rules = []
    for n, mid in enumerate(range(lo, hi + 1), start=1):
        rules.append({
            "region": region,
            "floor":  f"B{n}",
            "checks": [{"offset": 0, "value": mid, "op": "EQ"}],
            "xmin":   defaults["xmin"],
            "xmax":   defaults["xmax"],
            "ymin":   defaults["ymin"],
            "ymax":   defaults["ymax"],
            "dx":     0,
            "dy":     0,
        })
    return rules


def main():
    if not XML_PATH.exists():
        print(f"XML not found: {XML_PATH}", file=sys.stderr)
        sys.exit(1)

    tree = ET.parse(XML_PATH)
    root = tree.getroot()

    defaults = parse_class_defaults(root)
    regions  = parse_regions(root)

    rules = []
    for pv in root.findall(".//views/packetview"):
        if pv.find("floor") is not None and pv.find("const_floor") is None:
            rules.extend(emit_special_floor(pv, defaults))
            continue
        rule = packetview_to_rule(pv, defaults)
        if rule is not None:
            rules.append(rule)

    out = {
        "regions": {str(k): v for k, v in sorted(regions.items())},
        "rules":   rules,
    }

    JSON_PATH.parent.mkdir(parents=True, exist_ok=True)
    with JSON_PATH.open("w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
    print(f"Wrote {JSON_PATH} ({len(rules)} rules, {len(regions)} regions)")


if __name__ == "__main__":
    main()
