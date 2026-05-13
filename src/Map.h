#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nac
{

struct MapLocation
{
    int         region = 0;
    std::string region_name;
    std::string floor;
    int         x = 0;
    int         y = 0;
    int         width  = 0;       // logical region width  (from translation.json)
    int         height = 0;       // logical region height
};

// Resolves the avatar's current memory state (mapID byte at $2AF9 +
// xpos/ypos) to a human-readable MapLocation. Backed by maps_index.json
// — a flat array of pre-resolved records (one per "area"), each
// carrying its mapID, the xy slice it owns, the owning region's
// dimensions, and per-area xoffset/yoffset shifts. Generated from
// translation.json with the rule's offset-3/offset-4 checks folded
// into the xmin/xmax/ymin/ymax bounds, so Resolve doesn't need a
// dynamic check evaluator.
class MapTranslator
{
public:
    void Load(const std::filesystem::path& assetsDir);

    std::optional<MapLocation> Resolve(uint8_t mapID,
                                       uint8_t xpos,
                                       uint8_t ypos) const;

private:
    struct Record
    {
        int         id      = 0;
        int         region  = 0;
        std::string name;
        std::string floor;
        int         width   = 0;
        int         height  = 0;
        int         xmin    = 0, xmax = 255;
        int         ymin    = 0, ymax = 255;
        int         xoffset = 0, yoffset = 0;
    };
    std::vector<Record> m_records;
};

// One floor's geometry + tile bytes, decoded out of maps.bin.
struct FloorRecord
{
    int            region_id = 0;
    std::string    floor;            // "G", "F1", "B1", ...
    int            width    = 0;
    int            height   = 0;
    int            origin_x = 0;     // FindBound's top-left in floor coords
    int            origin_y = 0;
    const uint8_t* tiles    = nullptr;   // points into MapData::m_blob
};


// Loads NoxData/maps/maps.bin (produced by tools/pack_maps.py) and
// indexes it by (region_id, floor) for cheap lookup. Drives the
// teleport combo's list of known floors.
class MapData
{
public:
    void Load(const std::filesystem::path& assetsDir);
    bool Loaded() const { return !m_index.empty(); }

    const FloorRecord* Find(int region_id, const std::string& floor) const;

private:
    std::vector<uint8_t>                            m_blob;
    std::map<std::pair<int, std::string>, FloorRecord> m_index;
};

// 256-tile atlas decoded from Nox's tile shape tables in aux RAM:
//
//   $7000-$7FFF  static tiles, 32 bytes each (2 wide × 16 lines, HGR)
//                — tile id N at $7000 + N * $20, valid 0..$7F (only
//                  0..$74 are actually populated; rest are decoded
//                  too, just as garbage).
//   $8000-$BFFF  animated tiles, 128 bytes each (4 × 32-byte frames),
//                tile id N at $8000 + (N - $80) * $80, valid $80..$FF.
//                We currently only decode frame 0.
//
// Each tile is 14 × 16 monochrome HGR pixels (LSB = leftmost). The
// atlas is laid out as a 16 × 16 grid (224 × 256 px total); UVs:
//   uv0 = (N%16, N/16) × (1/16)
//   uv1 = uv0 + (1/16)
//
// Refresh() rebuilds from the current aux RAM snapshot. Cheap; called
// before each MapPanel render so the atlas tracks any patched tiles.
class TilesetTexture
{
public:
    void Refresh();
    unsigned Tex()      const { return m_tex; }       // monochrome (white on transparent)
    unsigned ColorTex() const { return m_texColor; }  // baked Apple ][ HGR colours
    bool     Has(uint8_t id) const { return m_has[id]; }

    static constexpr int kTileW  = 14;
    static constexpr int kTileH  = 16;
    static constexpr int kCols   = 16;
    static constexpr int kRows   = 16;
    static constexpr int kAtlasW = kCols * kTileW;     // 224
    static constexpr int kAtlasH = kRows * kTileH;     // 256

private:
    void EnsureTexture();
    void DecodeTile(uint8_t id, const uint8_t* src);

    unsigned m_tex          = 0;
    unsigned m_texColor     = 0;
    bool     m_has[256]     = {};
    uint8_t  m_pixels     [kAtlasW * kAtlasH * 4] = {};
    uint8_t  m_pixelsColor[kAtlasW * kAtlasH * 4] = {};
};

// Per-mapID "what tile has the player seen here" map. Each cell
// holds the byte tile-id last observed in Nox's 17×11 visible window
// at $0800, or 0 for "never seen". Driven from the snapshot each
// frame: every non-zero tile in the visible window updates the
// stored id, zeros are skipped (we keep the previous observation).
//
// Keyed by Nox's mapID byte at $2AF9 — the only unique-per-map ID
// the game has. Multiple mapIDs can resolve to the same human-
// readable (region, floor) label (e.g. Bayport's ground floor is
// four separate quadrant mapIDs), so we'd corrupt the stored grid
// if we keyed by (region, floor) — the quadrants would overwrite
// each other. Per-mapID storage gives each map its own grid.
//
// Persisted to <pref_dir>/seen_tiles.json so the discovered map
// survives app restarts.
class TileMap
{
public:
    static constexpr int kVisW = 17;
    static constexpr int kVisH = 11;

    void Load(const std::filesystem::path& prefDir);
    void Save() const;

    // Pull Nox's 17×11 visible buffer (`vis` points to 187 contiguous
    // bytes, row-major) into the per-mapID grid, centred on the
    // player. `fog` points to the matching 17×11 visibility mask at
    // $08BB — byte == 0 means the cell is currently visible; non-zero
    // means hidden. Only cells that are visible AND non-zero get
    // written. The (region_id, floor, region_name) triple is stashed
    // alongside the tile bytes so the dropdown can label the entry
    // without having to re-resolve the translation rule for every
    // stored map (some rules are position-dependent and can't be
    // re-resolved when the player isn't standing on the map).
    void Observe(uint8_t mapID,
                 int region_id, const std::string& floor,
                 const std::string& region_name,
                 int playerX, int playerY,
                 int floorWidth, int floorHeight,
                 const uint8_t* vis, const uint8_t* fog);

    // Tile id at (x, y) in mapID's grid, 0 = never observed.
    uint8_t TileAt(uint8_t mapID, int x, int y) const;

    // Dimensions stored at observation time, false if mapID unknown.
    bool Dims(uint8_t mapID, int& width, int& height) const;

    struct Entry
    {
        uint8_t     map_id;
        int         region_id;
        std::string floor;          // "G", "B1", ... — friendly floor label
        std::string region_name;    // human-readable region name
    };
    // Every mapID with at least one observed (non-zero) tile, with
    // the (region, floor) labels captured at observation time.
    // Drives the Map panel's "Map" pulldown.
    std::vector<Entry> ObservedMaps() const;

private:
    struct Floor
    {
        int                  width = 0, height = 0;
        int                  region_id = 0;
        std::string          floor;
        std::string          region_name;
        std::vector<uint8_t> tiles;    // size = width*height; 0 = unseen
    };
    Floor& EnsureFloor(uint8_t mapID,
                       int region_id, const std::string& floor,
                       const std::string& region_name,
                       int floorWidth, int floorHeight);

    std::filesystem::path     m_path;
    std::map<uint8_t, Floor>  m_floors;
    mutable bool              m_dirty = false;
};

// User-authored note attached to a tile on a specific mapID. text is
// the body; always_visible flips between hover-only tooltip and a
// label drawn permanently next to the marker.
struct MapNote
{
    int         x = 0;
    int         y = 0;
    std::string text;
    bool        always_visible = false;
};

// Per-mapID user notes. Stored at <pref_dir>/annotations.json with
// the same lifecycle as TileMap (load at startup, save on shutdown).
// Notes are a separate concern from observed tiles — user input
// shouldn't share storage / dirty tracking with snapshots of game RAM.
class MapAnnotations
{
public:
    void Load(const std::filesystem::path& prefDir);
    void Save() const;

    // Returns the note at (x,y) for mapID, or nullptr if none exists.
    const MapNote* Find(uint8_t mapID, int x, int y) const;
    // Insert or replace. Empty text deletes (Erase).
    void Set(uint8_t mapID, int x, int y,
             std::string text, bool always_visible);
    void Erase(uint8_t mapID, int x, int y);

    // All notes for a mapID — empty vector if none.
    const std::vector<MapNote>& NotesFor(uint8_t mapID) const;

private:
    std::filesystem::path                   m_path;
    std::map<uint8_t, std::vector<MapNote>> m_notes;
    mutable bool                            m_dirty = false;
};

class MapPanel
{
public:
    enum ColorScheme { CS_White = 0, CS_Green, CS_Amber, CS_Color };

    bool* OpenFlag()    { return &m_open; }
    bool& OpenRef()     { return m_open; }
    int&  ColorSchemeRef() { return m_colorScheme_int; }
    void  Render(const MapTranslator& tx, MapData& md,
                 TilesetTexture& tileset, TileMap& tiles,
                 MapAnnotations& notes);

private:
    bool m_open = false;

    // Map view selector. m_viewMapID == -1 means "follow the live
    // current map" (avatar visible, auto-centred). Anything in [0,255]
    // means "show this stored mapID instead" — observation still
    // writes into the live map, but the panel renders the chosen one
    // and hides the avatar marker.
    int         m_viewMapID = -1;
    // Backed as int so SaveSettings / LoadSettings can round-trip it
    // through nlohmann::json without a custom enum (de)serialiser. The
    // Render path reads this through the ColorScheme enum cast.
    int         m_colorScheme_int = (int)CS_Green;

    // Stored-map pan + zoom-to-fit. Pan is in tile units (added to the
    // floor-centre when computing the on-screen centre tile). m_needFit
    // forces a one-shot zoom-to-fit on the next render — set whenever
    // the user picks a stored map from the combo so we always start
    // framed. Pan resets on the same event.
    float m_panX    = 0.0f;
    float m_panY    = 0.0f;
    bool  m_needFit = false;

    // Note-editor popup state. Set when the user right-clicks a tile
    // in the canvas; consumed by the modal opened the same frame.
    // m_noteEditOpen is the trigger to call OpenPopup() once.
    bool    m_noteEditOpen     = false;
    uint8_t m_noteEditMapID    = 0;
    int     m_noteEditX        = 0;
    int     m_noteEditY        = 0;
    bool    m_noteEditAlwaysVisible = false;
    char    m_noteEditBuf[512] = {};
};

} // namespace nac
