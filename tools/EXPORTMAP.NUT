/*
 * NAC automap — Stage 1 export.
 *
 * Run inside Grid Cartographer with "Nox_Archaist - Realistic Maps.gct"
 * loaded. Walks every region × floor and prints a structured text dump
 * to GC's console; save that console output to a file (default name
 * "maps.txt") and feed it through tools/pack_maps.py to produce the
 * NoxData/maps/maps.bin NAC consumes at runtime.
 *
 * Output format (text, line-based):
 *
 *   NAC_MAPS 1
 *   REGION <region_name> <floor_friendly> <width> <height> <ox> <oy>
 *   <hex byte> <hex byte> ... (width entries)        <-- top row
 *   ...                                              <-- bottom row
 *   REGION ...
 *   END
 *
 * <region_name> is GCRegion.Name() — a string, possibly with spaces.
 * The Python packer maps name → integer region id via the same
 * NoxData/maps/translation.json the runtime already uses, so we don't
 * need GC to expose the numeric id.
 *
 * <ox, oy> is FindBound's min_x / min_y. We export from that origin so
 * sparsely-populated floors don't carry leading empty rows / cols.
 * Runtime uses the (ox, oy) to align the tile grid against the
 * translation.json's xpos/ypos when drawing the player marker.
 *
 * Tile bytes are the 8-bit "Custom Terrain" ids MAPMAKER.NUT writes —
 * one byte per cell, no encoding. Region origins are top-left
 * (origin_tl="true" everywhere in the Nox profile).
 *
 * GC scripting notes — proven against SETALLFOG.NUT / MAPMAKER.NUT:
 *   GCFloor.FindBound()      returns {min_x, min_y, max_x, max_y, ...}
 *   GCRegion.Name()          returns the human-readable name string
 *   GCFloor.FriendlyName()   returns short floor names ("G", "F1", "B2")
 *
 * Tile read API is the one piece we're guessing at. GCTile.Get() with
 * a nested Terrain.Custom accessor mirrors the write form in
 * MAPMAKER.NUT. If GC errors on the call inside getTileCustom(),
 * that's the wrapper to adjust.
 */

function getFloorName() {
    return GCFloor.FriendlyName();
}

function getRegionName() {
    return GCRegion.Name();
}

function getFloorBox() {
    var b = GCFloor.FindBound();
    return [b.min_x, b.min_y,
            b.max_x - b.min_x + 1,
            b.max_y - b.min_y + 1];
}

// Tiles without a Custom Terrain set don't carry the slot at all —
// Squirrel throws "Custom does not exist" on access. Fall back to 0
// so empty cells round-trip as 0x00 in the output. The packer (and
// runtime) treat 0 as "blank / no tile".
function getTileCustom(x, y) {
    GCTile.Select(x, y);
    try {
        var t = GCTile.Get();
        return t.Terrain.Custom;
    } catch (e) {
        return 0;
    }
}

// GC's Squirrel-flavoured runtime exposes neither printf-style format()
// nor String.slice(). Build hex digits via an array of single-char
// strings — array indexing is the access pattern MAPMAKER.NUT uses on
// its tile-data 2D array, so it's known-good against this engine.
HEX_DIGITS <- ["0","1","2","3","4","5","6","7","8","9","A","B","C","D","E","F"];

function hex2(v) {
    return HEX_DIGITS[(v >> 4) & 0xF] + HEX_DIGITS[v & 0xF];
}

function main(args) {
    GCKernel.SetBlockingMode(true);

    // Build the entire dump into a memory buffer, then prompt the user
    // for a save location. Console output isn't viable here — full Nox
    // is ~200 KB of tile bytes which would scroll forever / hit the
    // console's history limit. GCBuffer + GCExport.BufferAs is the
    // pattern MEMDEATH.NUT / MEMSCAN2.NUT use for the same reason.
    var buf = GCBuffer.Create();
    GCBuffer.Print(buf, "NAC_MAPS 1\n");
    GCConsole.Print("Exporting maps...\n");

    var iReg = 0;
    while (iReg < GCRegion.Count()) {
        GCRegion.Select(iReg);
        var rname = getRegionName();
        GCConsole.Print("  " + rname + "\n");

        var iFloor = GCFloor.Lowest();
        while (GCFloor.Exists(iFloor)) {
            GCFloor.Select(iFloor);
            var fname = getFloorName();
            var box   = getFloorBox();
            var ox = box[0], oy = box[1], w = box[2], h = box[3];

            GCBuffer.Print(buf, "REGION \"" + rname + "\" " + fname +
                                " " + w + " " + h + " " + ox + " " + oy + "\n");
            for (var y = 0; y < h; ++y) {
                var row = "";
                for (var x = 0; x < w; ++x) {
                    row += hex2(getTileCustom(ox + x, oy + y)) + " ";
                }
                GCBuffer.Print(buf, row + "\n");
            }
            iFloor++;
        }
        iReg++;
    }
    GCBuffer.Print(buf, "END\n");

    GCExport.BufferAs(buf, "Save NAC maps export...", "maps", "txt", "Text File");
    GCBuffer.Destroy(buf);
    GCConsole.Print("Done.\n");
    GCKernel.SetBlockingMode(false);
}
