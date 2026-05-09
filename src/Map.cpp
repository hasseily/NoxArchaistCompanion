#include "Emulator/StdAfx.h"

#include "Map.h"

#include "Emulator/CPU.h"     // g_noxInCombat
#include "Emulator/Memory.h"
#include "RamSnapshot.h"

#include <imgui.h>
#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

// On the nac executable target NOMINMAX isn't defined, so windows.h
// (pulled in via StdAfx.h) leaves min / max as macros. Wrapping the
// std calls in extra parens prevents the macro expansion at the call
// site without affecting the resulting code.

namespace nac
{

namespace
{

uint8_t Peek(uint16_t addr)
{
    return SnapshotPeek(addr);
}

// Returns true once Nox has a real party loaded (i.e. the user has
// actually started or restored a game). Each of the six party slots
// is 0x80 bytes starting at MEM_PARTY, with the member's name as a
// high-ASCII NUL-terminated string at offset 0x4B; an empty slot has
// 0x00 there. We gate live automap observation on this so the BASIC
// prompt / boot-screen garbage doesn't paint into the discovered map.
bool HasParty()
{
    if (!g_ramSnapshot.valid || cpuconstants.MEM_PARTY == 0) return false;
    for (uint32_t k = 0; k < 6; ++k)
    {
        const uint32_t addr = cpuconstants.MEM_PARTY + k * 0x80u + 0x4Bu;
        if (addr >= 0x20000u) continue;
        const uint8_t b = (addr < 0x10000u)
            ? g_ramSnapshot.main[addr]
            : g_ramSnapshot.aux[addr - 0x10000u];
        if (b != 0) return true;
    }
    return false;
}

bool CheckOp(int sample, int rhs, const std::string& op)
{
    if (op == "EQ"  || op.empty()) return sample == rhs;
    if (op == "GTE")               return sample >= rhs;
    if (op == "LTE")               return sample <= rhs;
    if (op == "GT")                return sample >  rhs;
    if (op == "LT")                return sample <  rhs;
    return false;
}

int SampleAtPeekOffset(int peekOffset, uint8_t mapID, uint8_t xpos, uint8_t ypos)
{
    switch (peekOffset)
    {
    case 0: return mapID;
    case 3: return xpos;
    case 4: return ypos;
    default: return -1;
    }
}

// Vision footprint. Nox draws a 17×11 viewport around the avatar when
// outdoors; we reveal the same footprint into the fog bitmap each time
// the safe-sample callback fires.
constexpr int kVisionW = 17;
constexpr int kVisionH = 11;

// Map-type byte at main $267D — Nox writes which kind of map is being
// rendered into the visible-tile buffer at $0800. $FF means "combat
// map", which we treat as "don't observe / don't paint into the
// player's discovered overworld map". The other values are useful as
// labels in the diag readout.
constexpr uint16_t kAddrMapType = 0x267D;
constexpr uint8_t  kMapTypeCombat = 0xFF;

const char* MapTypeName(uint8_t t)
{
    switch (t)
    {
    case 0x00: return "Surface";
    case 0x02: return "Undermap";
    case 0x03: return "Ruin";
    case 0x04: return "Undermap T";
    case 0x05: return "Town2";
    case 0x06: return "Town";
    case 0x07: return "Castle";
    case 0x08: return "Keep";
    case 0xFF: return "Combat";
    default:   return "?";
    }
}

} // namespace

// ---------------------------------------------------------------------------
// MapTranslator
// ---------------------------------------------------------------------------

void MapTranslator::Load(const std::filesystem::path& assetsDir)
{
    m_data = nlohmann::json{};
    const auto path = assetsDir / "maps" / "translation.json";
    if (!std::filesystem::exists(path)) return;
    try
    {
        std::ifstream f(path);
        f >> m_data;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "MapTranslator: parse failed for %s: %s\n",
                     path.string().c_str(), e.what());
        m_data = nlohmann::json{};
    }
}

std::string MapTranslator::RegionName(int region_id) const
{
    if (m_data.is_null() || !m_data.contains("regions")) return {};
    const std::string key = std::to_string(region_id);
    if (!m_data["regions"].contains(key)) return {};
    return m_data["regions"][key].value("name", std::string{});
}

int MapTranslator::FindMapID(int region_id, const std::string& floor) const
{
    if (m_data.is_null() || !m_data.contains("rules")) return -1;
    for (const auto& rule : m_data["rules"])
    {
        if (rule.value("region", -1) != region_id) continue;
        if (rule.value("floor",  std::string{}) != floor) continue;
        if (!rule.contains("checks")) continue;
        for (const auto& c : rule["checks"])
        {
            if (c.value("offset", -1) == 0)
                return c.value("value", -1);
        }
    }
    return -1;
}

void MapTranslator::RegionDims(int region_id, int& width, int& height) const
{
    width = height = 0;
    if (m_data.is_null() || !m_data.contains("regions")) return;
    const std::string key = std::to_string(region_id);
    if (!m_data["regions"].contains(key)) return;
    const auto& r = m_data["regions"][key];
    width  = r.value("width",  0);
    height = r.value("height", 0);
}

std::optional<MapLocation> MapTranslator::Resolve(uint8_t mapID,
                                                  uint8_t xpos,
                                                  uint8_t ypos) const
{
    if (m_data.is_null() || !m_data.contains("rules") || !m_data["rules"].is_array())
        return std::nullopt;

    for (const auto& rule : m_data["rules"])
    {
        const int xmin = rule.value("xmin", 0);
        const int xmax = rule.value("xmax", 0xFF);
        const int ymin = rule.value("ymin", 0);
        const int ymax = rule.value("ymax", 0xFF);
        if (xpos < xmin || xpos > xmax) continue;
        if (ypos < ymin || ypos > ymax) continue;

        bool ok = true;
        if (rule.contains("checks") && rule["checks"].is_array())
        {
            for (const auto& c : rule["checks"])
            {
                const int sample = SampleAtPeekOffset(
                    c.value("offset", -1), mapID, xpos, ypos);
                const int value  = c.value("value", 0);
                const std::string op = c.value("op", std::string("EQ"));
                if (!CheckOp(sample, value, op)) { ok = false; break; }
            }
        }
        if (!ok) continue;

        MapLocation loc;
        loc.region = rule.value("region", 0);
        loc.floor  = rule.value("floor",  std::string{});
        loc.x      = xpos + rule.value("dx", 0);
        loc.y      = ypos + rule.value("dy", 0);

        if (m_data.contains("regions"))
        {
            const std::string key = std::to_string(loc.region);
            if (m_data["regions"].contains(key))
            {
                const auto& r = m_data["regions"][key];
                loc.region_name = r.value("name", std::string{});
                loc.width  = r.value("width",  0);
                loc.height = r.value("height", 0);
            }
        }
        return loc;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// MapData
// ---------------------------------------------------------------------------

void MapData::Load(const std::filesystem::path& assetsDir)
{
    m_blob.clear();
    m_index.clear();

    const auto path = assetsDir / "maps" / "maps.bin";
    if (!std::filesystem::exists(path))
    {
        std::fprintf(stderr, "MapData: %s not found\n", path.string().c_str());
        return;
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return;
    const auto size = f.tellg();
    f.seekg(0);
    m_blob.resize((size_t)size);
    f.read(reinterpret_cast<char*>(m_blob.data()), size);

    if (m_blob.size() < 12 || std::memcmp(m_blob.data(), "NMAP", 4) != 0)
    {
        std::fprintf(stderr, "MapData: bad magic / too short\n");
        m_blob.clear();
        return;
    }

    const uint32_t version = *reinterpret_cast<const uint32_t*>(m_blob.data() + 4);
    const uint32_t count   = *reinterpret_cast<const uint32_t*>(m_blob.data() + 8);
    if (version != 1)
    {
        std::fprintf(stderr, "MapData: unsupported version %u\n", version);
        m_blob.clear();
        return;
    }

    constexpr size_t kHeaderSize = 12;
    constexpr size_t kRecordSize = 20;
    if (m_blob.size() < kHeaderSize + (size_t)count * kRecordSize)
    {
        std::fprintf(stderr, "MapData: truncated record table\n");
        m_blob.clear();
        return;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t* p = m_blob.data() + kHeaderSize + i * kRecordSize;
        FloorRecord r;
        r.region_id = *reinterpret_cast<const uint16_t*>(p + 0);
        char fbuf[5] = {};
        std::memcpy(fbuf, p + 2, 4);
        r.floor    = fbuf;
        r.width    = *reinterpret_cast<const uint16_t*>(p + 6);
        r.height   = *reinterpret_cast<const uint16_t*>(p + 8);
        r.origin_x = *reinterpret_cast<const int16_t* >(p + 10);
        r.origin_y = *reinterpret_cast<const int16_t* >(p + 12);
        const uint32_t doff = *reinterpret_cast<const uint32_t*>(p + 14);
        const size_t need = (size_t)r.width * (size_t)r.height;
        if (doff + need > m_blob.size())
        {
            std::fprintf(stderr, "MapData: record %u tile data out of bounds\n", i);
            continue;
        }
        r.tiles = m_blob.data() + doff;
        m_index[{r.region_id, r.floor}] = r;
    }

}

const FloorRecord* MapData::Find(int region_id, const std::string& floor) const
{
    auto it = m_index.find({region_id, floor});
    return it == m_index.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// TilesetTexture
// ---------------------------------------------------------------------------

void TilesetTexture::EnsureTexture()
{
    if (m_tex && m_texColor) return;
    auto makeTex = [&]() {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kAtlasW, kAtlasH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    };
    if (!m_tex)      m_tex      = makeTex();
    if (!m_texColor) m_texColor = makeTex();
}

void TilesetTexture::DecodeTile(uint8_t id, const uint8_t* src)
{
    // 16 lines × 2 bytes × 7 pixels each (LSB = leftmost). Two atlases
    // are filled in lockstep: a monochrome one (white-on-transparent,
    // tinted at draw-time for the white/green/amber CRT modes) and an
    // Apple ][ HGR-coloured one for the "Color" mode.
    //
    // HGR colour rule (the simple version — no NTSC fringe / sub-pixel
    // shift): a lit pixel whose immediate left or right neighbour is
    // also lit renders as WHITE; otherwise its colour is picked from
    // its in-tile column parity plus the high bit of the byte it lives
    // in:
    //   high bit 0 → even col VIOLET, odd col GREEN
    //   high bit 1 → even col BLUE,   odd col ORANGE
    // Bits 6 and 8 (last pixel of byte 0, first pixel of byte 1) are
    // screen-adjacent so the white test crosses the byte boundary.
    constexpr uint8_t kBlack [4] = {   0,   0,   0,   0 };
    constexpr uint8_t kViolet[4] = { 255,  68, 253, 255 };
    constexpr uint8_t kGreen [4] = {  20, 245,  60, 255 };
    constexpr uint8_t kBlue  [4] = {  20, 207, 253, 255 };
    constexpr uint8_t kOrange[4] = { 255, 106,  60, 255 };
    constexpr uint8_t kWhiteM[4] = { 255, 255, 255, 255 };

    const int x0 = (id % kCols) * kTileW;
    const int y0 = (id / kCols) * kTileH;
    for (int y = 0; y < kTileH; ++y)
    {
        const uint8_t bytes[2] = { src[y * 2 + 0], src[y * 2 + 1] };
        bool    on[kTileW];
        bool    hi[kTileW];
        for (int col = 0; col < kTileW; ++col)
        {
            const int     bi  = col / 7;
            const int     bit = col % 7;
            const uint8_t b   = bytes[bi];
            on[col] = (b & (1 << bit)) != 0;
            hi[col] = (b & 0x80) != 0;
        }
        uint8_t* dstM = m_pixels      + ((size_t)(y0 + y) * kAtlasW + x0) * 4;
        uint8_t* dstC = m_pixelsColor + ((size_t)(y0 + y) * kAtlasW + x0) * 4;
        for (int col = 0; col < kTileW; ++col)
        {
            const uint8_t* mono = on[col] ? kWhiteM : kBlack;

            const uint8_t* color;
            if (!on[col])
                color = kBlack;
            else
            {
                const bool leftOn  = (col > 0)             && on[col - 1];
                const bool rightOn = (col < kTileW - 1)    && on[col + 1];
                if (leftOn || rightOn)
                    color = kWhiteM;
                else
                {
                    const bool even = ((col & 1) == 0);
                    color = hi[col] ? (even ? kBlue   : kOrange)
                                    : (even ? kViolet : kGreen);
                }
            }

            std::memcpy(dstM, mono,  4); dstM += 4;
            std::memcpy(dstC, color, 4); dstC += 4;
        }
    }
    m_has[id] = true;
}

void TilesetTexture::Refresh()
{
    if (!g_ramSnapshot.valid) return;
    EnsureTexture();

    // Static tiles: $7000 + N * $20 for N in 0..$7F.
    for (int id = 0; id <= 0x7F; ++id)
        DecodeTile((uint8_t)id, &g_ramSnapshot.aux[0x7000 + id * 0x20]);

    // Animated tiles: $8000 + (N - $80) * $80 for N in $80..$FF, each
    // 128 bytes laid out as 4 × 32-byte frames. Decode frame 0 only —
    // animation cycling is a TODO once we know how Nox tracks the
    // current frame.
    for (int id = 0x80; id <= 0xFF; ++id)
        DecodeTile((uint8_t)id, &g_ramSnapshot.aux[0x8000 + (id - 0x80) * 0x80]);

    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAtlasW, kAtlasH,
                    GL_RGBA, GL_UNSIGNED_BYTE, m_pixels);
    glBindTexture(GL_TEXTURE_2D, m_texColor);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAtlasW, kAtlasH,
                    GL_RGBA, GL_UNSIGNED_BYTE, m_pixelsColor);
    glBindTexture(GL_TEXTURE_2D, 0);
}

std::vector<MapData::FloorListEntry>
MapData::AllFloors(const MapTranslator& tx) const
{
    std::vector<FloorListEntry> out;
    out.reserve(m_index.size());
    for (const auto& [k, fr] : m_index)
    {
        FloorListEntry e;
        e.region_id   = k.first;
        e.floor       = k.second;
        e.region_name = tx.RegionName(k.first);
        out.push_back(std::move(e));
    }
    return out;
}

// ---------------------------------------------------------------------------
// TileMap
// ---------------------------------------------------------------------------

namespace
{

// Tiny inline base64 — same scheme the old fogofwar.json used so the
// switch to seen_tiles.json doesn't need a real codec dependency.
const std::string kB64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string EncodeB64(const std::vector<uint8_t>& bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    int val = 0, valb = -6;
    for (uint8_t c : bytes)
    {
        val = (val << 8) | c;
        valb += 8;
        while (valb >= 0)
        {
            out.push_back(kB64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(kB64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::vector<uint8_t> DecodeB64(const std::string& b64)
{
    std::vector<int> tab(256, -1);
    for (int i = 0; i < 64; ++i) tab[(uint8_t)kB64[i]] = i;
    std::vector<uint8_t> out;
    out.reserve(b64.size() * 3 / 4);
    int val = 0, valb = -8;
    for (char c : b64)
    {
        if (c == '=') break;
        if (tab[(uint8_t)c] < 0) continue;
        val = (val << 6) | tab[(uint8_t)c];
        valb += 6;
        if (valb >= 0) { out.push_back((uint8_t)((val >> valb) & 0xFF)); valb -= 8; }
    }
    return out;
}

} // namespace

void TileMap::Load(const std::filesystem::path& prefDir)
{
    m_path = prefDir / "seen_tiles.json";
    m_floors.clear();
    m_dirty = false;
    if (!std::filesystem::exists(m_path)) return;
    try
    {
        std::ifstream f(m_path);
        nlohmann::json j; f >> j;
        if (!j.is_array()) return;
        for (const auto& fjson : j)
        {
            const int region = fjson.value("region", 0);
            const std::string floor = fjson.value("floor", std::string{});
            const int w = fjson.value("w", 0);
            const int h = fjson.value("h", 0);
            const std::string b64 = fjson.value("tiles_b64", std::string{});
            if (region == 0 || floor.empty() || w <= 0 || h <= 0) continue;
            Floor& fl = EnsureFloor(region, floor, w, h);
            const auto bytes = DecodeB64(b64);
            const size_t n = (std::min)(bytes.size(), (size_t)w * (size_t)h);
            std::memcpy(fl.tiles.data(), bytes.data(), n);
        }
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "TileMap: parse failed: %s\n", e.what());
    }
}

void TileMap::Save() const
{
    if (!m_dirty || m_path.empty()) return;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [k, fl] : m_floors)
    {
        arr.push_back({
            {"region",    k.first},
            {"floor",     k.second},
            {"w",         fl.width},
            {"h",         fl.height},
            {"tiles_b64", EncodeB64(fl.tiles)},
        });
    }
    try
    {
        std::ofstream f(m_path);
        f << arr.dump(2);
        m_dirty = false;
    }
    catch (...) {}
}

TileMap::Floor& TileMap::EnsureFloor(int region_id, const std::string& floor,
                                     int floorWidth, int floorHeight)
{
    auto& fl = m_floors[{region_id, floor}];
    if ((int)fl.tiles.size() != floorWidth * floorHeight)
    {
        fl.width  = floorWidth;
        fl.height = floorHeight;
        fl.tiles.assign((size_t)floorWidth * (size_t)floorHeight, 0);
    }
    return fl;
}

void TileMap::Observe(int region_id, const std::string& floor,
                      int playerX, int playerY,
                      int floorWidth, int floorHeight,
                      const uint8_t* vis, const uint8_t* fog)
{
    if (!vis || !fog || floorWidth <= 0 || floorHeight <= 0) return;
    Floor& fl = EnsureFloor(region_id, floor, floorWidth, floorHeight);

    // Nox's visible window stops scrolling when the avatar approaches
    // a map edge — the camera clamps to (0..floor_w-kVisW), and the
    // avatar shifts within the otherwise-stationary viewport. Use the
    // same clamp so $0800 cells map to the right floor tiles. Without
    // it, walking near the west edge plots the new content kVisW/2 = 8
    // tiles further west than where it actually belongs (the symptom
    // you saw on Vacous re-entry).
    int x0 = playerX - kVisW / 2;
    int y0 = playerY - kVisH / 2;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x0 + kVisW > floorWidth)  x0 = floorWidth  - kVisW;
    if (y0 + kVisH > floorHeight) y0 = floorHeight - kVisH;
    if (x0 < 0) x0 = 0;     // floors smaller than the viewport
    if (y0 < 0) y0 = 0;

    for (int row = 0; row < kVisH; ++row)
    {
        const int y = y0 + row;
        if (y < 0 || y >= floorHeight) continue;
        for (int col = 0; col < kVisW; ++col)
        {
            const int x = x0 + col;
            if (x < 0 || x >= floorWidth) continue;
            const int idx = row * kVisW + col;
            if (fog[idx] != 0) continue;       // hidden — skip
            const uint8_t id = vis[idx];
            if (id == 0) continue;             // visible but empty buffer slot
            uint8_t& cell = fl.tiles[y * floorWidth + x];
            if (cell != id) { cell = id; m_dirty = true; }
        }
    }
}

uint8_t TileMap::TileAt(int region_id, const std::string& floor, int x, int y) const
{
    auto it = m_floors.find({region_id, floor});
    if (it == m_floors.end()) return 0;
    const Floor& fl = it->second;
    if (x < 0 || x >= fl.width || y < 0 || y >= fl.height) return 0;
    return fl.tiles[y * fl.width + x];
}

std::vector<std::pair<int, std::string>> TileMap::ObservedFloors() const
{
    std::vector<std::pair<int, std::string>> out;
    out.reserve(m_floors.size());
    for (const auto& [k, fl] : m_floors)
    {
        for (uint8_t b : fl.tiles)
            if (b != 0) { out.push_back(k); break; }
    }
    return out;
}

// ---------------------------------------------------------------------------
// MapPanel
// ---------------------------------------------------------------------------

void MapPanel::Render(const MapTranslator& tx, MapData& /*md*/,
                      TilesetTexture& tileset, TileMap& tiles)
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(720, 540), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Map", &m_open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    // Canonical Nox coordinates: X at $6CEB, Y at $6CEC, both in main.
    const uint8_t mapID   = Peek(0x2AF9);
    const uint8_t xpos    = Peek(0x6CEB);
    const uint8_t ypos    = Peek(0x6CEC);
    const uint8_t mapType = Peek(kAddrMapType);

    ImGui::Text("mapID $%02X  X=%u  Y=%u  type $%02X (%s)",
                mapID, xpos, ypos, mapType, MapTypeName(mapType));

    // Live observation always runs, regardless of which floor the
    // panel happens to be rendering — switching the view pulldown to
    // a stored map shouldn't pause discovery on the live one. Skipped
    // on combat maps so battle tiles ($0800 repurposed) don't pollute
    // the overworld, and skipped before the player has a party so
    // boot / BASIC-prompt junk in $0800 doesn't paint into the map.
    auto liveLoc = tx.Resolve(mapID, xpos, ypos);
    if (liveLoc) { liveLoc->x = xpos; liveLoc->y = ypos; }
    if (liveLoc && g_ramSnapshot.valid && mapType != kMapTypeCombat && HasParty())
    {
        const int liveW = (std::max)(1, liveLoc->width);
        const int liveH = (std::max)(1, liveLoc->height);
        const uint8_t* vis = &g_ramSnapshot.main[0x0800];
        const uint8_t* fog = &g_ramSnapshot.main[0x08BB];
        tiles.Observe(liveLoc->region, liveLoc->floor,
                      liveLoc->x, liveLoc->y, liveW, liveH, vis, fog);
    }

    // Map pulldown: "Live (auto)" plus every (region, floor) the
    // player has ever observed.
    auto observed = tiles.ObservedFloors();
    {
        std::vector<std::string> labels;
        labels.reserve(observed.size() + 1);
        labels.emplace_back("Live (auto)");
        for (const auto& [r, f] : observed)
        {
            std::string name = tx.RegionName(r);
            if (name.empty()) name = std::to_string(r);
            labels.push_back(name + " / " + f);
        }
        int sel = 0;
        if (m_viewRegion >= 0)
        {
            for (size_t i = 0; i < observed.size(); ++i)
                if (observed[i].first == m_viewRegion &&
                    observed[i].second == m_viewFloor)
                { sel = (int)(i + 1); break; }
        }
        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::BeginCombo("Map", labels[sel].c_str()))
        {
            for (int i = 0; i < (int)labels.size(); ++i)
            {
                const bool s = (i == sel);
                if (ImGui::Selectable(labels[i].c_str(), s) && i != sel)
                {
                    if (i == 0) { m_viewRegion = -1; m_viewFloor.clear(); }
                    else
                    {
                        m_viewRegion = observed[i - 1].first;
                        m_viewFloor  = observed[i - 1].second;
                        // Fresh stored-map view → frame the whole floor
                        // and reset any previous pan from another map.
                        m_needFit = true;
                    }
                    m_panX = m_panY = 0.0f;
                }
                if (s) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    // CRT-style monochrome tints + a "Color" mode using the baked
    // Apple ][ HGR-coloured atlas.
    {
        const char* csNames[] = { "White", "Green", "Amber", "Color" };
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo("##cs", &m_colorScheme_int, csNames, IM_ARRAYSIZE(csNames));
        if (m_colorScheme_int < 0 || m_colorScheme_int > CS_Color)
            m_colorScheme_int = (int)CS_Green;
    }
    const ColorScheme colorScheme = (ColorScheme)m_colorScheme_int;

    // Resolve the floor we're actually rendering — live or stored.
    int         renderRegion = -1;
    std::string renderFloor;
    int         renderW = 0, renderH = 0;
    float       centreTileX = 0.0f, centreTileY = 0.0f;
    bool        showAvatar = false;
    const bool  liveView = (m_viewRegion < 0);

    if (liveView)
    {
        if (!liveLoc)
        {
            ImGui::TextDisabled("(no rule matched - likely on the BASIC prompt)");
            ImGui::End();
            return;
        }
        renderRegion = liveLoc->region;
        renderFloor  = liveLoc->floor;
        renderW      = (std::max)(1, liveLoc->width);
        renderH      = (std::max)(1, liveLoc->height);
        centreTileX  = (float)liveLoc->x;
        centreTileY  = (float)liveLoc->y;
        showAvatar   = true;

        ImGui::Text("Region %d (%s) - Floor %s - Tile %d, %d",
                    liveLoc->region, liveLoc->region_name.c_str(),
                    liveLoc->floor.c_str(), liveLoc->x, liveLoc->y);
    }
    else
    {
        renderRegion = m_viewRegion;
        renderFloor  = m_viewFloor;
        tx.RegionDims(renderRegion, renderW, renderH);
        if (renderW <= 0) renderW = 1;
        if (renderH <= 0) renderH = 1;
        // Stored view starts framed on the floor's centre; m_panX/Y
        // shifts that as the user drags inside the canvas.
        centreTileX  = renderW * 0.5f + m_panX;
        centreTileY  = renderH * 0.5f + m_panY;

        ImGui::Text("Region %d (%s) - Floor %s - viewing stored map",
                    renderRegion, tx.RegionName(renderRegion).c_str(),
                    renderFloor.c_str());
    }

    ImGui::Separator();

    static float s_zoom = 1.0f;
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputFloat("Zoom", &s_zoom, 0.05f, 0.25f, "%.2fx");
    s_zoom = (std::max)(0.05f, (std::min)(8.0f, s_zoom));
    ImGui::SameLine();
    const bool fitClicked = ImGui::Button("Fit");

    // Pull fresh tile bitmaps out of aux RAM ($7000 / $8000) before
    // drawing — Nox patches the shape tables when entering a new
    // dungeon / town, so the atlas tracks whatever's currently loaded.
    tileset.Refresh();

    // Avatar-centred render. No scroll, no canvas larger than the
    // visible region — we just compute where the avatar would sit at
    // the centre of the panel and offset every tile relative to it.
    // For a stored-map view we centre on the floor's middle plus pan,
    // and let the user drag inside the canvas to move that pan.
    ImGui::BeginChild("##map_canvas", ImVec2(0, 0),
                      ImGuiChildFlags_Borders);

    constexpr float kTileW = (float)TilesetTexture::kTileW;   // 14
    constexpr float kTileH = (float)TilesetTexture::kTileH;   // 16
    constexpr float kUvW   = 1.0f / TilesetTexture::kCols;
    constexpr float kUvH   = 1.0f / TilesetTexture::kRows;

    // GetContentRegionAvail rather than GetWindowSize: the latter
    // includes the child's borders / padding, so sizing the
    // InvisibleButton from it overshoots the usable area by a few
    // pixels and the child grows a vertical scrollbar.
    const ImVec2 view = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // Cover the canvas with an InvisibleButton so left-drag inside it
    // is consumed as an item interaction and never bubbles up to the
    // ImGui window-move handler — without this, dragging on the map
    // also drags the whole "Map" window around. Tile drawing below
    // uses absolute screen coords from `origin` so the button advancing
    // the cursor doesn't matter.
    ImGui::InvisibleButton("##map_canvas_hit", view);
    const bool canvasActive = ImGui::IsItemActive();

    // One-shot zoom-to-fit. Auto-fires the first frame after the user
    // picks a stored map (m_needFit was set in the combo handler) and
    // also when they explicitly click Fit. Live view is never auto-fit
    // — it would yank the user's chosen zoom out from under them.
    if (fitClicked || (m_needFit && !liveView))
    {
        const float zx = view.x / (renderW * kTileW);
        const float zy = view.y / (renderH * kTileH);
        s_zoom = (std::max)(0.05f, (std::min)(zx, zy));
        m_needFit = false;
    }

    // Pan: drag inside the canvas (stored-map view only — live view
    // stays locked to the avatar).
    if (!liveView && canvasActive &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        m_panX -= d.x / (kTileW * s_zoom);
        m_panY -= d.y / (kTileH * s_zoom);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        centreTileX = renderW * 0.5f + m_panX;
        centreTileY = renderH * 0.5f + m_panY;
    }

    const float centreX = origin.x + view.x * 0.5f;
    const float centreY = origin.y + view.y * 0.5f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 fogCol = IM_COL32(20, 20, 24, 255);

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + view.x, origin.y + view.y),
                      fogCol);

    // CS_Color samples the Apple ][ HGR-coloured atlas with a white
    // tint; the monochrome modes draw the mono atlas with a flat
    // phosphor-style tint.
    ImU32 tint = IM_COL32_WHITE;
    switch (colorScheme)
    {
    case CS_White: tint = IM_COL32(235, 235, 235, 255); break;
    case CS_Green: tint = IM_COL32( 80, 255, 120, 255); break;
    case CS_Amber: tint = IM_COL32(255, 180,  50, 255); break;
    case CS_Color: tint = IM_COL32_WHITE;               break;
    }
    const unsigned glTex = (colorScheme == CS_Color)
                         ? tileset.ColorTex()
                         : tileset.Tex();
    const auto tileTexId = static_cast<ImTextureID>((uintptr_t)glTex);

    const int spanX = (int)(view.x / (kTileW * s_zoom)) / 2 + 2;
    const int spanY = (int)(view.y / (kTileH * s_zoom)) / 2 + 2;
    const int x0 = (std::max)(0,           (int)std::floor(centreTileX) - spanX);
    const int x1 = (std::min)(renderW - 1, (int)std::ceil (centreTileX) + spanX);
    const int y0 = (std::max)(0,           (int)std::floor(centreTileY) - spanY);
    const int y1 = (std::min)(renderH - 1, (int)std::ceil (centreTileY) + spanY);

    for (int ty = y0; ty <= y1; ++ty)
    {
        for (int tx_ = x0; tx_ <= x1; ++tx_)
        {
            const uint8_t id = tiles.TileAt(renderRegion, renderFloor, tx_, ty);
            if (id == 0) continue;
            const float px0 = centreX + (tx_ - centreTileX - 0.5f) * kTileW * s_zoom;
            const float py0 = centreY + (ty  - centreTileY - 0.5f) * kTileH * s_zoom;
            const ImVec2 p0(px0, py0);
            const ImVec2 p1(px0 + kTileW * s_zoom, py0 + kTileH * s_zoom);
            if (glTex && tileset.Has(id))
            {
                const ImVec2 uv0((id % TilesetTexture::kCols) * kUvW,
                                 (id / TilesetTexture::kCols) * kUvH);
                const ImVec2 uv1(uv0.x + kUvW, uv0.y + kUvH);
                dl->AddImage(tileTexId, p0, p1, uv0, uv1, tint);
            }
            else
            {
                dl->AddRectFilled(p0, p1, tint);
            }
        }
    }

    if (showAvatar)
    {
        const ImVec2 c(centreX, centreY);
        const float r = (std::max)(3.0f, kTileW * s_zoom * 0.45f);
        dl->AddCircleFilled(c, r,        IM_COL32(255, 60, 60, 255));
        dl->AddCircle      (c, r + 1.5f, IM_COL32(0, 0, 0, 255), 0, 1.5f);
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace nac
