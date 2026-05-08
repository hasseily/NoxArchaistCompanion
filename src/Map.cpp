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

// Translate the GC FriendlyName form the EXPORTPNGS.NUT writes into
// PNG filenames ("Ground Floor", "Floor 2", "Basement 1") into the
// short floor codes the rest of NAC uses ("G", "F2", "B1"). Inverse
// of pack_maps.py's normalise_floor.
std::string FriendlyFromShort(const std::string& s)
{
    if (s == "G")              return "Ground Floor";
    if (!s.empty() && s[0] == 'F') return "Floor "    + s.substr(1);
    if (!s.empty() && s[0] == 'B') return "Basement " + s.substr(1);
    return s;
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
// FloorImageCache
// ---------------------------------------------------------------------------

void FloorImageCache::SetRoot(const std::filesystem::path& assetsDir)
{
    m_root = assetsDir / "maps" / "floors";
}

const FloorImageCache::Entry& FloorImageCache::Lookup(const std::string& region_name,
                                                      const std::string& floor_short)
{
    const std::string key = region_name + "/" + floor_short;
    auto it = m_cache.find(key);
    if (it != m_cache.end()) return it->second;

    Entry e;
    const std::string fname = region_name + "_" + FriendlyFromShort(floor_short) + ".png";
    const auto path = m_root / fname;
    if (std::filesystem::exists(path))
    {
        int w = 0, h = 0, channels = 0;
        unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &channels, 4);
        if (data)
        {
            GLuint tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
            glBindTexture(GL_TEXTURE_2D, 0);
            stbi_image_free(data);
            e.tex    = tex;
            e.width  = w;
            e.height = h;
        }
    }
    auto [ins, ok] = m_cache.emplace(key, e);
    return ins->second;
}

// ---------------------------------------------------------------------------
// FogOfWar
// ---------------------------------------------------------------------------

void FogOfWar::Load(const std::filesystem::path& prefDir)
{
    m_path = prefDir / "fogofwar.json";
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
            const std::string b64 = fjson.value("seen_b64", std::string{});
            if (region == 0 || floor.empty() || w <= 0 || h <= 0) continue;
            Floor& fl = EnsureFloor(region, floor, w, h);
            // Encoding: each byte in seen_b64 is one tile (0 / 1) — this
            // is wasteful (8x what a packed bitset would take) but the
            // file stays human-inspectable and ~200 KB total fits easily
            // in pref_dir. Decode is a straight base64 → byte copy.
            // Use a tiny inline base64 to avoid pulling another lib.
            static const std::string T =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::vector<int> tab(256, -1);
            for (int i = 0; i < 64; ++i) tab[(uint8_t)T[i]] = i;
            std::vector<uint8_t> bytes;
            bytes.reserve(b64.size() * 3 / 4);
            int val = 0, valb = -8;
            for (char c : b64)
            {
                if (c == '=') break;
                if (tab[(uint8_t)c] < 0) continue;
                val = (val << 6) | tab[(uint8_t)c];
                valb += 6;
                if (valb >= 0) { bytes.push_back((uint8_t)((val >> valb) & 0xFF)); valb -= 8; }
            }
            const size_t n = (std::min)(bytes.size(), (size_t)w * (size_t)h);
            std::memcpy(fl.seen.data(), bytes.data(), n);
        }
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "FogOfWar: parse failed: %s\n", e.what());
    }
}

void FogOfWar::Save() const
{
    if (!m_dirty || m_path.empty()) return;
    static const std::string T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [k, fl] : m_floors)
    {
        std::string b64;
        b64.reserve((fl.seen.size() + 2) / 3 * 4);
        int val = 0, valb = -6;
        for (uint8_t c : fl.seen)
        {
            val = (val << 8) | c;
            valb += 8;
            while (valb >= 0)
            {
                b64.push_back(T[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) b64.push_back(T[((val << 8) >> (valb + 8)) & 0x3F]);
        while (b64.size() % 4) b64.push_back('=');

        arr.push_back({
            {"region",   k.first},
            {"floor",    k.second},
            {"w",        fl.width},
            {"h",        fl.height},
            {"seen_b64", b64},
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

FogOfWar::Floor& FogOfWar::EnsureFloor(int region_id, const std::string& floor,
                                       int floorWidth, int floorHeight)
{
    auto& fl = m_floors[{region_id, floor}];
    if ((int)fl.seen.size() != floorWidth * floorHeight)
    {
        fl.width  = floorWidth;
        fl.height = floorHeight;
        fl.seen.assign((size_t)floorWidth * (size_t)floorHeight, 0);
    }
    return fl;
}

void FogOfWar::Reveal(int region_id, const std::string& floor,
                      int playerX, int playerY,
                      int floorWidth, int floorHeight)
{
    if (floorWidth <= 0 || floorHeight <= 0) return;
    Floor& fl = EnsureFloor(region_id, floor, floorWidth, floorHeight);

    const int x0 = (std::max)(0,                playerX - kVisionW / 2);
    const int x1 = (std::min)(floorWidth  - 1,  playerX + kVisionW / 2);
    const int y0 = (std::max)(0,                playerY - kVisionH / 2);
    const int y1 = (std::min)(floorHeight - 1,  playerY + kVisionH / 2);

    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            if (!fl.seen[y * floorWidth + x])
            {
                fl.seen[y * floorWidth + x] = 1;
                m_dirty = true;
            }
}

bool FogOfWar::IsRevealed(int region_id, const std::string& floor, int x, int y) const
{
    auto it = m_floors.find({region_id, floor});
    if (it == m_floors.end()) return false;
    const Floor& fl = it->second;
    if (x < 0 || x >= fl.width || y < 0 || y >= fl.height) return false;
    return fl.seen[y * fl.width + x] != 0;
}

const std::vector<uint8_t>* FogOfWar::Bitmap(int region_id,
                                             const std::string& floor) const
{
    auto it = m_floors.find({region_id, floor});
    return it == m_floors.end() ? nullptr : &it->second.seen;
}

// ---------------------------------------------------------------------------
// MapPanel
// ---------------------------------------------------------------------------

void MapPanel::Render(const MapTranslator& tx, const MapData& md,
                      FloorImageCache& images, FogOfWar& fog)
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
    const uint8_t ypos  = Peek(0x6CED);

    ImGui::Text("mapID $%02X  xpos $%02X  ypos $%02X", mapID, xpos, ypos);

    auto loc = tx.Resolve(mapID, xpos, ypos);
    if (!loc)
    {
        ImGui::TextDisabled("(no rule matched — likely on the BASIC prompt)");
        ImGui::End();
        return;
    }

    ImGui::Text("Region %d (%s) — Floor %s — Tile %d, %d",
                loc->region, loc->region_name.c_str(),
                loc->floor.c_str(), loc->x, loc->y);

    const FloorRecord* fr = md.Find(loc->region, loc->floor);
    if (!fr)
    {
        ImGui::TextDisabled("(no map data for this region/floor)");
        ImGui::End();
        return;
    }

    // Drop a 17×11 reveal each frame the snapshot puts the player on a
    // valid tile of this floor. Cheap: it only writes when a tile flips
    // from un-seen to seen.
    fog.Reveal(loc->region, loc->floor,
               loc->x - fr->origin_x, loc->y - fr->origin_y,
               fr->width, fr->height);

    const auto& img = images.Lookup(loc->region_name, loc->floor);

    ImGui::Separator();

    static float s_zoom = 1.0f;
    ImGui::SliderFloat("Zoom", &s_zoom, 0.25f, 4.0f, "%.2fx");
    ImGui::SameLine();
    if (ImGui::Button("Fit") && img.tex && img.width && img.height)
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float zx = avail.x / img.width;
        const float zy = (avail.y - 30) / img.height;
        s_zoom = (std::max)(0.1f, (std::min)(zx, zy));
    }

    ImGui::BeginChild("##map_canvas", ImVec2(0, 0),
                      ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

    if (img.tex == 0)
    {
        ImGui::TextDisabled("(no PNG for %s / %s — drop one into Assets/maps/floors/)",
                            loc->region_name.c_str(),
                            FriendlyFromShort(loc->floor).c_str());
    }
    else
    {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size((float)img.width * s_zoom, (float)img.height * s_zoom);

        ImGui::Image(static_cast<ImTextureID>((uintptr_t)img.tex), size);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Fog overlay: paint solid dark grey over every undiscovered
        // tile in the floor record. Drawn AFTER the image so it covers
        // unrevealed terrain. Rectangles are computed in floor-tile
        // space and converted to PNG pixel space via the floor's
        // origin offset and the tileset's 14×16 native tile size.
        constexpr float kTileW = 14.0f;
        constexpr float kTileH = 16.0f;
        const ImU32 fogCol = IM_COL32(20, 20, 24, 255);

        for (int ty = 0; ty < fr->height; ++ty)
        {
            for (int tx = 0; tx < fr->width; ++tx)
            {
                if (fog.IsRevealed(loc->region, loc->floor, tx, ty)) continue;
                const float px0 = tx * kTileW * s_zoom;
                const float py0 = ty * kTileH * s_zoom;
                const float px1 = px0 + kTileW * s_zoom;
                const float py1 = py0 + kTileH * s_zoom;
                dl->AddRectFilled(ImVec2(origin.x + px0, origin.y + py0),
                                  ImVec2(origin.x + px1, origin.y + py1),
                                  fogCol);
            }
        }

        // Player marker.
        const int px = loc->x - fr->origin_x;
        const int py = loc->y - fr->origin_y;
        if (px >= 0 && px < fr->width && py >= 0 && py < fr->height)
        {
            const ImVec2 c(origin.x + (px + 0.5f) * kTileW * s_zoom,
                           origin.y + (py + 0.5f) * kTileH * s_zoom);
            const float r = (std::max)(3.0f, kTileW * s_zoom * 0.45f);
            dl->AddCircleFilled(c, r,        IM_COL32(255, 60, 60, 255));
            dl->AddCircle      (c, r + 1.5f, IM_COL32(0, 0, 0, 255), 0, 1.5f);
        }
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace nac
