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

class MapTranslator
{
public:
    void Load(const std::filesystem::path& assetsDir);
    bool Loaded() const { return !m_data.is_null(); }

    std::optional<MapLocation> Resolve(uint8_t mapID,
                                       uint8_t xpos,
                                       uint8_t ypos) const;

    // Look up a region's human-readable name + full grid dims by id.
    // Returns the name string ("Wynmar", "Bayport", ...) or empty if
    // the region isn't in translation.json. RegionDims fills width /
    // height to the GC profile's grid dims.
    std::string RegionName(int region_id) const;
    void        RegionDims(int region_id, int& width, int& height) const;

    // For teleport: given a target (region, floor), find the first
    // mapID byte ($2AF9) that the rule set maps to that floor. Returns
    // -1 if no rule matches. For floors with multiple sub-region
    // mapIDs (e.g. Bayport's quadrants) we just return the first.
    int FindMapID(int region_id, const std::string& floor) const;

private:
    nlohmann::json m_data;
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


// Loads Assets/maps/maps.bin (produced by tools/pack_maps.py) and
// indexes it by (region_id, floor) for cheap lookup. Drives the
// teleport combo's list of known floors.
class MapData
{
public:
    void Load(const std::filesystem::path& assetsDir);
    bool Loaded() const { return !m_index.empty(); }

    const FloorRecord* Find(int region_id, const std::string& floor) const;

    struct FloorListEntry { int region_id; std::string floor; std::string region_name; };
    std::vector<FloorListEntry> AllFloors(const class MapTranslator& tx) const;

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

// Per-(region, floor) "what tile has the player seen here" map. Each
// cell holds the byte tile-id last observed in Nox's 17×11 visible
// window at $0800, or 0 for "never seen". Driven from the snapshot
// each frame: every non-zero tile in the visible window updates the
// stored id, zeros are skipped (we keep the previous observation).
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
    // bytes, row-major) into the (region, floor) map, centred on the
    // player. `fog` points to the matching 17×11 visibility mask at
    // $08BB — byte == 0 means the cell is currently visible to the
    // player; non-zero means hidden by walls / dark / off-screen edge.
    // Only cells that are both visible AND non-zero get written.
    void Observe(int region_id, const std::string& floor,
                 int playerX, int playerY,
                 int floorWidth, int floorHeight,
                 const uint8_t* vis, const uint8_t* fog);

    // Tile id at (x, y), 0 = never observed.
    uint8_t TileAt(int region_id, const std::string& floor, int x, int y) const;

    // (region_id, floor) pairs with at least one observed (non-zero)
    // tile. Drives the Map panel's "Map" pulldown.
    std::vector<std::pair<int, std::string>> ObservedFloors() const;

private:
    using Key = std::pair<int, std::string>;
    struct Floor
    {
        int width = 0, height = 0;
        std::vector<uint8_t> tiles;    // size = width*height; 0 = unseen
    };
    Floor& EnsureFloor(int region_id, const std::string& floor,
                       int floorWidth, int floorHeight);

    std::filesystem::path m_path;
    std::map<Key, Floor>  m_floors;
    mutable bool          m_dirty = false;
};

class MapPanel
{
public:
    enum ColorScheme { CS_White = 0, CS_Green, CS_Amber, CS_Color };

    bool* OpenFlag()    { return &m_open; }
    bool& OpenRef()     { return m_open; }
    int&  ColorSchemeRef() { return m_colorScheme_int; }
    void  Render(const MapTranslator& tx, MapData& md,
                 TilesetTexture& tileset, TileMap& tiles);

private:
    bool m_open = false;

    // Map view selector. m_viewRegion == -1 means "follow the live
    // current map" (avatar visible, auto-centred). Anything else means
    // "show this stored (region, floor) instead" — observation still
    // writes into the live map, but the panel renders the chosen one
    // and hides the avatar marker.
    int         m_viewRegion = -1;
    std::string m_viewFloor;
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
};

} // namespace nac
