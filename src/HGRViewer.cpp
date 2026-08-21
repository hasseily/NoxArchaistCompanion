#include "Emulator/StdAfx.h"

#include "HGRViewer.h"

#include "Emulator/Memory.h"
#include "ImGuiHelpers.h"

#include <imgui.h>
#include <glad/glad.h>

#include <cstring>

namespace nac
{

void HGRViewerPanel::EnsureTexture()
{
    if (m_tex) return;
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_tex = tex;
}

void HGRViewerPanel::Refresh()
{
    const uint8_t* bank = reinterpret_cast<const uint8_t*>(MemGetBankPtr(m_bank == 0 ? 0 : 1));
    if (!bank) { std::memset(m_pixels, 0, sizeof(m_pixels)); return; }

    // //e HGR interleave: for line y in [0..191],
    //   row   = y % 8                  (0..7  — micro-row within a third)
    //   third = (y % 64) / 8           (0..7  — third within a segment)
    //   seg   = y / 64                 (0..2  — top / mid / bottom)
    // Line base offset = row*$400 + third*$80 + seg*$28.
    // Each row is 40 bytes × 7 monochrome pixels (bit 0 = leftmost,
    // bit 7 = palette select on real //e — we ignore palette for the
    // raw bitmap view).
    for (int y = 0; y < kH; ++y)
    {
        const int row   = y % 8;
        const int third = (y % 64) / 8;
        const int seg   = y / 64;
        const int line_off = row * 0x400 + third * 0x80 + seg * 0x28;

        for (int col = 0; col < 40; ++col)
        {
            const int addr = (m_baseAddr + line_off + col) & 0xFFFF;
            uint8_t b = bank[addr];
            if (m_invert) b = (uint8_t)(~b);
            for (int bit = 0; bit < 7; ++bit)
            {
                const int x = col * 7 + bit;
                const uint8_t v = (b & (1 << bit)) ? 0xFF : 0x00;
                uint8_t* p = m_pixels + (y * kW + x) * 4;
                p[0] = v; p[1] = v; p[2] = v; p[3] = 0xFF;
            }
        }
    }

    EnsureTexture();
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, m_pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void HGRViewerPanel::Render()
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(720, 540), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("HGR viewer", &m_open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    ImGui::RadioButton("Main", &m_bank, 0);
    nac::ui::SameLineIfFits(nac::ui::CheckableWidth("Aux"));
    ImGui::RadioButton("Aux",  &m_bank, 1);

    nac::ui::SameLineIfFits(nac::ui::LabeledItemWidth(80.0f, "Base $"));
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("Base $", &m_baseAddr, 0, 0,
                    ImGuiInputTextFlags_CharsHexadecimal);

    // Quick jump buttons for the standard pages and adjacent 8 KiB
    // chunks — saves typing while scanning.
    static constexpr struct { const char* label; int addr; } kQuickJumps[] = {
        { "$2000",  0x2000 },
        { "$4000",  0x4000 },
        { "$6000",  0x6000 },
        { "$8000",  0x8000 },
        { "$A000",  0xA000 },
        { "$C000",  0xC000 },
    };
    for (const auto& q : kQuickJumps)
    {
        nac::ui::SameLineIfFits(nac::ui::ButtonWidth(q.label));
        if (ImGui::SmallButton(q.label)) m_baseAddr = q.addr;
    }

    if (ImGui::SmallButton("- $400")) m_baseAddr -= 0x400;
    nac::ui::SameLineIfFits(nac::ui::ButtonWidth("+ $400"));
    if (ImGui::SmallButton("+ $400")) m_baseAddr += 0x400;
    nac::ui::SameLineIfFits(nac::ui::ButtonWidth("- $100"));
    if (ImGui::SmallButton("- $100")) m_baseAddr -= 0x100;
    nac::ui::SameLineIfFits(nac::ui::ButtonWidth("+ $100"));
    if (ImGui::SmallButton("+ $100")) m_baseAddr += 0x100;
    nac::ui::SameLineIfFits(nac::ui::CheckableWidth("Invert"));
    ImGui::Checkbox("Invert", &m_invert);

    m_baseAddr &= 0xFFFF;

    ImGui::SetNextItemWidth(80.0f);
    ImGui::SliderInt("Zoom", &m_zoom, 1, 6);

    ImGui::TextWrapped("Showing $%04X..$%04X (%s, %s)",
                       m_baseAddr, (m_baseAddr + 0x1FFF) & 0xFFFF,
                       m_bank == 0 ? "main" : "aux",
                       m_invert ? "inverted" : "normal");

    ImGui::Separator();

    Refresh();

    ImGui::BeginChild("##hgr_canvas", ImVec2(0, 0), false,
                      ImGuiWindowFlags_HorizontalScrollbar);
    if (m_tex)
    {
        ImGui::Image(static_cast<ImTextureID>((uintptr_t)m_tex),
                     ImVec2((float)kW * m_zoom, (float)kH * m_zoom));
    }
    ImGui::EndChild();

    ImGui::End();
}

} // namespace nac
