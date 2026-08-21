#include "Emulator/StdAfx.h"

#include "MemoryViewer.h"

#include "AppleStyle.h"
#include "Emulator/Memory.h"

#include <imgui.h>

#include <cctype>
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

    // Hex grids need a non-proportional font so the columns line up.
    // The interface uses the proportional Nox font; a 10px ProggyForever
    // instance shows more rows while keeping the hex / ASCII columns aligned.
    ImFont* mono = MonoFont();
    if (mono) ImGui::PushFont(mono);

    // Bank selector + jump-to-address.
    ImGui::RadioButton("Main", &m_bank, 0); ImGui::SameLine();
    ImGui::RadioButton("Aux",  &m_bank, 1); ImGui::SameLine();

    ImGui::SetNextItemWidth(80.0f);
    const bool jumpRequested = ImGui::InputInt("Jump $", &m_jumpTo, 0, 0,
        ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
    if (m_jumpTo < 0)       m_jumpTo = 0;
    if (m_jumpTo > 0xFFFF)  m_jumpTo = 0xFFFF;

    // Search row: type a hex byte sequence ("01 02 ff" or "0102FF"),
    // hit Search to mark every match with a yellow background, or
    // Refine to keep only matches whose bytes now equal the new
    // pattern (classic memory-scanner narrowing — search FF, walk in
    // game, refine DE to find the bytes that flipped). Both operate
    // on the active bank only.
    ImGui::SetNextItemWidth(110.0f);
    const bool searchRequested = ImGui::InputText("Search hex",
        m_searchInput, sizeof(m_searchInput),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool searchClicked = ImGui::Button("Search");
    ImGui::SameLine();
    const bool refineClicked = ImGui::Button("Refine");
    ImGui::SameLine();
    const bool clearClicked = ImGui::Button("Clear");
    ImGui::SameLine();
    ImGui::Checkbox("Filter rows", &m_filterRows);
    if (!m_matches[m_bank].empty())
    {
        ImGui::SameLine();
        ImGui::Text("(%zu matches)", m_matches[m_bank].size());
    }

    if (clearClicked)
    {
        m_searchInput[0] = 0;
        m_matches[m_bank].clear();
        m_patternLen[m_bank] = 0;
        std::memset(m_highlight[m_bank], 0, sizeof(m_highlight[m_bank]));
    }

    auto parsePattern = [&]() {
        std::vector<uint8_t> out;
        int hi = -1;
        for (const char* p = m_searchInput; *p; ++p)
        {
            const char c = *p;
            int n = -1;
            if (c >= '0' && c <= '9') n = c - '0';
            else if (c >= 'a' && c <= 'f') n = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') n = 10 + (c - 'A');
            if (n < 0) continue;
            if (hi < 0) hi = n;
            else { out.push_back((uint8_t)((hi << 4) | n)); hi = -1; }
        }
        return out;
    };

    auto regenerateHighlight = [&](int bank, int patLen) {
        std::memset(m_highlight[bank], 0, sizeof(m_highlight[bank]));
        for (int off : m_matches[bank])
            for (int k = 0; k < patLen; ++k)
                m_highlight[bank][off + k] = true;
    };

    if (searchRequested || searchClicked)
    {
        const auto pat = parsePattern();
        m_matches[m_bank].clear();
        m_patternLen[m_bank] = (int)pat.size();
        const uint8_t* bank = reinterpret_cast<const uint8_t*>(
            MemGetBankPtr(m_bank == 0 ? 0 : 1));
        if (bank && !pat.empty())
        {
            const int last = 0x10000 - (int)pat.size();
            for (int i = 0; i <= last; ++i)
                if (std::memcmp(bank + i, pat.data(), pat.size()) == 0)
                    m_matches[m_bank].push_back(i);
        }
        regenerateHighlight(m_bank, (int)pat.size());
    }
    else if (refineClicked && !m_matches[m_bank].empty())
    {
        const auto pat = parsePattern();
        const uint8_t* bank = reinterpret_cast<const uint8_t*>(
            MemGetBankPtr(m_bank == 0 ? 0 : 1));
        if (bank && !pat.empty())
        {
            std::vector<int> kept;
            kept.reserve(m_matches[m_bank].size());
            for (int off : m_matches[m_bank])
            {
                if (off + (int)pat.size() > 0x10000) continue;
                if (std::memcmp(bank + off, pat.data(), pat.size()) == 0)
                    kept.push_back(off);
            }
            m_matches[m_bank].swap(kept);
            m_patternLen[m_bank] = (int)pat.size();
            regenerateHighlight(m_bank, (int)pat.size());
        }
    }

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
        if (mono) ImGui::PopFont();
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

    const ImU32 hiBg = IM_COL32(255, 220, 0, 255);   // bright saturated yellow
    const ImVec4 colHiNormal(0.0f, 0.0f, 0.0f, 1.0f);                // black on yellow
    const ImVec4 colHiChanged(0.6f, 0.0f, 0.0f, 1.0f);               // dark red on yellow
    const bool* hilight = m_highlight[m_bank];
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Build the row list. When "Filter rows" is on, only rows that
    // contain at least one highlighted byte are emitted; the user's
    // clipper / scroll then operates on the filtered set.
    std::vector<int> filteredRows;
    if (m_filterRows)
    {
        for (int row = 0; row < 0x10000 / 16; ++row)
        {
            const int base = row * 16;
            for (int col = 0; col < 16; ++col)
            {
                if (hilight[base + col]) { filteredRows.push_back(row); break; }
            }
        }
    }
    const int totalRows = m_filterRows ? (int)filteredRows.size() : 0x10000 / 16;

    ImGuiListClipper clipper;
    clipper.Begin(totalRows);
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const int row  = m_filterRows ? filteredRows[i] : i;
            const int addr = row * 16;
            ImGui::Text("%04X ", addr);
            for (int col = 0; col < 16; ++col)
            {
                ImGui::SameLine(0.0f, 4.0f);
                const uint8_t b = bank[addr + col];
                const bool hl = hilight[addr + col];
                const double age = now - ages[addr + col];
                ImVec4 c;
                if (hl)
                {
                    c = (age < kHighlightSeconds) ? colHiChanged : colHiNormal;
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const ImVec2 sz = ImGui::CalcTextSize("FF");
                    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), hiBg);
                }
                else
                {
                    c = colorAt(addr + col);
                }
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
    if (mono) ImGui::PopFont();
    ImGui::End();
}

} // namespace nac
