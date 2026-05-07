#!/usr/bin/env python3
"""NAC automap — Stage 2 packer.

Reads the text output of tools/EXPORTMAP.NUT (`maps.txt` by default)
and writes the binary `NoxArchaistCompanion/Assets/maps/maps.bin` that
NAC's MapData loader consumes.

The text input identifies regions by their human-readable name (which
GCRegion.Name() returns); we map that back to the integer region id
via Assets/maps/translation.json (which we already ship).

Binary format (little-endian throughout):

    [4]  magic = b"NMAP"
    [4]  version = 1 (u32)
    [4]  floor_count (u32)
    [floor_count * 20]  FloorRecord:
       [2]  region_id   (u16)
       [4]  floor_name  (zero-padded ASCII, e.g. b"G\0\0\0")
       [2]  width       (u16)
       [2]  height      (u16)
       [2]  origin_x    (i16)
       [2]  origin_y    (i16)
       [4]  data_offset from file start (u32)
       [2]  reserved    (u16, zero)
    [variable]  tile blobs concatenated, in record order

Each tile blob is width*height bytes, row-major top-to-bottom. (origin_x,
origin_y) is FindBound's top-left corner — sparse floors don't carry
leading empty rows / cols. Runtime translates an in-floor position
(fx, fy) to a tile index via (fx - origin_x, fy - origin_y).
"""

import argparse
import json
import re
import shlex
import struct
import sys
from pathlib import Path

REPO_ROOT   = Path(__file__).resolve().parents[1]
DEFAULT_IN  = REPO_ROOT / "tools" / "maps.txt"
DEFAULT_OUT = REPO_ROOT / "NoxArchaistCompanion" / "Assets" / "maps" / "maps.bin"
TRANSLATION = REPO_ROOT / "NoxArchaistCompanion" / "Assets" / "maps" / "translation.json"

MAGIC   = b"NMAP"
VERSION = 1


def load_name_to_id(path: Path) -> dict:
    """{ region_name: region_id } from translation.json's regions table."""
    with path.open("r", encoding="utf-8") as f:
        data = json.load(f)
    out = {}
    for rid, info in data.get("regions", {}).items():
        out[info["name"]] = int(rid)
    return out


def normalise_floor(name: str) -> str:
    """GC's FriendlyName() returns multi-word labels ("Ground Floor",
    "Floor 2", "Basement 1") but the translation.json rules use the
    short-code form ("G", "F2", "B1"). Map between them."""
    s = name.strip()
    if s == "Ground Floor":
        return "G"
    m = re.match(r"^Floor\s+(\d+)$",    s)
    if m: return "F" + m.group(1)
    m = re.match(r"^Basement\s*(\d+)?$", s)
    if m: return "B" + (m.group(1) or "1")
    # Already a short code (or something we don't recognise) — keep as-is
    # so the floor name still round-trips even if we missed a label form.
    return s


def parse_text(path: Path, name_to_id: dict):
    """Yields (region_id, floor_name, width, height, ox, oy, tiles[bytes])."""
    with path.open("r", encoding="utf-8") as f:
        header = f.readline().strip()
        if header != "NAC_MAPS 1":
            raise SystemExit(f"unexpected header: {header!r} (expected 'NAC_MAPS 1')")

        line = f.readline()
        while line:
            stripped = line.strip()
            if stripped == "END":
                return
            if not stripped.startswith("REGION "):
                line = f.readline()
                continue
            # `REGION "Region Name" <floor name> w h ox oy`. Floor name
            # may be multi-word ("Ground Floor"), so peel the last four
            # ints off the end and join whatever's between the quoted
            # region and those ints as the floor name.
            tokens = shlex.split(stripped)
            if len(tokens) < 7 or tokens[0] != "REGION":
                raise SystemExit(f"bad REGION line: {stripped!r}")
            try:
                ox = int(tokens[-2]); oy = int(tokens[-1])
                w  = int(tokens[-4]); h  = int(tokens[-3])
            except ValueError:
                raise SystemExit(f"bad REGION line numbers: {stripped!r}")
            rname = tokens[1]
            fname = normalise_floor(" ".join(tokens[2:-4]))
            if rname not in name_to_id:
                print(f"warning: region {rname!r} not in translation.json — skipping",
                      file=sys.stderr)
                # Drain the rows to keep the parser in sync.
                for _ in range(h):
                    f.readline()
                line = f.readline()
                continue
            rid = name_to_id[rname]
            tiles = bytearray(w * h)
            for y in range(h):
                row = f.readline().split()
                if len(row) != w:
                    raise SystemExit(
                        f"region {rname!r} floor {fname}: row {y} has {len(row)} cells, expected {w}"
                    )
                for x, hexbyte in enumerate(row):
                    tiles[y * w + x] = int(hexbyte, 16)
            yield rid, fname, w, h, ox, oy, bytes(tiles)
            line = f.readline()
        raise SystemExit("missing END marker")


def encode_floor_name(name: str) -> bytes:
    b = name.encode("ascii")
    if len(b) > 4:
        raise SystemExit(f"floor name too long: {name!r}")
    return b + b"\x00" * (4 - len(b))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in",  dest="src", type=Path, default=DEFAULT_IN,
                    help=f"text dump from EXPORTMAP.NUT (default: {DEFAULT_IN})")
    ap.add_argument("--out", dest="dst", type=Path, default=DEFAULT_OUT,
                    help=f"binary output (default: {DEFAULT_OUT})")
    ap.add_argument("--translation", type=Path, default=TRANSLATION,
                    help=f"name→id source (default: {TRANSLATION})")
    args = ap.parse_args()

    if not args.src.exists():
        raise SystemExit(f"input not found: {args.src}")
    if not args.translation.exists():
        raise SystemExit(f"translation.json not found: {args.translation}")

    name_to_id = load_name_to_id(args.translation)
    floors = list(parse_text(args.src, name_to_id))
    if not floors:
        raise SystemExit("no floors parsed")

    args.dst.parent.mkdir(parents=True, exist_ok=True)

    header_size  = 12
    record_size  = 20
    data_section_start = header_size + record_size * len(floors)

    records = bytearray()
    blobs   = bytearray()
    offset  = data_section_start
    for rid, fname, w, h, ox, oy, tiles in floors:
        records += struct.pack("<H4sHHhhIH",
                               rid,
                               encode_floor_name(fname),
                               w, h,
                               ox, oy,
                               offset,
                               0)
        blobs  += tiles
        offset += len(tiles)

    with args.dst.open("wb") as f:
        f.write(MAGIC)
        f.write(struct.pack("<II", VERSION, len(floors)))
        f.write(records)
        f.write(blobs)

    total = args.dst.stat().st_size
    print(f"Wrote {args.dst} ({len(floors)} floors, {total} bytes)")


if __name__ == "__main__":
    main()
