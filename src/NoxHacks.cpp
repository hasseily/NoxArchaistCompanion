#include "Emulator/StdAfx.h"

#include "NoxHacks.h"

#include "Emulator/CPU.h"
#include "Emulator/Memory.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>
#include <fstream>

namespace nac
{

bool LoadNoxConstants(const std::filesystem::path& assetsDir,
                      const std::string&           version)
{
    std::memset(&cpuconstants, 0, sizeof(cpuconstants));
    if (version.empty()) return false;

    std::ifstream f(assetsDir / "Versions.json");
    if (!f) return false;

    nlohmann::json j;
    try { f >> j; }
    catch (...) { return false; }

    if (!j.contains(version) || !j[version].is_object()) return false;
    const auto& v = j[version];

    auto u = [&](const char* key) -> UINT {
        try {
            return (UINT)std::stoul(v[key].get<std::string>(), nullptr, 0);
        } catch (...) { return 0; }
    };

    cpuconstants.MEM_PARTY            = u("MEM_PARTY");
    cpuconstants.MEM_FOOD             = u("MEM_FOOD");
    cpuconstants.MEM_GOLD             = u("MEM_GOLD");
    cpuconstants.MEM_TORCHES          = u("MEM_TORCHES");
    cpuconstants.MEM_PICKS            = u("MEM_PICKS");
    cpuconstants.PC_PRINTSTR          = u("PC_PRINTSTR");
    cpuconstants.PC_CARRIAGE_RETURN1  = u("PC_CARRIAGE_RETURN1");
    cpuconstants.PC_CARRIAGE_RETURN2  = u("PC_CARRIAGE_RETURN2");
    cpuconstants.PC_COUT              = u("PC_COUT");
    cpuconstants.A_PRINT_RIGHT        = u("A_PRINT_RIGHT");
    cpuconstants.PC_INITIATE_COMBAT   = u("PC_INITIATE_COMBAT");
    cpuconstants.PC_END_COMBAT        = u("PC_END_COMBAT");
    return true;
}

// ---------------------------------------------------------------------------
// CombatLogPanel
// ---------------------------------------------------------------------------

namespace { CombatLogPanel* s_combatLog = nullptr; }

static void CombatLogTrampoline(char ch, bool flush)
{
    if (s_combatLog) s_combatLog->Append(ch, flush);
}

CombatLogPanel::CombatLogPanel()
{
    s_combatLog = this;
    g_noxLogCallback = &CombatLogTrampoline;
    g_noxLogIncludeCombat = false;
}

void CombatLogPanel::Append(char ch, bool /*flush*/)
{
    // Cap the buffer so a long session doesn't eat all our RAM. Drop the
    // first ~25% in one go when we hit 64 KiB so we don't realloc per char.
    if (m_buf.size() > 64 * 1024)
        m_buf.erase(0, 16 * 1024);
    m_buf.push_back(ch);
}

void CombatLogPanel::Render()
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Nox combat log", &m_open,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) m_buf.clear();
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    ImGui::SameLine();
    if (ImGui::Checkbox("Include combat", &m_includeCombat))
        g_noxLogIncludeCombat = m_includeCombat;

    ImGui::Separator();
    ImGui::BeginChild("##noxlog", ImVec2(0, 0), false);
    // Wrap to the child's right edge so long Nox lines (which have no
    // word breaks of their own) fit without forcing a horizontal scroll.
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
    ImGui::TextUnformatted(m_buf.data(), m_buf.data() + m_buf.size());
    ImGui::PopTextWrapPos();
    if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

// ---------------------------------------------------------------------------
// HackPanel — peek/poke party stats and arbitrary main-RAM addresses.
// ---------------------------------------------------------------------------

namespace
{

// memmain is 64 KiB; cpuconstants.MEM_* may point into either main or aux
// (offsets >= 0x10000 mean the aux half). Returns nullptr if address is
// out of range or the bank isn't allocated yet.
uint8_t* MemPtr(uint32_t addr)
{
    if (addr < 0x10000u)
    {
        uint8_t* p = reinterpret_cast<uint8_t*>(MemGetMainPtr(0));
        return p ? p + addr : nullptr;
    }
    if (addr < 0x20000u)
    {
        uint8_t* p = reinterpret_cast<uint8_t*>(MemGetAuxPtr(0));
        return p ? p + (addr - 0x10000u) : nullptr;
    }
    return nullptr;
}

// 16-bit little-endian read. Nox stores food and gold as two bytes
// (low, high) at the MEM_FOOD / MEM_GOLD addresses.
int Read16(uint32_t addr)
{
    uint8_t* lo = MemPtr(addr);
    uint8_t* hi = MemPtr(addr + 1);
    if (!lo || !hi) return 0;
    return *lo | (*hi << 8);
}
void Write16(uint32_t addr, int value)
{
    uint8_t* lo = MemPtr(addr);
    uint8_t* hi = MemPtr(addr + 1);
    if (!lo || !hi) return;
    *lo = (uint8_t)(value & 0xFF);
    *hi = (uint8_t)((value >> 8) & 0xFF);
}

int Read8(uint32_t addr)
{
    uint8_t* p = MemPtr(addr);
    return p ? *p : 0;
}
void Write8(uint32_t addr, int value)
{
    uint8_t* p = MemPtr(addr);
    if (p) *p = (uint8_t)(value & 0xFF);
}

void DragU16(const char* label, uint32_t addr, int max)
{
    if (!addr) return;          // cpuconstants not populated yet
    int v = Read16(addr);
    if (ImGui::DragInt(label, &v, 1.0f, 0, max))
        Write16(addr, v);
}
void DragU8(const char* label, uint32_t addr)
{
    if (!addr) return;
    int v = Read8(addr);
    if (ImGui::DragInt(label, &v, 1.0f, 0, 255))
        Write8(addr, v);
}

} // namespace

void HackPanel::Render()
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(360, 280), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Nox hack", &m_open,
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    if (cpuconstants.PC_PRINTSTR == 0)
    {
        ImGui::TextDisabled("Insert a Nox Archaist HDV first — these "
                            "fields read from version-specific addresses.");
        ImGui::End();
        return;
    }

    ImGui::Checkbox("Hex", &m_hex);
    const char* fmt = m_hex ? "%X" : "%d";
    ImGui::PushItemWidth(120);

    // Inventory
    DragU16("Food",    cpuconstants.MEM_FOOD,    9999);
    DragU16("Gold",    cpuconstants.MEM_GOLD,    65535);
    DragU8 ("Torches", cpuconstants.MEM_TORCHES);
    DragU8 ("Picks",   cpuconstants.MEM_PICKS);

    ImGui::Separator();
    ImGui::TextUnformatted("Peek / poke");
    ImGui::DragInt("Address (hex)", &m_pokeAddr, 1.0f, 0, 0x1FFFF, "%X");
    int cur = Read8((uint32_t)m_pokeAddr);
    ImGui::DragInt("Value", &cur, 1.0f, 0, 255, fmt);
    if (cur != Read8((uint32_t)m_pokeAddr))
        Write8((uint32_t)m_pokeAddr, cur);

    ImGui::PopItemWidth();
    ImGui::End();
    (void)fmt;
}

} // namespace nac
