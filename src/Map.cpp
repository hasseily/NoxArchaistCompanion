#include "Emulator/StdAfx.h"

#include "Map.h"

#include "Emulator/CPU.h"     // g_noxInCombat
#include "Emulator/Memory.h"
#include "RamSnapshot.h"

#include <imgui.h>
#include <glad/glad.h>

#include <algorithm>
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
    if (m_tex) return;
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
    m_tex = tex;
}

void TilesetTexture::DecodeTile(uint8_t id, const uint8_t* src)
{
    // 16 lines × 2 bytes × 7 pixels each (LSB = leftmost). Bit set →
    // opaque white, bit clear → transparent. The transparency lets the
    // dark fog background show through "empty" tile pixels.
    const int x0 = (id % kCols) * kTileW;
    const int y0 = (id / kCols) * kTileH;
    for (int y = 0; y < kTileH; ++y)
    {
        const uint8_t b0 = src[y * 2 + 0];
        const uint8_t b1 = src[y * 2 + 1];
        uint8_t* dst = m_pixels + ((size_t)(y0 + y) * kAtlasW + x0) * 4;
        for (int bit = 0; bit < 7; ++bit)
        {
            const bool on = (b0 & (1 << bit)) != 0;
            *dst++ = on ? 0xFF : 0; *dst++ = on ? 0xFF : 0;
            *dst++ = on ? 0xFF : 0; *dst++ = on ? 0xFF : 0;
        }
        for (int bit = 0; bit < 7; ++bit)
        {
            const bool on = (b1 & (1 << bit)) != 0;
            *dst++ = on ? 0xFF : 0; *dst++ = on ? 0xFF : 0;
            *dst++ = on ? 0xFF : 0; *dst++ = on ? 0xFF : 0;
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

void TileMap::Clear()
{
    m_floors.clear();
    m_dirty = true;
}

uint8_t TileMap::TileAt(int region_id, const std::string& floor, int x, int y) const
{
    auto it = m_floors.find({region_id, floor});
    if (it == m_floors.end()) return 0;
    const Floor& fl = it->second;
    if (x < 0 || x >= fl.width || y < 0 || y >= fl.height) return 0;
    return fl.tiles[y * fl.width + x];
}

// ---------------------------------------------------------------------------
// MapPanel
// ---------------------------------------------------------------------------

void MapPanel::Render(const MapTranslator& tx, MapData& md,
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
    // Used everywhere — Map panel display, tile observation, teleport
    // write target, party_full sextant template.
    const uint8_t mapID = Peek(0x2AF9);
    const uint8_t xpos  = Peek(0x6CEB);
    const uint8_t ypos  = Peek(0x6CEC);

    ImGui::Text("mapID $%02X  X=%u  Y=%u", mapID, xpos, ypos);

    // Teleport sub-panel — pick a (region, floor) + (X, Y) and write
    // the matching mapID, $6CEB (X) and $6CEC (Y) bytes into //e main
    // RAM. The running game picks them up on its next read.
    auto floors = md.AllFloors(tx);
    if (ImGui::CollapsingHeader("Teleport"))
    {
        if (floors.empty())
        {
            ImGui::TextDisabled("No floors loaded.");
        }
        else
        {
            std::vector<std::string> labels;
            labels.reserve(floors.size());
            for (const auto& e : floors)
                labels.push_back(
                    (e.region_name.empty() ? std::to_string(e.region_id) : e.region_name)
                    + " / " + e.floor);

            if (m_tpIdx < 0 || m_tpIdx >= (int)floors.size()) m_tpIdx = 0;
            if (ImGui::BeginCombo("Target", labels[m_tpIdx].c_str()))
            {
                for (int i = 0; i < (int)floors.size(); ++i)
                {
                    const bool sel = (i == m_tpIdx);
                    if (ImGui::Selectable(labels[i].c_str(), sel)) m_tpIdx = i;
                    if (sel) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            ImGui::InputInt("X", &m_tpX, 1, 16);
            ImGui::InputInt("Y", &m_tpY, 1, 16);
            if (ImGui::Button("Teleport"))
            {
                const auto& e = floors[m_tpIdx];
                const int newMapID = tx.FindMapID(e.region_id, e.floor);
                if (newMapID >= 0)
                {
                    if (uint8_t* p = reinterpret_cast<uint8_t*>(MemGetMainPtr(0)))
                    {
                        p[0x2AF9] = (uint8_t)newMapID;
                        p[0x6CEB] = (uint8_t)m_tpX;
                        p[0x6CEC] = (uint8_t)m_tpY;
                    }
                }
            }
        }
    }

    auto loc = tx.Resolve(mapID, xpos, ypos);
    // Pin loc.x / loc.y to the raw Nox bytes (no rule dx/dy applied)
    // — the per-floor PNG / tile data layout is 1:1 with Nox's xpos
    // / ypos, so any rule-side coordinate translation would push the
    // marker off the right tile.
    if (loc)
    {
        loc->x = xpos;
        loc->y = ypos;
    }
    if (!loc)
    {
        ImGui::TextDisabled("(no rule matched — likely on the BASIC prompt)");
        ImGui::End();
        return;
    }

    ImGui::Text("Region %d (%s) — Floor %s — Tile %d, %d",
                loc->region, loc->region_name.c_str(),
                loc->floor.c_str(), loc->x, loc->y);

    const int regionW = (std::max)(1, loc->width);
    const int regionH = (std::max)(1, loc->height);

    // Observe Nox's 17×11 visible-tile buffer ($0800) gated by the
    // matching visibility mask ($08BB — 0 = visible, 1 = hidden).
    // Only cells the player can actually see right now flow into the
    // (region, floor) TileMap; everything else is left untouched so
    // previously-discovered tiles persist.
    //
    // Skip during combat — Nox repurposes the same $0800 buffer for
    // the battle map (PC_INITIATE_COMBAT through PC_END_COMBAT). If
    // we observed those bytes they'd overwrite the overworld tiles
    // we'd previously discovered for this floor.
    const uint8_t* vis = &g_ramSnapshot.main[0x0800];
    const uint8_t* fog = &g_ramSnapshot.main[0x08BB];
    if (g_ramSnapshot.valid && !g_noxInCombat)
        tiles.Observe(loc->region, loc->floor, loc->x, loc->y,
                      regionW, regionH, vis, fog);

    // Diagnostic: live view of the two 17×11 buffers Observe() reads.
    // Watch this while you walk to verify $0800 / $08BB are the right
    // addresses and that fog == 0 / non-0 means visible / hidden.
    if (ImGui::CollapsingHeader("[diag] visible + fog buffers"))
    {
        ImGui::Text("$0800 (tiles, hex)         $08BB (fog, hex)");
        for (int row = 0; row < TileMap::kVisH; ++row)
        {
            std::string visRow, fogRow;
            visRow.reserve(TileMap::kVisW * 3);
            fogRow.reserve(TileMap::kVisW * 3);
            for (int col = 0; col < TileMap::kVisW; ++col)
            {
                char b1[4], b2[4];
                std::snprintf(b1, 4, "%02X ", vis[row * TileMap::kVisW + col]);
                std::snprintf(b2, 4, "%02X ", fog[row * TileMap::kVisW + col]);
                visRow += b1;
                fogRow += b2;
            }
            ImGui::Text("%s   %s", visRow.c_str(), fogRow.c_str());
        }
    }

    ImGui::Separator();

    static float s_zoom = 1.0f;
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputFloat("Zoom", &s_zoom, 0.05f, 0.25f, "%.2fx");
    s_zoom = (std::max)(0.05f, (std::min)(8.0f, s_zoom));
    ImGui::SameLine();
    if (ImGui::Button("Fit"))
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float zx = avail.x / (regionW * (float)TilesetTexture::kTileW);
        const float zy = (avail.y - 30) / (regionH * (float)TilesetTexture::kTileH);
        s_zoom = (std::max)(0.05f, (std::min)(zx, zy));
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear seen"))
    {
        tiles.Clear();
        tiles.Save();        // also wipe the on-disk cache immediately
    }

    // Pull fresh tile bitmaps out of aux RAM ($7000 / $8000) before
    // drawing — Nox patches the shape tables when entering a new
    // dungeon / town, so the atlas tracks whatever's currently loaded.
    tileset.Refresh();

    // Avatar-centred render. No scroll, no canvas larger than the
    // visible region — we just compute where the avatar would sit at
    // the centre of the panel and offset every tile relative to it.
    // This guarantees the avatar never drifts to a corner even at
    // map edges (where ImGui's scroll clamp would otherwise pin us).
    ImGui::BeginChild("##map_canvas", ImVec2(0, 0),
                      ImGuiChildFlags_Borders);

    constexpr float kTileW = (float)TilesetTexture::kTileW;   // 14
    constexpr float kTileH = (float)TilesetTexture::kTileH;   // 16
    constexpr float kUvW   = 1.0f / TilesetTexture::kCols;
    constexpr float kUvH   = 1.0f / TilesetTexture::kRows;

    const ImVec2 view = ImGui::GetWindowSize();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float centreX = origin.x + view.x * 0.5f;
    const float centreY = origin.y + view.y * 0.5f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 fogCol = IM_COL32(20, 20, 24, 255);

    // Solid dark fill across the visible area first; observed tiles
    // overpaint it.
    dl->AddRectFilled(origin,
                      ImVec2(origin.x + view.x, origin.y + view.y),
                      fogCol);

    auto colorForTile = [](uint8_t id) -> ImU32 {
        uint32_t h = id * 0x9E3779B1u + 0xBF58476Du;
        h ^= h >> 16;
        const uint8_t r = 60 + ((h >> 0)  & 0xFF) * 180 / 255;
        const uint8_t g = 60 + ((h >> 8)  & 0xFF) * 180 / 255;
        const uint8_t b = 60 + ((h >> 16) & 0xFF) * 180 / 255;
        return IM_COL32(r, g, b, 255);
    };

    const auto tileTexId = static_cast<ImTextureID>((uintptr_t)tileset.Tex());

    // Range of floor tiles that fit on screen, with one extra cell of
    // margin so partially-visible edge tiles aren't culled.
    const int spanX = (int)(view.x / (kTileW * s_zoom)) / 2 + 2;
    const int spanY = (int)(view.y / (kTileH * s_zoom)) / 2 + 2;
    const int x0 = (std::max)(0,           loc->x - spanX);
    const int x1 = (std::min)(regionW - 1, loc->x + spanX);
    const int y0 = (std::max)(0,           loc->y - spanY);
    const int y1 = (std::min)(regionH - 1, loc->y + spanY);

    for (int ty = y0; ty <= y1; ++ty)
    {
        for (int tx = x0; tx <= x1; ++tx)
        {
            const uint8_t id = tiles.TileAt(loc->region, loc->floor, tx, ty);
            if (id == 0) continue;
            const float px0 = centreX + (tx - loc->x - 0.5f) * kTileW * s_zoom;
            const float py0 = centreY + (ty - loc->y - 0.5f) * kTileH * s_zoom;
            const ImVec2 p0(px0, py0);
            const ImVec2 p1(px0 + kTileW * s_zoom, py0 + kTileH * s_zoom);
            if (tileset.Tex() && tileset.Has(id))
            {
                const ImVec2 uv0((id % TilesetTexture::kCols) * kUvW,
                                 (id / TilesetTexture::kCols) * kUvH);
                const ImVec2 uv1(uv0.x + kUvW, uv0.y + kUvH);
                dl->AddImage(tileTexId, p0, p1, uv0, uv1);
            }
            else
            {
                dl->AddRectFilled(p0, p1, colorForTile(id));
            }
        }
    }

    // Player marker dead-centre.
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
