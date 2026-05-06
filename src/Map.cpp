#include "Emulator/StdAfx.h"

#include "Map.h"

#include "Emulator/Memory.h"

#include <imgui.h>

#include <cstdint>
#include <cstdio>
#include <fstream>

namespace nac
{

namespace
{

uint8_t Peek(uint16_t addr)
{
    const uint8_t* page = memshadow[addr >> 8];
    return page ? page[addr & 0xFF] : 0;
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

// translation.json `<check offset>` indexes into the original 5-element
// peek list from the GC profile (mapID, region, maptype, xpos, ypos).
// We only end up reading three of those slots; the others are unused
// by every shipped rule.
int SampleAtPeekOffset(int peekOffset, uint8_t mapID, uint8_t xpos, uint8_t ypos)
{
    switch (peekOffset)
    {
    case 0: return mapID;
    case 3: return xpos;
    case 4: return ypos;
    default: return -1;   // intentionally fails any check
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

std::optional<MapLocation> MapTranslator::Resolve(uint8_t mapID,
                                                  uint8_t xpos,
                                                  uint8_t ypos) const
{
    if (m_data.is_null() || !m_data.contains("rules") || !m_data["rules"].is_array())
        return std::nullopt;

    for (const auto& rule : m_data["rules"])
    {
        // Range gate first — cheaper than walking the checks array.
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
// MapPanel
// ---------------------------------------------------------------------------

void MapPanel::Render(const MapTranslator& tx)
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(280, 220), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Map (debug)", &m_open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    const uint8_t mapID   = Peek(0x2AF9);
    const uint8_t region  = Peek(0x0000);
    const uint8_t maptype = Peek(0x267D);
    const uint8_t xpos    = Peek(0x6CEC);
    const uint8_t ypos    = Peek(0x6CED);

    ImGui::Text("mapID    $2AF9 = $%02X (%u)", mapID,   mapID);
    ImGui::Text("region   $0000 = $%02X (%u)", region,  region);
    ImGui::Text("maptype  $267D = $%02X (%u)", maptype, maptype);
    ImGui::Text("xpos     $6CEC = $%02X (%u)", xpos, xpos);
    ImGui::Text("ypos     $6CED = $%02X (%u)", ypos, ypos);

    ImGui::Separator();
    if (!tx.Loaded())
    {
        ImGui::TextDisabled("translation.json not loaded");
    }
    else if (auto loc = tx.Resolve(mapID, xpos, ypos))
    {
        ImGui::Text("Region: %d (%s)", loc->region, loc->region_name.c_str());
        ImGui::Text("Floor:  %s", loc->floor.c_str());
        ImGui::Text("Tile:   %d, %d  of  %d x %d",
                    loc->x, loc->y, loc->width, loc->height);
    }
    else
    {
        ImGui::TextDisabled("(no rule matched)");
    }

    ImGui::End();
}

} // namespace nac
