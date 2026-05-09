#include "Emulator/StdAfx.h"

#include "Map.h"

#include "Emulator/Memory.h"
#include "RamSnapshot.h"

#include <imgui.h>
#include <stb_image.h>
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
        // The GC profile XML labels byte $6CEC "xpos" and byte $6CED
        // "ypos", but Nox actually stores them the other way: $6CEC is
        // Y (row, increases going south), $6CED is X (col, increases
        // going east). The rules' xmin/xmax/ymin/ymax + dx/dy were
        // authored against the XML's labels, so they still gate on the
        // correct byte (xpos here = byte 3 = $6CEC); we only need to
        // swap the *meaning* of the rule's adjustment when emitting
        // loc.x / loc.y. Result: loc.x = east-west, loc.y = north-south.
        loc.x      = ypos + rule.value("dy", 0);
        loc.y      = xpos + rule.value("dx", 0);

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

namespace
{

std::string FloorFriendly(const std::string& s)
{
    if (s == "G")              return "Ground Floor";
    if (!s.empty() && s[0] == 'F') return "Floor "    + s.substr(1);
    if (!s.empty() && s[0] == 'B') return "Basement " + s.substr(1);
    return s;
}

} // namespace

void TilesetTexture::Build(const std::filesystem::path& assetsDir,
                           const MapData& maps,
                           const MapTranslator& tx)
{
    // 512×512 RGBA, stride 2048 bytes. Filled with transparent black,
    // overwritten as we discover each tile id's bitmap. Tiles we never
    // see remain transparent — the renderer falls back to a colour
    // for those via TilesetTexture::Has().
    std::vector<uint8_t> atlas((size_t)kTexPx * kTexPx * 4, 0);

    const auto floorsDir = assetsDir / "maps" / "floors";

    for (const auto& fl : maps.AllFloors(tx))
    {
        const FloorRecord* fr = maps.Find(fl.region_id, fl.floor);
        if (!fr) continue;

        const std::string fname = fl.region_name + "_" + FloorFriendly(fl.floor) + ".png";
        const auto path = floorsDir / fname;
        if (!std::filesystem::exists(path)) continue;

        int w = 0, h = 0, ch = 0;
        unsigned char* png = stbi_load(path.string().c_str(), &w, &h, &ch, 4);
        if (!png) continue;

        // FloorRecord is (origin_x, origin_y) + (width, height) tiles.
        // The PNG covers exactly (width × kTilePx) by (height × kTilePx)
        // px (after the 32-px chrome crop we did earlier). Loop the
        // tile grid; for any tile id we haven't yet seen, copy its
        // 32×32 region out of the PNG into the atlas.
        for (int ty = 0; ty < fr->height; ++ty)
        {
            for (int tx = 0; tx < fr->width; ++tx)
            {
                const uint8_t id = fr->tiles[ty * fr->width + tx];
                if (id == 0 || m_has[id]) continue;
                const int srcX = tx * kTilePx;
                const int srcY = ty * kTilePx;
                if (srcX + kTilePx > w || srcY + kTilePx > h) continue;
                const int dstX = (id % kCols) * kTilePx;
                const int dstY = (id / kCols) * kTilePx;
                for (int row = 0; row < kTilePx; ++row)
                {
                    const uint8_t* src = png + ((size_t)(srcY + row) * w + srcX) * 4;
                    uint8_t*       dst = atlas.data() + ((size_t)(dstY + row) * kTexPx + dstX) * 4;
                    std::memcpy(dst, src, (size_t)kTilePx * 4);
                }
                m_has[id] = true;
            }
        }
        stbi_image_free(png);
    }

    // Upload as one GL texture.
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kTexPx, kTexPx, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, atlas.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    m_tex = tex;
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
    const int x0 = playerX - kVisW / 2;
    const int y0 = playerY - kVisH / 2;
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
                      const TilesetTexture& tileset, TileMap& tiles)
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(720, 540), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Map", &m_open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    const uint8_t mapID = Peek(0x2AF9);
    const uint8_t xpos  = Peek(0x6CEC);
    const uint8_t ypos  = Peek(0x6CEF);

    ImGui::Text("mapID $%02X  X=%u  Y=%u", mapID, ypos, xpos);

    // Teleport sub-panel — pick a (region, floor) + (X, Y) and write
    // the matching mapID, $6CEF (X) and $6CEC (Y) bytes into //e main
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
                        p[0x6CEF] = (uint8_t)m_tpX;
                        p[0x6CEC] = (uint8_t)m_tpY;
                    }
                }
            }
        }
    }

    auto loc = tx.Resolve(mapID, xpos, ypos);
    // Use the raw RAM bytes for the player tile, not the resolver's
    // dx/dy-adjusted output. The packetview <move> offsets in GC's
    // profile are used for GC's combined-floor display, but each
    // per-floor PNG we export already lays out its own coordinate
    // system 1:1 with Nox's raw xpos/ypos.
    if (loc)
    {
        loc->x = ypos;
        loc->y = xpos;
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

    ImGui::Text("Region %d (%s) — Floor %s",
                loc->region, loc->region_name.c_str(), loc->floor.c_str());

    const int regionW = (std::max)(1, loc->width);
    const int regionH = (std::max)(1, loc->height);

    // Observe Nox's 17×11 visible-tile buffer ($0800) gated by the
    // matching visibility mask ($08BB — 0 = visible, 1 = hidden).
    // Only cells the player can actually see right now flow into the
    // (region, floor) TileMap; everything else is left untouched so
    // previously-discovered tiles persist.
    const uint8_t* vis = &g_ramSnapshot.main[0x0800];
    const uint8_t* fog = &g_ramSnapshot.main[0x08BB];
    if (g_ramSnapshot.valid)
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
        const float zx = avail.x / (regionW * 32.0f);
        const float zy = (avail.y - 30) / (regionH * 32.0f);
        s_zoom = (std::max)(0.05f, (std::min)(zx, zy));
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear seen"))
    {
        tiles.Clear();
        tiles.Save();        // also wipe the on-disk cache immediately
    }

    ImGui::BeginChild("##map_canvas", ImVec2(0, 0),
                      ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

    constexpr float kTilePx = 32.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 fogCol = IM_COL32(20, 20, 24, 255);

    // Background — solid dark across the whole region. Drawn first so
    // unseen cells stay dark; observed tiles paint over it.
    const ImVec2 bg0(origin.x, origin.y);
    const ImVec2 bg1(origin.x + regionW * kTilePx * s_zoom,
                     origin.y + regionH * kTilePx * s_zoom);
    dl->AddRectFilled(bg0, bg1, fogCol);

    // Render each observed tile as a textured quad sampled from the
    // 512×512 tileset atlas (built once at startup from the floor
    // PNGs). For tile ids the atlas didn't capture, fall back to a
    // hash-derived colour so the cell at least shows up.
    auto colorForTile = [](uint8_t id) -> ImU32 {
        uint32_t h = id * 0x9E3779B1u + 0xBF58476Du;
        h ^= h >> 16;
        const uint8_t r = 60 + ((h >> 0)  & 0xFF) * 180 / 255;
        const uint8_t g = 60 + ((h >> 8)  & 0xFF) * 180 / 255;
        const uint8_t b = 60 + ((h >> 16) & 0xFF) * 180 / 255;
        return IM_COL32(r, g, b, 255);
    };

    constexpr float kUv = (float)TilesetTexture::kTilePx /
                          (float)TilesetTexture::kTexPx;
    const auto tileTexId = static_cast<ImTextureID>((uintptr_t)tileset.Tex());

    for (int ty = 0; ty < regionH; ++ty)
    {
        for (int tx = 0; tx < regionW; ++tx)
        {
            const uint8_t id = tiles.TileAt(loc->region, loc->floor, tx, ty);
            if (id == 0) continue;
            const float px0 = origin.x + tx * kTilePx * s_zoom;
            const float py0 = origin.y + ty * kTilePx * s_zoom;
            const ImVec2 p0(px0, py0);
            const ImVec2 p1(px0 + kTilePx * s_zoom, py0 + kTilePx * s_zoom);
            if (tileset.Tex() && tileset.Has(id))
            {
                const ImVec2 uv0((id % TilesetTexture::kCols) * kUv,
                                 (id / TilesetTexture::kCols) * kUv);
                const ImVec2 uv1(uv0.x + kUv, uv0.y + kUv);
                dl->AddImage(tileTexId, p0, p1, uv0, uv1);
            }
            else
            {
                dl->AddRectFilled(p0, p1, colorForTile(id));
            }
        }
    }

    // Player marker.
    if (loc->x >= 0 && loc->x < regionW &&
        loc->y >= 0 && loc->y < regionH)
    {
        const float ax = (loc->x + 0.5f) * kTilePx * s_zoom;
        const float ay = (loc->y + 0.5f) * kTilePx * s_zoom;
        const ImVec2 c(origin.x + ax, origin.y + ay);
        const float r = (std::max)(3.0f, kTilePx * s_zoom * 0.45f);
        dl->AddCircleFilled(c, r,        IM_COL32(255, 60, 60, 255));
        dl->AddCircle      (c, r + 1.5f, IM_COL32(0, 0, 0, 255), 0, 1.5f);

        // Auto-centre the canvas on the avatar each frame.
        const ImVec2 view = ImGui::GetWindowSize();
        ImGui::SetScrollX(ax - view.x * 0.5f);
        ImGui::SetScrollY(ay - view.y * 0.5f);
    }

    // Reserve canvas area for scroll bars.
    ImGui::Dummy(ImVec2(regionW * kTilePx * s_zoom,
                        regionH * kTilePx * s_zoom));

    ImGui::EndChild();
    ImGui::End();
}

} // namespace nac
