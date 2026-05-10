# Automap data export workflow

NAC's automap consumes a single binary file at runtime —
`NoxArchaistCompanion/NoxData/maps/maps.bin` — that holds every region
× floor's tile grid for Nox Archaist. The file is generated from the
authoritative `Nox_Archaist - Realistic Maps.gct` map in the upstream
[`GridCartographer-Mapping`](https://github.com/hasseily/GridCartographer-Mapping)
repo via a two-stage pipeline. NAC itself never reads the `.gct` —
the export is a one-off step (re-run when the .gct changes).

## Stage 1 — Grid Cartographer (Windows-only)

1. Open Grid Cartographer (the `.gct` format requires it; GC is
   Windows-only).
2. Load `Maps/Nox_Archaist - Realistic Maps.gct` from
   `GridCartographer-Mapping`.
3. Run `tools/EXPORTMAP.NUT` from this repo. It walks every region ×
   floor and prints a structured text dump to GC's console.
4. Save the console output to `tools/maps.txt`.

The script's region-name / floor-name / FindBound calls are proven
against the existing scripts in `GridCartographer-Mapping/Scripts/`.
The one wrapper that's still a guess is `getTileCustom` — if GC
errors there, that's the only function to adjust.

## Stage 2 — pack to binary (cross-platform)

```bash
python tools/pack_maps.py
```

Reads `tools/maps.txt`, writes
`NoxArchaistCompanion/NoxData/maps/maps.bin`. Idempotent. Add `--in`
or `--out` for non-default paths.

## Binary layout

Little-endian throughout.

```
[4]  magic = "NMAP"
[4]  version = 1 (u32)
[4]  floor_count (u32)
[floor_count × 20]  FloorRecord[]:
   [2]  region_id   (u16)
   [4]  floor_name  (zero-padded ASCII, e.g. "G\0\0\0")
   [2]  width       (u16)
   [2]  height      (u16)
   [2]  origin_x    (i16)
   [2]  origin_y    (i16)
   [4]  data_offset from file start (u32)
   [2]  reserved    (u16, zero)
[variable]  tile blobs concatenated, in record order
```

Each tile blob is `width*height` bytes, row-major top-to-bottom. One
byte per cell — the "Custom Terrain" id GC uses (see
`Scripts/MAPMAKER.NUT` in the mapping repo for the encoding).
`(origin_x, origin_y)` is FindBound's top-left corner — sparse floors
don't carry leading empty rows / cols. The runtime translates an
in-floor position `(fx, fy)` to a tile index via
`(fx - origin_x, fy - origin_y)`.

Estimated total size: 26 regions × ~3 floors avg × ~50×50 tiles ≈
200 KB. Loaded once at startup; fixed-size FloorRecord array means a
straight `(region_id, floor)` → `FloorRecord*` hash map.

## Tileset PNG

Separate from `maps.bin` — `NoxData/maps/tileset.png`, 14×16 px tiles
in a grid. Source TBD (extracted from the `.gct` itself or from
`GridCartographer-Mapping/Tilesets/`). Loaded as a single GL texture
at startup; tile id N indexes into the grid.
