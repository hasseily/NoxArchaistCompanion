#include "Emulator/StdAfx.h"

#include "Map.h"

#include "Emulator/Memory.h"

#include <imgui.h>

#include <cstdint>

namespace nac
{

namespace
{

// Soft-switch-aware single-byte read of a CPU-visible address. Same
// path the Templates engine uses — memshadow[] is the page table the
// CPU itself dereferences, so STORE80 / RAMRD / ALTZP toggles in
// flight don't make us read the wrong bank.
uint8_t Peek(uint16_t addr)
{
    const uint8_t* page = memshadow[addr >> 8];
    return page ? page[addr & 0xFF] : 0;
}

} // namespace

void MapPanel::Render()
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(220, 160), ImGuiCond_FirstUseEver);
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
    ImGui::Separator();
    ImGui::Text("xpos     $6CEC = $%02X (%u)", xpos, xpos);
    ImGui::Text("ypos     $6CED = $%02X (%u)", ypos, ypos);

    ImGui::End();
}

} // namespace nac
