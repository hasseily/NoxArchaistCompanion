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

    // Wipe everything — for the "Clear" button. Marks dirty so the
    // next Save flushes an empty file (and removes the old contents).
    void Clear();

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
    bool* OpenFlag()    { return &m_open; }
    bool& OpenRef()     { return m_open; }
    void  Render(const MapTranslator& tx, MapData& md, TileMap& tiles);

private:
    bool m_open = false;

    // Teleport sub-panel state. The user picks a (region, floor) and
    // an (X, Y); clicking Teleport writes the corresponding mapID +
    // xpos/ypos bytes to the //e's main RAM, and the running game
    // picks them up on its next read.
    int  m_tpIdx = 0;
    int  m_tpX   = 0;
    int  m_tpY   = 0;
};

} // namespace nac
