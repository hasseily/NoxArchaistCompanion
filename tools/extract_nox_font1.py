#!/usr/bin/env python3
"""Extract Nox Archaist's built-in FONT1 from na.graphics.dsk.

The utilities disk is a ProDOS volume stored in DOS 3.3 sector order.
FONT1 is a 768-byte file containing 96 consecutive 7x8 glyphs for
character codes $20-$7F, one byte per scanline.
"""

from __future__ import annotations

import argparse
import hashlib
from dataclasses import dataclass
from pathlib import Path


BLOCK_SIZE = 512
SECTOR_SIZE = 256
TRACK_SIZE = 16 * SECTOR_SIZE

# DOS-order sector indices for each half of a ProDOS block.
DOS_SECTORS_BY_BLOCK = (
    (0x0, 0xE),
    (0xD, 0xC),
    (0xB, 0xA),
    (0x9, 0x8),
    (0x7, 0x6),
    (0x5, 0x4),
    (0x3, 0x2),
    (0x1, 0xF),
)


@dataclass(frozen=True)
class Entry:
    name: str
    storage_type: int
    key_block: int
    eof: int


class ProDosDisk:
    def __init__(self, image: bytes):
        if len(image) % TRACK_SIZE:
            raise ValueError("disk image is not a 16-sector image")
        self.image = image

    def read_block(self, block: int) -> bytes:
        track, index = divmod(block, 8)
        if track * TRACK_SIZE >= len(self.image):
            raise ValueError(f"block {block} lies outside the disk image")
        halves = []
        for sector in DOS_SECTORS_BY_BLOCK[index]:
            offset = track * TRACK_SIZE + sector * SECTOR_SIZE
            halves.append(self.image[offset : offset + SECTOR_SIZE])
        return b"".join(halves)

    def directory(self, key_block: int):
        block = key_block
        while block:
            data = self.read_block(block)
            next_block = int.from_bytes(data[2:4], "little")
            for offset in range(4, BLOCK_SIZE, 39):
                entry = data[offset : offset + 39]
                if len(entry) < 39:
                    break
                storage_type = entry[0] >> 4
                name_length = entry[0] & 0x0F
                if not storage_type or not name_length:
                    continue
                name = entry[1 : 1 + name_length].decode("ascii")
                yield Entry(
                    name=name,
                    storage_type=storage_type,
                    key_block=int.from_bytes(entry[17:19], "little"),
                    eof=int.from_bytes(entry[21:24], "little"),
                )
            block = next_block

    def find(self, directory_block: int, name: str) -> Entry:
        wanted = name.upper()
        for entry in self.directory(directory_block):
            if entry.name.upper() == wanted:
                return entry
        raise FileNotFoundError(f"{name} was not found in directory block {directory_block}")

    def read_file(self, entry: Entry) -> bytes:
        if entry.storage_type == 1:  # seedling
            return self.read_block(entry.key_block)[: entry.eof]
        if entry.storage_type != 2:  # sapling
            raise ValueError(
                f"unsupported ProDOS storage type {entry.storage_type} for {entry.name}"
            )

        index = self.read_block(entry.key_block)
        output = bytearray()
        blocks = (entry.eof + BLOCK_SIZE - 1) // BLOCK_SIZE
        for i in range(blocks):
            data_block = index[i] | (index[256 + i] << 8)
            if not data_block:
                output.extend(bytes(BLOCK_SIZE))
            else:
                output.extend(self.read_block(data_block))
        return bytes(output[: entry.eof])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("disk", type=Path, help="na.graphics.dsk")
    parser.add_argument("output", type=Path, help="output FONT1 binary")
    args = parser.parse_args()

    disk = ProDosDisk(args.disk.read_bytes())
    fonts = disk.find(2, "FONTS")
    if fonts.storage_type != 0x0D:
        raise ValueError("FONTS is not a ProDOS subdirectory")
    font1 = disk.find(fonts.key_block, "FONT1")
    data = disk.read_file(font1)
    if len(data) != 0x300:
        raise ValueError(f"FONT1 has {len(data)} bytes; expected 768")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"{args.output}: {len(data)} bytes, sha256 {hashlib.sha256(data).hexdigest()}")


if __name__ == "__main__":
    main()
