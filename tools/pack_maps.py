#!/usr/bin/env python3
"""NAC automap — Stage 2 packer.

Reads the text output of tools/export_maps.nut (`maps.txt` by default)
and writes the binary `NoxArchaistCompanion/Assets/maps/maps.bin` that
NAC's MapData loader consumes.

Format (little-endian throughout):

    [4]  magic = b"NMAP"
    [4]  version = 1 (u32)
    [4]  floor_count (u32)
    [floor_count * 16]  FloorRecord:
       [2]  region_id  (u16)
       [4]  floor_name (zero-padded ASCII, e.g. b"G\0\0\0")
       [2]  width      (u16)
       [2]  height     (u16)
       [4]  data_offset from file start (u32)
       [2]  reserved   (u16, zero)
    [variable]  tile blobs concatenated, in record order

Each tile blob is width*height bytes, row-major top-to-bottom.
"""

import argparse
import struct
import sys
from pathlib import Path

REPO_ROOT  = Path(__file__).resolve().parents[1]
DEFAULT_IN = REPO_ROOT / "tools" / "maps.txt"
DEFAULT_OUT = REPO_ROOT / "NoxArchaistCompanion" / "Assets" / "maps" / "maps.bin"

MAGIC   = b"NMAP"
VERSION = 1


def parse_text(path: Path):
    """Yields (region_id, floor_name, width, height, tiles[bytes])."""
    with path.open("r", encoding="utf-8") as f:
        header = f.readline().strip()
        if header != "NAC_MAPS 1":
            raise SystemExit(f"unexpected header: {header!r} (expected 'NAC_MAPS 1')")

        line = f.readline()
        while line:
            line = line.strip()
            if line == "END":
                return
            if not line.startswith("REGION "):
                line = f.readline()
                continue
            _, rid, fname, w, h = line.split()
            rid, w, h = int(rid), int(w), int(h)
            tiles = bytearray(w * h)
            for y in range(h):
                row = f.readline().split()
                if len(row) != w:
                    raise SystemExit(
                        f"region {rid} floor {fname}: row {y} has {len(row)} cells, expected {w}"
                    )
                for x, hexbyte in enumerate(row):
                    tiles[y * w + x] = int(hexbyte, 16)
            yield rid, fname, w, h, bytes(tiles)
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
                    help=f"text dump from export_maps.nut (default: {DEFAULT_IN})")
    ap.add_argument("--out", dest="dst", type=Path, default=DEFAULT_OUT,
                    help=f"binary output (default: {DEFAULT_OUT})")
    args = ap.parse_args()

    if not args.src.exists():
        raise SystemExit(f"input not found: {args.src}")

    floors = list(parse_text(args.src))
    if not floors:
        raise SystemExit("no floors parsed")

    args.dst.parent.mkdir(parents=True, exist_ok=True)

    header_size  = 12
    record_size  = 16
    data_section_start = header_size + record_size * len(floors)

    records = bytearray()
    blobs   = bytearray()
    offset  = data_section_start
    for rid, fname, w, h, tiles in floors:
        records += struct.pack("<H4sHHIH",
                               rid,
                               encode_floor_name(fname),
                               w, h,
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
