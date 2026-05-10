/*
 * NAC automap — Phase E export.
 *
 * Run inside Grid Cartographer with Nox_Archaist - Realistic Maps.gct
 * loaded. Walks every region × floor and saves a PNG of each via
 * GCExport.Image to OUTPUT_DIR. Edit OUTPUT_DIR below to your liking
 * before running. Once done, copy the resulting PNGs into
 *   NoxArchaistCompanion/Assets/maps/floors/
 * in the NAC source tree; NAC loads them lazily at first display.
 *
 * Filenames: "<RegionName>_<FloorFriendly>.png" — e.g.
 *   "Bayport_Ground Floor.png"
 *   "Depths of Vacous_Basement 3.png"
 * The runtime maps RegionName → region id via the same translation.json
 * the Phase B / C pipeline already uses, and normalises the floor part
 * the same way pack_maps.py does ("Ground Floor" → "G", etc.).
 *
 * Note: GCExport.Image takes an absolute path and overwrites without
 * prompting. Make sure OUTPUT_DIR exists.
 */

OUTPUT_DIR <- @"C:\Users\hasse\Downloads\floors\";

function main(args) {
    GCKernel.SetBlockingMode(true);
    GCConsole.Print("Exporting floor PNGs to " + OUTPUT_DIR + "\n");

    var iReg = 0;
    while (iReg < GCRegion.Count()) {
        GCRegion.Select(iReg);
        var rname = GCRegion.Name();

        var iFloor = GCFloor.Lowest();
        while (GCFloor.Exists(iFloor)) {
            GCFloor.Select(iFloor);
            var fname = GCFloor.FriendlyName();
            var path  = OUTPUT_DIR + rname + "_" + fname + ".png";
            var res   = GCExport.Image(path);
            if (res != 0) {
                GCConsole.Print("  ERROR " + res + " saving " + path + "\n");
            } else {
                GCConsole.Print("  " + rname + " / " + fname + "\n");
            }
            iFloor++;
        }
        iReg++;
    }

    GCConsole.Print("Done.\n");
    GCKernel.SetBlockingMode(false);
}
