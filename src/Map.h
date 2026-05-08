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

// Loads Assets/maps/maps.bin (produced by tools/pack_maps.py from
// EXPORTMAP.NUT's output) and indexes it by (region_id, floor) for
// cheap lookup each frame.
class MapData
{
public:
    void Load(const std::filesystem::path& assetsDir);
    bool Loaded() const { return !m_index.empty(); }

    const FloorRecord* Find(int region_id, const std::string& floor) const;

private:
    std::vector<uint8_t>                            m_blob;     // whole file
    std::map<std::pair<int, std::string>, FloorRecord> m_index;
};

// Lazy GL-texture cache for the per-floor PNGs exported via
// tools/EXPORTPNGS.NUT. PNGs live under Assets/maps/floors/ and are
// loaded on first request via stb_image. Each entry holds the GL
// texture id + native pixel dimensions.
class FloorImageCache
{
public:
    void SetRoot(const std::filesystem::path& assetsDir);

    struct Entry
    {
        unsigned tex = 0;        // GL texture id (0 if not loaded / missing)
        int      width  = 0;     // native pixel size
        int      height = 0;
    };

    // Returns the entry for (region_name, floor). Loads on first call;
    // subsequent calls hit the cache. region_name and floor must match
    // the EXPORTPNGS.NUT filename convention (region from translation
    // .json's regions table; floor in friendly form, see Lookup notes).
    const Entry& Lookup(const std::string& region_name,
                        const std::string& floor_short);

private:
    std::filesystem::path                 m_root;
    std::map<std::string, Entry>          m_cache;     // key = "<region>/<floor_short>"
};

// Per-(region, floor) "have I seen this tile" bitmap, persisted to
// pref_dir/fogofwar.json. Reveals a 17×11 box around the player each
// time it's poked. Phase F.
class FogOfWar
{
public:
    void Load(const std::filesystem::path& prefDir);
    void Save() const;

    // Mark the visible 17×11 footprint (centred on player) as seen.
    void Reveal(int region_id, const std::string& floor,
                int playerX, int playerY,
                int floorWidth, int floorHeight);

    // True if the tile at (x, y) on (region, floor) has been seen.
    bool IsRevealed(int region_id, const std::string& floor, int x, int y) const;

    // Raw access to a floor's bitmap (row-major, w*h bytes; 1 = seen).
    const std::vector<uint8_t>* Bitmap(int region_id, const std::string& floor) const;

private:
    using Key = std::pair<int, std::string>;
    struct Floor
    {
        int width = 0, height = 0;
        std::vector<uint8_t> seen;     // size = width*height
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
    void  Render(const MapTranslator& tx, const MapData& md,
                 FloorImageCache& images, FogOfWar& fog);

private:
    bool m_open = false;
};

} // namespace nac
