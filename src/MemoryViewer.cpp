#include "Emulator/StdAfx.h"

#include "MemoryViewer.h"

#include "Emulator/Memory.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>

namespace nac
{

void MemoryViewerPanel::Render()
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Memory", &m_open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    // Bank selector + jump-to-address.
    ImGui::RadioButton("Main", &m_bank, 0); ImGui::SameLine();
    ImGui::RadioButton("Aux",  &m_bank, 1); ImGui::SameLine();

    ImGui::SetNextItemWidth(80.0f);
    const bool jumpRequested = ImGui::InputInt("Jump $", &m_jumpTo, 0, 0,
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    if (m_jumpTo < 0)       m_jumpTo = 0;
    if (m_jumpTo > 0xFFFF)  m_jumpTo = 0xFFFF;

    ImGui::Separator();

    // Snapshot / diff: pull the canonical bank state through MemGet-
    // BankPtr (which flushes the cache) and compare against last
    // frame's copy. New writes get a fresh timestamp so the render
    // can fade them back over kHighlightSeconds. First open seeds
    // the prev buffer without flagging anything.
    const double now = ImGui::GetTime();
    const uint8_t* mainBank = reinterpret_cast<const uint8_t*>(MemGetBankPtr(0));
    const uint8_t* auxBank  = reinterpret_cast<const uint8_t*>(MemGetBankPtr(1));
    if (mainBank && auxBank)
    {
        if (!m_changeInit)
        {
            std::memcpy(m_prev[0], mainBank, 0x10000);
            std::memcpy(m_prev[1], auxBank,  0x10000);
            m_changeInit = true;
        }
        else
        {
            for (int i = 0; i < 0x10000; ++i)
            {
                if (mainBank[i] != m_prev[0][i])
                {
                    m_prev[0][i]    = mainBank[i];
                    m_changeAt[0][i] = now;
                }
                if (auxBank[i] != m_prev[1][i])
                {
                    m_prev[1][i]    = auxBank[i];
                    m_changeAt[1][i] = now;
                }
            }
        }
    }

    const uint8_t* bank = (m_bank == 0) ? mainBank : auxBank;
    const double*  ages = (m_bank == 0) ? m_changeAt[0] : m_changeAt[1];
    if (!bank)
    {
        ImGui::TextDisabled("(bank not allocated)");
        ImGui::End();
        return;
    }

    ImGui::BeginChild("##memhex", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

    // Per-byte highlight uses ImGui::TextColored on the changed bytes
    // and plain ImGui::Text otherwise. Slightly more draw calls than
    // a single Text per row, but only inside the visible window
    // (ImGuiListClipper) so the cost is bounded.
    const ImVec4 colNormal = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const ImVec4 colFresh  = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);

    auto colorAt = [&](int addr) -> ImVec4 {
        const double age = now - ages[addr];
        if (age >= kHighlightSeconds) return colNormal;
        const float t = (float)(age / kHighlightSeconds);   // 0..1
        return ImVec4(colNormal.x + (colFresh.x - colNormal.x) * (1 - t),
                      colNormal.y + (colFresh.y - colNormal.y) * (1 - t),
                      colNormal.z + (colFresh.z - colNormal.z) * (1 - t),
                      1.0f);
    };

    ImGuiListClipper clipper;
    clipper.Begin(0x10000 / 16);
    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            const int addr = row * 16;
            ImGui::Text("%04X ", addr);
            for (int i = 0; i < 16; ++i)
            {
                ImGui::SameLine(0.0f, 4.0f);
                const uint8_t b = bank[addr + i];
                const ImVec4 c = colorAt(addr + i);
                ImGui::TextColored(c, "%02X", b);
            }

            // ASCII column — high-bit-set characters mean text in Nox,
            // strip the bit before printing. Other non-printables show
            // as '.'.
            char asc[17] = {};
            for (int i = 0; i < 16; ++i)
            {
                const uint8_t b = bank[addr + i];
                asc[i] = (b >= 0x20 && b < 0x7F) ? (char)b
                       : (b >= 0xA0 && b < 0xFF) ? (char)(b & 0x7F)
                       : '.';
            }
            ImGui::SameLine(0.0f, 12.0f);
            ImGui::Text("%s", asc);
        }
    }

    if (jumpRequested)
    {
        const float lineH = ImGui::GetTextLineHeightWithSpacing();
        ImGui::SetScrollY((m_jumpTo / 16) * lineH);
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace nac
