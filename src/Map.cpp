#include "Emulator/StdAfx.h"

#include "Map.h"

#include "Emulator/Memory.h"
#include "RamSnapshot.h"

#include <imgui.h>

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

// All Nox-state reads route through the PC_PRINTSTR-latched snapshot
// instead of live memshadow — see RamSnapshot.h for the rationale.
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

// Deterministic colour-from-tile-id. Phase D placeholder: distinct
// IDs get distinct colours so terrain shape is legible without the
// real tileset PNG (Phase E delivers that). Tile 0 is transparent —
// FindBound includes empty cells outside the drawn area for sparsely
// populated floors.
ImU32 ColorForTile(uint8_t id)
{
    if (id == 0) return 0;                                        // transparent
    uint32_t h = id * 0x9E3779B1u + 0xBF58476Du;
    h ^= h >> 16;
    uint8_t r = 60 + ((h >> 0)  & 0xFF) * 180 / 255;
    uint8_t g = 60 + ((h >> 8)  & 0xFF) * 180 / 255;
    uint8_t b = 60 + ((h >> 16) & 0xFF) * 180 / 255;
    return IM_COL32(r, g, b, 255);
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
        // floor_name: zero-padded ASCII in 4 bytes
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
// MapPanel
// ---------------------------------------------------------------------------

void MapPanel::Render(const MapTranslator& tx, const MapData& md)
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Map", &m_open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    const uint8_t mapID = Peek(0x2AF9);
    const uint8_t xpos  = Peek(0x6CEC);
    const uint8_t ypos  = Peek(0x6CED);

    // Status header — keeps the raw-byte readout from Phase A so we can
    // still spot RAM oddities, but compact (one line).
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

    ImGui::Separator();

    // Tile size: auto-fit on first render, slider afterwards. 6 px is a
    // sweet spot for 80×80 floors at the default window size; user
    // bumps it for small maps, drops it for huge ones (Wynmar 256×256).
    static int s_tilePx = 6;
    ImGui::SliderInt("Tile px", &s_tilePx, 2, 24);
    ImGui::SameLine();
    if (ImGui::Button("Fit"))
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const int byW = (int)(avail.x / fr->width);
        const int byH = (int)((avail.y - 30) / fr->height);
        s_tilePx = (std::max)(2, (std::min)(24, (std::min)(byW, byH)));
    }

    // Drawing surface: scrollable child so big floors pan, with a
    // border so the map's edge is visible.
    ImGui::BeginChild("##map_canvas", ImVec2(0, 0),
                      ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int ty = 0; ty < fr->height; ++ty)
    {
        for (int tx = 0; tx < fr->width; ++tx)
        {
            const uint8_t id = fr->tiles[ty * fr->width + tx];
            const ImU32 col = ColorForTile(id);
            if ((col & IM_COL32_A_MASK) == 0) continue;       // transparent

            const ImVec2 a(origin.x + tx * (float)s_tilePx,
                           origin.y + ty * (float)s_tilePx);
            const ImVec2 b(a.x + s_tilePx, a.y + s_tilePx);
            dl->AddRectFilled(a, b, col);
        }
    }

    // Player marker — translate logical floor coords into tile cells.
    const int px = loc->x - fr->origin_x;
    const int py = loc->y - fr->origin_y;
    if (px >= 0 && px < fr->width && py >= 0 && py < fr->height)
    {
        const ImVec2 c(origin.x + (px + 0.5f) * s_tilePx,
                       origin.y + (py + 0.5f) * s_tilePx);
        const float r = (std::max)(2.0f, s_tilePx * 0.45f);
        dl->AddCircleFilled(c, r,           IM_COL32(255, 60, 60, 255));
        dl->AddCircle      (c, r + 1.5f,    IM_COL32(0, 0, 0, 255), 0, 1.5f);
    }

    // Reserve the canvas area so the scroll bars sit at the right
    // extents — without this, ImGui treats the child as empty and
    // there's nothing to scroll past.
    ImGui::Dummy(ImVec2((float)fr->width  * s_tilePx,
                        (float)fr->height * s_tilePx));

    ImGui::EndChild();
    ImGui::End();
}

} // namespace nac
