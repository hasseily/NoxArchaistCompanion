/*
 * NAC automap — Stage 1 export.
 *
 * Run inside Grid Cartographer with "Nox_Archaist - Realistic Maps.gct"
 * loaded. Walks every region × floor and prints a structured text dump
 * to GC's console; save that console output to a file (default name
 * "maps.txt") and feed it through tools/pack_maps.py to produce the
 * Assets/maps/maps.bin NAC consumes at runtime.
 *
 * Output format (text, line-based):
 *
 *   NAC_MAPS 1
 *   REGION <region_id> <floor_friendly> <width> <height>
 *   <hex byte> <hex byte> ... (width entries)        <-- row 0 (top)
 *   ...                                              <-- row height-1
 *   REGION ...
 *   END
 *
 * Tile bytes are the 8-bit "Custom Terrain" IDs MAPMAKER.NUT writes —
 * one byte per cell, no encoding. Region origins are top-left
 * (origin_tl="true" everywhere in the Nox profile).
 *
 * GC scripting note: this script assumes
 *   GCRegion.Active()           returns the integer region id
 *   GCRegion.GridWidth() / .GridHeight()  return floor dims
 *   GCTile.Get().Terrain.Custom returns the byte we want
 * If the GC API differs in your version, adjust the four wrappers
 * marked with "API:" below — the rest of the script is independent.
 */

function getRegionId() {
    // API: id of the currently selected region. EXPORTALL.NUT prints
    // GCRegion.Active() as a name in some versions and as an id in
    // others. If this dumps names instead of ints, replace with whatever
    // returns the integer id (e.g. GCRegion.ActiveId()).
    return GCRegion.Active();
}

function getFloorName() {
    // API: short floor name like "G", "F1", "B2".
    return GCFloor.FriendlyName();
}

function getRegionDims() {
    // API: returns [width, height]. The Nox profile XML calls them
    // grid.width / grid.height so the GC API is probably the same name.
    return [GCRegion.GridWidth(), GCRegion.GridHeight()];
}

function getTileCustom(x, y) {
    // API: 8-bit Custom Terrain id at (x, y). MAPMAKER.NUT sets it via
    // GCTile.Set({Terrain = {Custom = ...}}); the read side mirrors.
    GCTile.Select(x, y);
    var t = GCTile.Get();
    return t.Terrain.Custom;
}

function main(args) {
    GCKernel.SetBlockingMode(true);
    GCConsole.Print("NAC_MAPS 1\n");

    var iReg = 0;
    while (iReg < GCRegion.Count()) {
        GCRegion.Select(iReg);
        var rid = getRegionId();

        var iFloor = GCFloor.Lowest();
        while (GCFloor.Exists(iFloor)) {
            GCFloor.Select(iFloor);
            var fname = getFloorName();
            var dims  = getRegionDims();
            var w = dims[0];
            var h = dims[1];

            GCConsole.Print("REGION " + rid + " " + fname + " " + w + " " + h + "\n");
            for (var y = 0; y < h; ++y) {
                var row = "";
                for (var x = 0; x < w; ++x) {
                    var v = getTileCustom(x, y);
                    if (v < 16) row += "0";
                    row += format("%X ", v);
                }
                GCConsole.Print(row + "\n");
            }
            iFloor++;
        }
        iReg++;
    }

    GCConsole.Print("END\n");
    GCKernel.SetBlockingMode(false);
}
