#!/usr/bin/env python3
"""Extract DirectXTK .spritefont glyphs, atlas pixels, and metrics."""

from __future__ import annotations

import argparse
import json
import struct
import zlib
from dataclasses import asdict, dataclass
from pathlib import Path


MAGIC = b"DXTKfont"
DXGI_FORMAT_R8G8B8A8_UNORM = 28
DXGI_FORMAT_BC2_UNORM = 74
DXGI_FORMAT_B4G4R4A4_UNORM = 115


@dataclass(frozen=True)
class Glyph:
    character: int
    left: int
    top: int
    right: int
    bottom: int
    x_offset: float
    y_offset: float
    x_advance: float


@dataclass(frozen=True)
class SpriteFont:
    glyphs: list[Glyph]
    line_spacing: float
    default_character: int
    width: int
    height: int
    texture_format: int
    stride: int
    rows: int
    pixels: bytes


def parse_spritefont(path: Path) -> SpriteFont:
    data = path.read_bytes()
    if not data.startswith(MAGIC):
        raise ValueError(f"{path}: not a DirectXTK sprite font")

    glyph_count = struct.unpack_from("<I", data, len(MAGIC))[0]
    offset = len(MAGIC) + 4
    glyphs = []
    for _ in range(glyph_count):
        values = struct.unpack_from("<Iiiii3f", data, offset)
        glyphs.append(Glyph(*values))
        offset += struct.calcsize("<Iiiii3f")

    line_spacing, default_character, width, height, texture_format, stride, rows = (
        struct.unpack_from("<f6I", data, offset)
    )
    offset += struct.calcsize("<f6I")
    pixels = data[offset:]
    expected_size = stride * rows
    if len(pixels) != expected_size:
        raise ValueError(
            f"{path}: texture payload is {len(pixels)} bytes, expected {expected_size}"
        )

    return SpriteFont(
        glyphs,
        line_spacing,
        default_character,
        width,
        height,
        texture_format,
        stride,
        rows,
        pixels,
    )


def _rgb565(value: int) -> tuple[int, int, int]:
    r = ((value >> 11) & 31) * 255 // 31
    g = ((value >> 5) & 63) * 255 // 63
    b = (value & 31) * 255 // 31
    return r, g, b


def _decode_bc2(font: SpriteFont) -> bytes:
    rgba = bytearray(font.width * font.height * 4)
    blocks_wide = (font.width + 3) // 4
    for by in range((font.height + 3) // 4):
        for bx in range(blocks_wide):
            block_offset = (by * blocks_wide + bx) * 16
            alpha_bits, color0, color1, color_bits = struct.unpack_from(
                "<QHHI", font.pixels, block_offset
            )
            rgb0 = _rgb565(color0)
            rgb1 = _rgb565(color1)
            colors = (
                rgb0,
                rgb1,
                tuple((2 * a + b) // 3 for a, b in zip(rgb0, rgb1)),
                tuple((a + 2 * b) // 3 for a, b in zip(rgb0, rgb1)),
            )
            for py in range(4):
                for px in range(4):
                    pixel = py * 4 + px
                    x = bx * 4 + px
                    y = by * 4 + py
                    if x >= font.width or y >= font.height:
                        continue
                    r, g, b = colors[(color_bits >> (pixel * 2)) & 3]
                    a = ((alpha_bits >> (pixel * 4)) & 15) * 17
                    out = (y * font.width + x) * 4
                    rgba[out : out + 4] = bytes((r, g, b, a))
    return bytes(rgba)


def decode_rgba(font: SpriteFont) -> bytes:
    if font.texture_format == DXGI_FORMAT_R8G8B8A8_UNORM:
        return font.pixels
    if font.texture_format == DXGI_FORMAT_BC2_UNORM:
        return _decode_bc2(font)
    if font.texture_format == DXGI_FORMAT_B4G4R4A4_UNORM:
        rgba = bytearray(font.width * font.height * 4)
        for pixel, (value,) in enumerate(struct.iter_unpack("<H", font.pixels)):
            b = (value & 15) * 17
            g = ((value >> 4) & 15) * 17
            r = ((value >> 8) & 15) * 17
            a = ((value >> 12) & 15) * 17
            rgba[pixel * 4 : pixel * 4 + 4] = bytes((r, g, b, a))
        return bytes(rgba)
    raise ValueError(f"unsupported DXGI texture format {font.texture_format}")


def write_png(path: Path, width: int, height: int, rgba: bytes) -> None:
    def chunk(kind: bytes, payload: bytes) -> bytes:
        body = kind + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body))

    scanlines = b"".join(
        b"\0" + rgba[y * width * 4 : (y + 1) * width * 4] for y in range(height)
    )
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(scanlines, 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def write_metadata(path: Path, source: Path, font: SpriteFont) -> None:
    metadata = {
        "source": source.as_posix(),
        "line_spacing": font.line_spacing,
        "default_character": font.default_character,
        "texture": {
            "width": font.width,
            "height": font.height,
            "dxgi_format": font.texture_format,
            "stride": font.stride,
            "rows": font.rows,
        },
        "glyphs": [asdict(glyph) for glyph in font.glyphs],
    }
    path.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


def render_preview(font: SpriteFont, rgba: bytes) -> tuple[int, int, bytes]:
    lines = [
        "Nox Archaist Companion",
        "The quick brown fox jumps over the lazy dog.",
        "!\"#$%&'()*+,-./0123456789:;<=>?",
        "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_",
        "`abcdefghijklmnopqrstuvwxyz{|}~",
    ]
    by_character = {glyph.character: glyph for glyph in font.glyphs}

    def line_width(text: str) -> int:
        x = 0.0
        for character in text:
            glyph = by_character[ord(character)]
            x += glyph.x_offset + glyph.right - glyph.left + glyph.x_advance
        return max(0, round(x))

    padding = 6
    width = max(line_width(line) for line in lines) + padding * 2
    height = round(font.line_spacing * len(lines)) + padding * 2
    output = bytearray((0, 0, 0, 255) * (width * height))
    for row, line in enumerate(lines):
        x = float(padding)
        y = padding + row * font.line_spacing
        for character in line:
            glyph = by_character[ord(character)]
            x += glyph.x_offset
            for glyph_y in range(glyph.top, glyph.bottom):
                for glyph_x in range(glyph.left, glyph.right):
                    src = (glyph_y * font.width + glyph_x) * 4
                    alpha = rgba[src + 3]
                    dst_x = round(x) + glyph_x - glyph.left
                    dst_y = round(y + glyph.y_offset) + glyph_y - glyph.top
                    if 0 <= dst_x < width and 0 <= dst_y < height:
                        dst = (dst_y * width + dst_x) * 4
                        output[dst : dst + 4] = bytes((alpha, alpha, alpha, 255))
            x += glyph.right - glyph.left + glyph.x_advance
    return width, height, bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output_stem", type=Path)
    args = parser.parse_args()

    font = parse_spritefont(args.input)
    args.output_stem.parent.mkdir(parents=True, exist_ok=True)
    rgba = decode_rgba(font)
    write_png(
        args.output_stem.with_suffix(".png"),
        font.width,
        font.height,
        rgba,
    )
    preview_width, preview_height, preview_rgba = render_preview(font, rgba)
    write_png(
        args.output_stem.with_name(args.output_stem.name + "-preview").with_suffix(".png"),
        preview_width,
        preview_height,
        preview_rgba,
    )
    write_metadata(args.output_stem.with_suffix(".json"), args.input, font)
    print(
        f"{args.input}: {len(font.glyphs)} glyphs, "
        f"{font.width}x{font.height}, line spacing {font.line_spacing:g}"
    )


if __name__ == "__main__":
    main()
