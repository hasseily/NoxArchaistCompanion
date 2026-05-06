#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace nac
{

// Resolved location: which logical region/floor the player is on, and
// the (x, y) within that floor's grid (after any per-rule dx/dy shift).
struct MapLocation
{
    int         region = 0;        // region id from translation.json
    std::string region_name;       // human-readable
    std::string floor;             // "G", "F1", "F2", "F3", "B1", ...
    int         x = 0;
    int         y = 0;
    int         width  = 0;        // floor grid dims (from regions table)
    int         height = 0;
};

// Loads Assets/maps/translation.json (the C++-side derivation of the
// Grid Cartographer profile XML) once at startup, then walks its rule
// list each frame to translate (mapID, xpos, ypos) into a MapLocation.
class MapTranslator
{
public:
    void Load(const std::filesystem::path& assetsDir);
    bool Loaded() const { return !m_data.is_null(); }

    // Returns nullopt if no rule matches (e.g. the player is in a map
    // the profile didn't cover — happens on the BASIC prompt before a
    // game is loaded, or in unmapped scratch areas).
    std::optional<MapLocation> Resolve(uint8_t mapID,
                                       uint8_t xpos,
                                       uint8_t ypos) const;

private:
    nlohmann::json m_data;
};

// Internal automap. Phase A+B: a debug window that reads the five RAM
// bytes and (if loaded) shows the resolved (region, floor, x, y).
//
// Raw bytes (1 each, all CPU-visible — read through memshadow so soft
// switches are honoured):
//   $2AF9  mapID
//   $0000  region   (peeked but not used by any translation rule)
//   $267D  maptype  (peeked but not used by any translation rule)
//   $6CEC  xpos
//   $6CED  ypos
class MapPanel
{
public:
    bool* OpenFlag()    { return &m_open; }
    bool& OpenRef()     { return m_open; }
    void  Render(const MapTranslator& tx);

private:
    bool m_open = false;
};

} // namespace nac
