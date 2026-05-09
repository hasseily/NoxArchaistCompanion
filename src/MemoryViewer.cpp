#include "Emulator/StdAfx.h"

#include "MemoryViewer.h"

#include "Emulator/Memory.h"

#include <imgui.h>

#include <cstdio>

namespace nac
{

void MemoryViewerPanel::Render()
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory", &m_open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    // Bank selector + jump-to-address.
    ImGui::RadioButton("Main", &m_bank, 0); ImGui::SameLine();
    ImGui::RadioButton("Aux",  &m_bank, 1); ImGui::SameLine();

    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Jump $", &m_jumpTo, 0, 0,
                    ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    if (m_jumpTo < 0)       m_jumpTo = 0;
    if (m_jumpTo > 0xFFFF)  m_jumpTo = 0xFFFF;

    ImGui::Separator();

    // MemGetBankPtr flushes the cache before returning, so the bytes
    // we display are always the canonical post-write state — same path
    // the RamSnapshot uses for panel reads.
    const uint8_t* bank = reinterpret_cast<const uint8_t*>(MemGetBankPtr(m_bank == 0 ? 0 : 1));
    if (!bank)
    {
        ImGui::TextDisabled("(bank not allocated)");
        ImGui::End();
        return;
    }

    // 16 bytes per row × 256 rows = full 64 KiB. ImGuiListClipper
    // keeps it cheap regardless of scroll position.
    ImGui::BeginChild("##memhex", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    ImGuiListClipper clipper;
    clipper.Begin(0x10000 / 16);
    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            const int addr = row * 16;
            char hex[16 * 3 + 1] = {};
            char asc[17]         = {};
            for (int i = 0; i < 16; ++i)
            {
                const uint8_t b = bank[addr + i];
                std::snprintf(hex + i * 3, 4, "%02X ", b);
                asc[i] = (b >= 0x20 && b < 0x7F) ? (char)b
                       : (b >= 0xA0 && b < 0xFF) ? (char)(b & 0x7F)   // high-ASCII text
                       : '.';
            }
            ImGui::Text("%04X  %s %s", addr, hex, asc);
        }
    }

    // Jump scroll: when the user types a new address and presses Enter
    // (CharsHexadecimal | EnterReturnsTrue above triggered last frame),
    // SetScrollY here puts the row in view.
    if (ImGui::IsItemDeactivatedAfterEdit() ||
        (m_jumpTo >= 0 && ImGui::IsKeyPressed(ImGuiKey_Enter, false)))
    {
        const float lineH = ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetScrollY((m_jumpTo / 16) * lineH);
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace nac
