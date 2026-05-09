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
// ConversationLogPanel
// ---------------------------------------------------------------------------

namespace { ConversationLogPanel* s_conversationLog = nullptr; }

static void ConversationLogTrampoline(char ch, bool flush)
{
    if (s_conversationLog) s_conversationLog->Append(ch, flush);
}

ConversationLogPanel::ConversationLogPanel()
{
    s_conversationLog = this;
    g_noxLogCallback = &ConversationLogTrampoline;
    g_noxLogIncludeCombat = false;
}

void ConversationLogPanel::ApplyIncludeCombat()
{
    g_noxLogIncludeCombat = m_includeCombat;
}

void ConversationLogPanel::Append(char ch, bool flush)
{
    // Cap the buffer so a long session doesn't eat all our RAM. Drop the
    // first ~25% in one go when we hit 64 KiB so we don't realloc per char.
    if (m_buf.size() > 64 * 1024)
        m_buf.erase(0, 16 * 1024);

    // The trap distinguishes its own paragraph-break sentinels
    // (flush=true, ch='\n' — fired between PRINTSTR calls and at
    // combat-mode line endings) from raw COUT characters
    // (flush=false). The former become real newlines; the latter
    // include Nox's per-line CRs from word-wrapping into the narrow
    // right-scroll panel, which we want to collapse to spaces so the
    // captured text reflows as a paragraph in our wider log window.
    if (flush)
    {
        if (m_buf.empty()) return;
        const size_t n = m_buf.size();
        if (n >= 2 && m_buf[n - 1] == '\n' && m_buf[n - 2] == '\n') return;
        m_buf.push_back('\n');
        return;
    }

    if (ch == '\r' || ch == '\n')
    {
        if (m_buf.empty()) return;
        const char back = m_buf.back();
        if (back == ' ' || back == '\n') return;
        m_buf.push_back(' ');
        return;
    }

    // Eat leading spaces after a real newline (otherwise Nox's
    // per-line indentation prefix shows up at the start of every
    // paragraph).
    if (ch == ' ' && !m_buf.empty() && m_buf.back() == '\n')
        return;

    m_buf.push_back(ch);

    // Visual break after Nox's "<Press a key>" prompt — it ends a
    // narrative beat and the next block reads better on its own line.
    static constexpr char kPrompt[]   = "<Press a key>";
    static constexpr size_t kPromptN  = sizeof(kPrompt) - 1;
    if (m_buf.size() >= kPromptN &&
        std::memcmp(m_buf.data() + m_buf.size() - kPromptN,
                    kPrompt, kPromptN) == 0)
    {
        m_buf.push_back('\n');
    }
}

namespace
{
struct LogScrollState { bool wantTail; };
int LogInputCallback(ImGuiInputTextCallbackData* data)
{
    if (data->EventFlag & ImGuiInputTextFlags_CallbackAlways)
    {
        auto* st = static_cast<LogScrollState*>(data->UserData);
        if (st && st->wantTail)
            data->CursorPos = data->BufTextLen;   // jump cursor → InputText scrolls it into view
    }
    return 0;
}
}

void ConversationLogPanel::RebuildWrappedBuffer(float wrapWidth)
{
    m_wrappedBuf.clear();
    m_wrappedWidth  = wrapWidth;
    m_wrappedBufLen = m_buf.size();
    if (m_buf.empty()) return;

    ImFont* font = ImGui::GetFont();
    if (!font || wrapWidth <= 1.0f)
    {
        m_wrappedBuf = m_buf;
        return;
    }
    const float scale = ImGui::GetFontSize() / font->FontSize;
    m_wrappedBuf.reserve(m_buf.size() + m_buf.size() / 16);

    const char* p   = m_buf.data();
    const char* end = p + m_buf.size();
    while (p < end)
    {
        const char* lineEnd =
            static_cast<const char*>(std::memchr(p, '\n', end - p));
        if (!lineEnd) lineEnd = end;

        if (p == lineEnd)
        {
            // Empty source line → preserve as a paragraph break.
            m_wrappedBuf.push_back('\n');
        }
        else
        {
            const char* lp = p;
            while (lp < lineEnd)
            {
                const char* wrap = font->CalcWordWrapPositionA(
                    scale, lp, lineEnd, wrapWidth);
                if (wrap <= lp)   wrap = lp + 1;       // ensure progress
                if (wrap > lineEnd) wrap = lineEnd;
                m_wrappedBuf.append(lp, wrap - lp);
                m_wrappedBuf.push_back('\n');
                lp = wrap;
                while (lp < lineEnd && *lp == ' ') ++lp;
            }
        }

        p = lineEnd;
        if (p < end) ++p;
    }
}

void ConversationLogPanel::Render()
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Nox conversation log", &m_open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) m_buf.clear();
    ImGui::SameLine();
    if (ImGui::Button("New Paragraph"))
    {
        // One blank line between blocks. No-op on empty buffer; collapses
        // if the previous Append already left a trailing newline.
        if (!m_buf.empty())
        {
            if (m_buf.back() != '\n') m_buf.push_back('\n');
            m_buf.push_back('\n');
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    ImGui::SameLine();
    if (ImGui::Checkbox("Include combat", &m_includeCombat))
        g_noxLogIncludeCombat = m_includeCombat;

    ImGui::Separator();

    // Read-only multiline input gives native selection, Ctrl+C, and a
    // built-in scrollbar — TextUnformatted in a child window can't
    // select across lines. The input doesn't word-wrap on its own, so
    // we feed it m_wrappedBuf (m_buf with hard newlines inserted at
    // the input's content-width boundaries). Rebuilt on width or
    // content change.
    const ImGuiStyle& style = ImGui::GetStyle();
    const float wrapWidth = (std::max)(
        16.0f,
        ImGui::GetContentRegionAvail().x
            - style.ScrollbarSize
            - style.FramePadding.x * 2.0f
            - 4.0f);   // a few pixels of safety so we never trigger an h-scrollbar
    if (m_wrappedBufLen != m_buf.size() || m_wrappedWidth != wrapWidth)
        RebuildWrappedBuffer(wrapWidth);

    LogScrollState st{ m_autoScroll && !m_logFocused };
    constexpr ImGuiInputTextFlags kFlags =
        ImGuiInputTextFlags_ReadOnly |
        ImGuiInputTextFlags_CallbackAlways |
        ImGuiInputTextFlags_NoUndoRedo |
        ImGuiInputTextFlags_NoHorizontalScroll;
    // m_wrappedBuf.data() is mutable (C++17) and NUL-terminated past
    // size(); ImGui won't write through it (ReadOnly).
    ImGui::InputTextMultiline("##noxlog",
                              m_wrappedBuf.data(),
                              m_wrappedBuf.size() + 1,
                              ImVec2(-FLT_MIN, -FLT_MIN),
                              kFlags,
                              &LogInputCallback,
                              &st);
    m_logFocused = ImGui::IsItemFocused();

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

    ImGui::SetNextWindowSize(ImVec2(360, 460), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Nox hack", &m_open, ImGuiWindowFlags_NoCollapse))
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

    // Per-character editor. Party block is six 0x80-byte slots starting at
    // MEM_PARTY; per-member field offsets match the Windows hack dialog.
    if (cpuconstants.MEM_PARTY)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Character");

        // Pull names (high-ASCII, NUL-terminated) at offset 0x4b of each slot.
        // Six fixed slots — show "(empty)" for any with a zero first byte.
        char  names[6][17] = {};
        const char* items[6] = {};
        for (int k = 0; k < 6; ++k)
        {
            uint32_t base = cpuconstants.MEM_PARTY + (uint32_t)k * 0x80;
            for (int i = 0; i < 16; ++i)
            {
                uint8_t c = (uint8_t)Read8(base + 0x4b + i);
                if (c == 0) { names[k][i] = 0; break; }
                names[k][i] = (char)(c & 0x7F);
            }
            names[k][16] = 0;
            items[k] = names[k][0] ? names[k] : "(empty)";
        }
        if (m_member < 0 || m_member > 5) m_member = 0;
        ImGui::Combo("Member", &m_member, items, 6);

        const uint32_t cb = cpuconstants.MEM_PARTY + (uint32_t)m_member * 0x80;
        DragU8 ("Level",    cb + 0x01);
        DragU16("Exp",      cb + 0x05, 65535);
        DragU8 ("Melee",    cb + 0x19);
        DragU8 ("Ranged",   cb + 0x1c);
        DragU8 ("Dodge",    cb + 0x1f);
        DragU8 ("Crit",     cb + 0x22);
        DragU8 ("Lockpick", cb + 0x25);
    }

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
