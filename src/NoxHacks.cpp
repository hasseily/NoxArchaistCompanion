#include "Emulator/StdAfx.h"

#include "NoxHacks.h"

#include "ImGuiHelpers.h"

#include "Emulator/CardManager.h"
#include "Emulator/CPU.h"
#include "Emulator/Harddisk.h"
#include "Emulator/Memory.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace nac
{

namespace
{

struct SaveRespecLayout
{
    UINT saveParty = 0;
    UINT saveInventory = 0;
    UINT saveEquipment = 0;
    UINT inventoryDefinitions = 0;
};

SaveRespecLayout s_saveRespecLayout;

}

bool LoadNoxConstants(const std::filesystem::path& assetsDir,
                      const std::string&           version)
{
    std::memset(&cpuconstants, 0, sizeof(cpuconstants));
    s_saveRespecLayout = {};
    g_noxPreloadMenu = false;
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
    cpuconstants.MEM_XPOS             = u("MEM_XPOS");
    cpuconstants.MEM_YPOS             = u("MEM_YPOS");
    cpuconstants.PC_PRINTSTR          = u("PC_PRINTSTR");
    cpuconstants.PC_CARRIAGE_RETURN1  = u("PC_CARRIAGE_RETURN1");
    cpuconstants.PC_CARRIAGE_RETURN2  = u("PC_CARRIAGE_RETURN2");
    cpuconstants.PC_COUT              = u("PC_COUT");
    cpuconstants.A_PRINT_RIGHT        = u("A_PRINT_RIGHT");
    cpuconstants.PC_INITIATE_COMBAT   = u("PC_INITIATE_COMBAT");
    cpuconstants.PC_END_COMBAT        = u("PC_END_COMBAT");
    cpuconstants.PC_MAIN_MENU_IDLE    = u("PC_MAIN_MENU_IDLE");
    cpuconstants.PC_MAIN_MENU_ACTIVATE = u("PC_MAIN_MENU_ACTIVATE");
    s_saveRespecLayout.saveParty      = u("SAVE_PARTY");
    s_saveRespecLayout.saveInventory  = u("SAVE_INVENTORY");
    s_saveRespecLayout.saveEquipment  = u("SAVE_EQUIPMENT");
    s_saveRespecLayout.inventoryDefinitions = u("INVENTORY_DEFINITIONS");
    return true;
}

bool IsNoxRespecAvailable()
{
    return g_noxPreloadMenu && s_saveRespecLayout.saveParty != 0 &&
           s_saveRespecLayout.saveInventory != 0 &&
           s_saveRespecLayout.saveEquipment != 0 &&
           s_saveRespecLayout.inventoryDefinitions != 0;
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

    // Eat leading spaces after a real newline AND collapse runs of
    // spaces (the trap injects an end-of-PRINTSTR space, and Nox's
    // per-line CR also became a space — sometimes both land in a row).
    if (ch == ' ' && !m_buf.empty() &&
        (m_buf.back() == '\n' || m_buf.back() == ' '))
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
    const float scale = ImGui::GetFontSize() / font->LegacySize;
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
    nac::ui::SameLineIfFits(nac::ui::ButtonWidth("New Paragraph"));
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
    nac::ui::SameLineIfFits(nac::ui::CheckableWidth("Auto-scroll"));
    ImGui::Checkbox("Auto-scroll", &m_autoScroll);
    nac::ui::SameLineIfFits(nac::ui::CheckableWidth("Include combat"));
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

    constexpr ImGuiInputTextFlags kFlags =
        ImGuiInputTextFlags_ReadOnly |
        ImGuiInputTextFlags_NoUndoRedo |
        ImGuiInputTextFlags_NoHorizontalScroll;

    // Capture the parent window + the InputText's stack ID *before*
    // calling InputTextMultiline so we can locate its internal scroll
    // child afterwards. ImGui's BeginChildEx names the child as
    // "<parent_name>/<input_label>_<id_hex>" and hashes the full path
    // for the window ID — so neither FindWindowByID(GetID(label)) nor
    // a CallbackAlways callback (which only fires on active edit
    // inputs, not read-only) work; constructing the path string and
    // FindWindowByName does.
    ImGuiWindow* parent  = ImGui::GetCurrentWindow();
    const ImGuiID childId = ImGui::GetID("##noxlog");

    // m_wrappedBuf.data() is mutable (C++17) and NUL-terminated past
    // size(); ImGui won't write through it (ReadOnly).
    ImGui::InputTextMultiline("##noxlog",
                              m_wrappedBuf.data(),
                              m_wrappedBuf.size() + 1,
                              ImVec2(-FLT_MIN, -FLT_MIN),
                              kFlags);

    // Sticky follow-tail. The naive "snap if at bottom" check fails
    // because Scroll.y lags ScrollMax.y by one frame whenever new
    // content arrives, so atBottom reads false and we'd stop pinning
    // immediately. Instead we track intent in m_followTail:
    //
    //   * disengage when the user scrolls below LAST frame's max
    //     (ScrollMax never shrinks on append, so being under it can
    //     only mean a user-driven scroll up — wheel, scrollbar drag,
    //     keyboard, etc.)
    //   * re-engage when they scroll back to the current max
    //
    // The flag persists across frames so newly-appended content keeps
    // pinning the view to the bottom without our snap competing with
    // the user's own input.
    if (m_autoScroll && parent)
    {
        char childName[256];
        std::snprintf(childName, sizeof(childName), "%s/##noxlog_%08X",
                      parent->Name, (unsigned)childId);
        if (ImGuiWindow* child = ImGui::FindWindowByName(childName))
        {
            constexpr float kSlopPx = 8.0f;
            const float maxY = child->ScrollMax.y;
            const float curY = child->Scroll.y;

            if (m_followTail)
            {
                if (curY < m_lastScrollMax - kSlopPx)
                    m_followTail = false;
            }
            else
            {
                if (curY >= maxY - kSlopPx)
                    m_followTail = true;
            }

            if (m_followTail && maxY > 0.0f)
                ImGui::SetScrollY(child, maxY);

            m_lastScrollMax = maxY;
        }
    }
    else
    {
        // Auto-scroll off → next time it's switched on, start tailing
        // again from wherever the buffer ended up.
        m_followTail = true;
        m_lastScrollMax = 0.0f;
    }

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

constexpr size_t kProdosBlockBytes = 512;
constexpr size_t kPartyRecordBytes = 0x80;
constexpr size_t kPartyMembers = 6;
constexpr size_t kInventoryRecordBytes = 6;
constexpr size_t kInventoryBytes = 0x600;
constexpr size_t kEquipmentSlots = 8;
constexpr size_t kEquipmentSummaryBytes = kPartyMembers * kEquipmentSlots * 2;
constexpr size_t kInventoryDefinitionBytes = 256 * 0x20;
constexpr int kMinimumAttribute = 8;

struct ProdosFile
{
    BYTE storage = 0;
    uint16_t keyBlock = 0;
    uint32_t eof = 0;
};

enum class VerifiedWriteResult
{
    Committed,
    NotWritten,
    Restored,
    RecoveryFailed
};

uint16_t ReadLe16(const BYTE* value)
{
    return (uint16_t)(value[0] | (value[1] << 8));
}

class MountedProdos
{
public:
    MountedProdos()
    {
        if (GetCardMgr().QuerySlot(SLOT7) == CT_GenericHDD)
            m_disk = static_cast<HarddiskInterfaceCard*>(
                GetCardMgr().GetObj(SLOT7));
    }

    bool FindFile(uint16_t directoryBlock, const char* wanted,
                  ProdosFile& file)
    {
        std::array<BYTE, kProdosBlockBytes> block{};
        for (int visited = 0; directoryBlock && visited < 128; ++visited)
        {
            if (!ReadBlock(directoryBlock, block.data())) return false;
            for (int slot = 0; slot < 13; ++slot)
            {
                const BYTE* entry = block.data() + 4 + slot * 39;
                const int nameLength = entry[0] & 0x0F;
                if (!nameLength || nameLength > 15) continue;

                std::string name;
                name.reserve((size_t)nameLength);
                for (int i = 0; i < nameLength; ++i)
                    name.push_back((char)(entry[1 + i] & 0x7F));
                if (name != wanted) continue;

                file.storage = entry[0] >> 4;
                file.keyBlock = ReadLe16(entry + 0x11);
                file.eof = entry[0x15] | (entry[0x16] << 8) |
                           (entry[0x17] << 16);
                return true;
            }
            directoryBlock = ReadLe16(block.data() + 2);
        }
        return false;
    }

    bool ReadRange(const ProdosFile& file, uint32_t offset,
                   BYTE* destination, size_t length)
    {
        if (!m_disk || !destination || offset > file.eof ||
            length > file.eof - offset)
            return false;

        std::array<BYTE, kProdosBlockBytes> block{};
        size_t transferred = 0;
        while (transferred < length)
        {
            const uint32_t fileOffset = offset + (uint32_t)transferred;
            UINT imageBlock = 0;
            if (!ResolveImageBlock(file,
                                   fileOffset / kProdosBlockBytes,
                                   imageBlock) ||
                !ReadBlock(imageBlock, block.data()))
                return false;

            const size_t within = fileOffset % kProdosBlockBytes;
            const size_t count = (std::min)(length - transferred,
                                            kProdosBlockBytes - within);
            std::memcpy(destination + transferred,
                        block.data() + within, count);
            transferred += count;
        }
        return true;
    }

    VerifiedWriteResult WriteVerifiedFile(
        const ProdosFile& file,
        const std::vector<BYTE>& originalFile,
        const std::vector<BYTE>& replacementFile)
    {
        if (!m_disk || originalFile.empty() ||
            originalFile.size() != file.eof ||
            replacementFile.size() != originalFile.size())
            return VerifiedWriteResult::NotWritten;

        struct BlockChange
        {
            UINT imageBlock = 0;
            std::array<BYTE, kProdosBlockBytes> original{};
            std::array<BYTE, kProdosBlockBytes> replacement{};
        };
        std::vector<BlockChange> changes;

        const size_t logicalBlocks =
            (originalFile.size() + kProdosBlockBytes - 1) /
            kProdosBlockBytes;
        for (size_t logicalBlock = 0; logicalBlock < logicalBlocks;
             ++logicalBlock)
        {
            const size_t start = logicalBlock * kProdosBlockBytes;
            const size_t count = (std::min)(
                kProdosBlockBytes, originalFile.size() - start);
            if (std::memcmp(originalFile.data() + start,
                            replacementFile.data() + start, count) == 0)
                continue;

            BlockChange change;
            if (!ResolveImageBlock(file, (uint32_t)logicalBlock,
                                   change.imageBlock) ||
                !ReadBlock(change.imageBlock, change.original.data()))
                return VerifiedWriteResult::NotWritten;

            // A malformed ProDOS index must not alias two changed logical
            // blocks to the same physical block.
            for (const BlockChange& existing : changes)
                if (existing.imageBlock == change.imageBlock)
                    return VerifiedWriteResult::NotWritten;

            change.replacement = change.original;
            std::memcpy(change.replacement.data(),
                        replacementFile.data() + start, count);
            changes.push_back(change);
        }

        if (changes.empty()) return VerifiedWriteResult::Committed;

        bool committed = true;
        for (BlockChange& change : changes)
        {
            if (!m_disk->WriteImageBlock(HARDDISK_1, change.imageBlock,
                                         change.replacement.data()))
            {
                committed = false;
                break;
            }
        }
        std::array<BYTE, kProdosBlockBytes> observed{};
        if (committed)
        {
            for (const BlockChange& change : changes)
            {
                if (!ReadBlock(change.imageBlock, observed.data()) ||
                    observed != change.replacement)
                {
                    committed = false;
                    break;
                }
            }
        }
        // Verify the complete logical file as well as each changed physical
        // block. This catches malformed ProDOS indexes that alias a changed
        // logical block with an otherwise unchanged block (or the index
        // block itself).
        if (committed)
        {
            std::vector<BYTE> observedFile(file.eof);
            if (!ReadRange(file, 0, observedFile.data(), observedFile.size()) ||
                observedFile != replacementFile)
                committed = false;
        }
        if (committed) return VerifiedWriteResult::Committed;

        bool alreadyOriginal = true;
        for (const BlockChange& change : changes)
            if (!ReadBlock(change.imageBlock, observed.data()) ||
                observed != change.original)
                alreadyOriginal = false;
        if (alreadyOriginal) return VerifiedWriteResult::Restored;

        // Restore every snapshotted block, including any block whose write
        // was not attempted. This leaves the whole transaction in one known
        // state and makes the recovery verification straightforward.
        for (BlockChange& change : changes)
            m_disk->WriteImageBlock(HARDDISK_1, change.imageBlock,
                                    change.original.data());
        bool restored = true;
        for (const BlockChange& change : changes)
            if (!ReadBlock(change.imageBlock, observed.data()) ||
                observed != change.original)
                restored = false;

        return restored ? VerifiedWriteResult::Restored
                        : VerifiedWriteResult::RecoveryFailed;
    }

private:
    bool ReadBlock(UINT block, BYTE* destination)
    {
        return m_disk &&
               m_disk->ReadImageBlock(HARDDISK_1, block, destination);
    }

    bool ResolveImageBlock(const ProdosFile& file, uint32_t logicalBlock,
                           UINT& imageBlock)
    {
        if (logicalBlock >= 256 ||
            (file.storage != 1 && file.storage != 2))
            return false;

        if (file.storage == 1)
        {
            if (logicalBlock != 0) return false;
            imageBlock = file.keyBlock;
            return imageBlock != 0;
        }

        std::array<BYTE, kProdosBlockBytes> index{};
        if (!ReadBlock(file.keyBlock, index.data())) return false;
        imageBlock = index[logicalBlock] |
                     (index[256 + logicalBlock] << 8);
        return imageBlock != 0;
    }

    HarddiskInterfaceCard* m_disk = nullptr;
};

bool OpenNoxFiles(MountedProdos& disk,
                  std::array<ProdosFile, 2>& saves,
                  ProdosFile* inventoryDefinitions = nullptr)
{
    ProdosFile directory;
    if (!disk.FindFile(2, "NA", directory) || directory.storage != 0x0D ||
        !disk.FindFile(directory.keyBlock, "DATA.SAVE.GAME1", saves[0]) ||
        !disk.FindFile(directory.keyBlock, "DATA.SAVE.GAME2", saves[1]))
        return false;
    return !inventoryDefinitions ||
           disk.FindFile(directory.keyBlock, "SRTN.INVENTORY",
                         *inventoryDefinitions);
}

std::string DecodeHighAscii(const BYTE* value, size_t length)
{
    std::string text;
    for (size_t i = 0; i < length && value[i]; ++i)
    {
        const char decoded = (char)(value[i] & 0x7F);
        if (decoded < 32 || decoded > 126) return {};
        text.push_back(decoded);
    }
    return text;
}

std::string DecodeCharacterName(const BYTE* record)
{
    return DecodeHighAscii(record + 0x4B, 15);
}

bool SaveLayoutFits(const ProdosFile& file)
{
    const uint64_t partyEnd = (uint64_t)s_saveRespecLayout.saveParty +
                              kPartyMembers * kPartyRecordBytes;
    const uint64_t inventoryEnd =
        (uint64_t)s_saveRespecLayout.saveInventory + kInventoryBytes;
    const uint64_t equipmentEnd =
        (uint64_t)s_saveRespecLayout.saveEquipment +
        kEquipmentSummaryBytes;
    return partyEnd <= file.eof && inventoryEnd <= file.eof &&
           equipmentEnd <= file.eof;
}

struct EquipmentKey
{
    BYTE page = 0;
    BYTE item = 0;
};

struct EquipmentViewEntry
{
    std::string slot;
    std::string name;
    bool cachedOnly = false;
    bool readinessOnly = false;
};

struct EquipmentView
{
    std::vector<EquipmentViewEntry> entries;
    bool consistent = true;
};

const char* EquipmentSlotLabel(int slot)
{
    static constexpr const char* labels[kEquipmentSlots] = {
        "Left hand", "Right hand", "Head", "Torso / cloak",
        "Boots", "Gloves", "Ring", "Necklace"
    };
    return slot >= 0 && slot < (int)kEquipmentSlots
        ? labels[slot] : "Other readied item";
}

std::string DecodeItemName(
    const std::array<uint8_t, kInventoryDefinitionBytes>& definitions,
    EquipmentKey key)
{
    const BYTE definition = (BYTE)(key.item + 0x70 * (key.page & 0x0F));
    const BYTE* record = definitions.data() + (size_t)definition * 0x20;
    std::string name = DecodeHighAscii(record + 0x0D, 19);
    if (!name.empty()) return name;

    char fallback[32];
    std::snprintf(fallback, sizeof(fallback), "Item %X/%02X",
                  key.page & 0x0F, key.item);
    return fallback;
}

EquipmentView BuildEquipmentView(
    const std::vector<uint8_t>& save, int member,
    const std::array<uint8_t, kInventoryDefinitionBytes>& definitions)
{
    EquipmentView view;
    if (member < 0 || member >= (int)kPartyMembers ||
        save.size() < s_saveRespecLayout.saveEquipment +
                      kEquipmentSummaryBytes ||
        save.size() < s_saveRespecLayout.saveInventory + kInventoryBytes)
    {
        view.consistent = false;
        return view;
    }

    struct ReadyItem
    {
        EquipmentKey key;
        int slot = 0;
        bool matched = false;
    };
    std::vector<ReadyItem> ready;
    const BYTE readyMask = (BYTE)(1u << (member + 2));
    const size_t inventoryBegin = s_saveRespecLayout.saveInventory;
    const size_t inventoryEnd = inventoryBegin + kInventoryBytes;
    for (size_t offset = inventoryBegin;
         offset + kInventoryRecordBytes <= inventoryEnd;
         offset += kInventoryRecordBytes)
    {
        const BYTE* record = save.data() + offset;
        if (record[0] == 0xFE) continue;
        EquipmentKey key{ (BYTE)(record[0] & 0x0F), record[1] };
        const int inventorySlot = record[5] & 0x1F;
        for (int readyByte = 3; readyByte <= 4; ++readyByte)
        {
            if ((record[readyByte] & readyMask) == 0) continue;
            ready.push_back({ key, inventorySlot, false });
        }
    }

    const BYTE* summary = save.data() + s_saveRespecLayout.saveEquipment +
                          (size_t)member * kEquipmentSlots * 2;
    for (int slot = 0; slot < (int)kEquipmentSlots; ++slot)
    {
        EquipmentKey key{ (BYTE)(summary[slot * 2] & 0x0F),
                          summary[slot * 2 + 1] };
        const int expectedSlot = slot < 2 ? 1 : slot;
        auto match = std::find_if(
            ready.begin(), ready.end(), [&](const ReadyItem& item) {
                return !item.matched && item.key.page == key.page &&
                       item.key.item == key.item &&
                       item.slot == expectedSlot;
            });
        if (match != ready.end())
        {
            match->matched = true;
            view.entries.push_back({ EquipmentSlotLabel(slot),
                                     DecodeItemName(definitions, key) });
            continue;
        }

        // These are the canonical empty pairs. A low item byte of zero is
        // not enough by itself: several real late-game items use xx/00, so a
        // matching ready occurrence above always wins.
        const bool canonicalEmpty =
            (slot == 0 && key.page == 0 && key.item == 0) ||
            (slot == 1 && key.page == 3) ||
            (slot >= 2 && key.page == 1 && key.item == 0);
        if (canonicalEmpty) continue;

        view.consistent = false;
        view.entries.push_back({ EquipmentSlotLabel(slot),
                                 DecodeItemName(definitions, key), true,
                                 false });
    }

    for (const ReadyItem& item : ready)
    {
        if (item.matched) continue;
        view.consistent = false;
        const char* label = item.slot == 1 ? "Hand"
                            : item.slot >= 2 && item.slot <= 7
                                ? EquipmentSlotLabel(item.slot)
                                : EquipmentSlotLabel(-1);
        view.entries.push_back({ label, DecodeItemName(definitions, item.key),
                                 false, true });
    }
    return view;
}

void ApplyUnequippedState(std::vector<BYTE>& save, int member,
                          const std::array<int, 3>& attributes)
{
    const BYTE readyMask = (BYTE)(1u << (member + 2));
    const BYTE keepMask = (BYTE)~readyMask;
    const size_t inventoryBegin = s_saveRespecLayout.saveInventory;
    const size_t inventoryEnd = inventoryBegin + kInventoryBytes;
    for (size_t offset = inventoryBegin;
         offset + kInventoryRecordBytes <= inventoryEnd;
         offset += kInventoryRecordBytes)
    {
        BYTE* item = save.data() + offset;
        if (item[0] == 0xFE) continue;
        item[3] &= keepMask;
        item[4] &= keepMask;
    }

    static constexpr BYTE emptyEquipment[kEquipmentSlots * 2] = {
        0x00, 0x00, 0x03, 0x00,
        0x01, 0x00, 0x01, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x01, 0x00, 0x01, 0x00
    };
    BYTE* equipment = save.data() + s_saveRespecLayout.saveEquipment +
                      (size_t)member * sizeof(emptyEquipment);
    std::copy(std::begin(emptyEquipment), std::end(emptyEquipment), equipment);

    BYTE* record = save.data() + s_saveRespecLayout.saveParty +
                   (size_t)member * kPartyRecordBytes;
    for (int attribute = 0; attribute < 3; ++attribute)
        record[0x10 + attribute] = (BYTE)attributes[attribute];
    // INT is also maximum MP; do not leave current MP above it.
    record[0x04] = (BYTE)(std::min)((int)record[0x04], attributes[2]);

    // These are the same cached values changed by Nox's own full-unready
    // path. Preserve unrelated flag bits while removing the equipment bit.
    record[0x07] = 0;                          // armor rating
    const int hitChance = (std::min)(95, 50 + attributes[1] / 3);
    record[0x08] = (BYTE)(((hitChance / 10) << 4) |
                          (hitChance % 10));    // packed-BCD to-hit
    record[0x09] = (BYTE)(attributes[0] >> 3); // fists damage
    record[0x0A] = 0;                          // no right-hand damage
    record[0x0B] = 0;                          // equipment magic resistance
    record[0x14] &= 0x7F;
    record[0x15] = 0x01;                       // fists / empty-hand mode
    record[0x16] = 0x10;
    record[0x17] = 0x00;
    record[0x40] = 0;                          // left-hand item effect
    record[0x41] = 0;                          // right-hand item effect
    record[0x46] = 0;                          // equipped weight

    std::fill(record + 0x5A, record + 0x6D, 0xAA);
    static constexpr BYTE fists[] = {
        0xC6, 0xE9, 0xF3, 0xF4, 0xF3, 0x00
    };
    std::copy(std::begin(fists), std::end(fists), record + 0x5A);

    std::fill(record + 0x6D, record + 0x80, 0xAA);
    std::fill(record + 0x6D, record + 0x7E, 0xAD);
    record[0x7E] = 0;
}

} // namespace

bool RespecPanel::LoadSaveData()
{
    m_saveDataLoaded = false;
    if (!IsNoxRespecAvailable())
    {
        m_respecStatus = "Respec is available only at the Nox main menu.";
        return false;
    }

    MountedProdos disk;
    std::array<ProdosFile, 2> saves{};
    ProdosFile definitions;
    if (!OpenNoxFiles(disk, saves, &definitions))
    {
        m_respecStatus = "Could not find the Nox saves and item definitions on the mounted HDV.";
        return false;
    }
    if ((uint64_t)s_saveRespecLayout.inventoryDefinitions +
            kInventoryDefinitionBytes > definitions.eof)
    {
        m_respecStatus = "The mounted HDV has an unsupported item-definition layout.";
        return false;
    }

    std::array<std::vector<uint8_t>, 2> loadedSaves;
    std::array<std::array<std::string, kPartyMembers>, 2> loadedNames{};
    std::array<uint8_t, kInventoryDefinitionBytes> loadedDefinitions{};
    if (!disk.ReadRange(definitions,
                        s_saveRespecLayout.inventoryDefinitions,
                        loadedDefinitions.data(), loadedDefinitions.size()))
    {
        m_respecStatus = "Could not read the mounted HDV's item definitions.";
        return false;
    }

    for (int save = 0; save < 2; ++save)
    {
        if (!SaveLayoutFits(saves[save]))
        {
            m_respecStatus = "A Nox save has an unsupported equipment layout.";
            return false;
        }
        loadedSaves[save].resize(saves[save].eof);
        if (!disk.ReadRange(saves[save], 0, loadedSaves[save].data(),
                            loadedSaves[save].size()))
        {
            m_respecStatus = "Could not read the selected HDV's save data.";
            return false;
        }
        for (int member = 0; member < (int)kPartyMembers; ++member)
        {
            const BYTE* record = loadedSaves[save].data() +
                s_saveRespecLayout.saveParty + member * kPartyRecordBytes;
            loadedNames[save][member] = DecodeCharacterName(record);
        }
    }

    m_saveData = std::move(loadedSaves);
    m_saveNames = std::move(loadedNames);
    m_itemDefinitions = std::move(loadedDefinitions);
    m_saveDataLoaded = true;
    m_respecStatus.clear();
    StageSelectedCharacter();
    return true;
}

void RespecPanel::StageSelectedCharacter()
{
    m_respecStatus.clear();
    if (!m_saveDataLoaded || m_saveSlot < 0 || m_saveSlot >= 2 ||
        m_saveMember < 0 || m_saveMember >= (int)kPartyMembers)
    {
        m_respecValues = {};
        return;
    }

    const BYTE* record = m_saveData[m_saveSlot].data() +
        s_saveRespecLayout.saveParty + m_saveMember * kPartyRecordBytes;
    for (int attribute = 0; attribute < 3; ++attribute)
        m_respecValues[attribute] = record[0x10 + attribute];
}

bool RespecPanel::ApplyRespec()
{
    if (!IsNoxRespecAvailable())
    {
        m_respecStatus = "Respec cancelled: the game has left its main menu.";
        return false;
    }
    if (!m_saveDataLoaded || m_saveSlot < 0 || m_saveSlot >= 2 ||
        m_saveMember < 0 || m_saveMember >= (int)kPartyMembers ||
        m_saveNames[m_saveSlot][m_saveMember].empty())
        return false;

    const BYTE* cachedRecord = m_saveData[m_saveSlot].data() +
        s_saveRespecLayout.saveParty + m_saveMember * kPartyRecordBytes;
    for (int value : m_respecValues)
    {
        if (value < kMinimumAttribute || value > 255)
        {
            m_respecStatus = "Each attribute must be between 8 and 255.";
            return false;
        }
    }
    const int originalTotal = cachedRecord[0x10] + cachedRecord[0x11] +
                              cachedRecord[0x12];
    const int newTotal = m_respecValues[0] + m_respecValues[1] +
                         m_respecValues[2];
    if (newTotal != originalTotal)
    {
        m_respecStatus = "Redistribute the existing points before applying.";
        return false;
    }

    MountedProdos disk;
    std::array<ProdosFile, 2> saves{};
    if (!OpenNoxFiles(disk, saves))
    {
        m_respecStatus = "Could not reopen the selected save file.";
        return false;
    }
    if (!SaveLayoutFits(saves[m_saveSlot]) ||
        saves[m_saveSlot].eof != m_saveData[m_saveSlot].size())
    {
        m_respecStatus = "The selected save layout changed; refresh it before applying.";
        return false;
    }

    std::vector<BYTE> current(saves[m_saveSlot].eof);
    if (!disk.ReadRange(saves[m_saveSlot], 0, current.data(), current.size()))
    {
        m_respecStatus = "Could not reread the selected save.";
        return false;
    }
    if (current != m_saveData[m_saveSlot])
    {
        m_respecStatus = "The save changed; refresh it before applying.";
        return false;
    }

    std::vector<BYTE> replacement = current;
    ApplyUnequippedState(replacement, m_saveMember, m_respecValues);
    if (replacement == current)
    {
        m_respecStatus = "No attribute or equipment changes were needed.";
        return true;
    }

    const VerifiedWriteResult result = disk.WriteVerifiedFile(
        saves[m_saveSlot], current, replacement);
    if (result != VerifiedWriteResult::Committed)
    {
        if (result == VerifiedWriteResult::NotWritten)
            m_respecStatus = "Respec failed before writing; the save was not changed.";
        else if (result == VerifiedWriteResult::Restored)
            m_respecStatus = "Respec failed; the original save blocks were verified.";
        else
            m_respecStatus = "Respec failed and the original save blocks could not be restored.";
        return false;
    }

    m_saveData[m_saveSlot] = std::move(replacement);
    m_respecStatus = "Respec saved to slot " +
        std::to_string(m_saveSlot + 1) +
        ". All equipment was removed from the character.";
    return true;
}

void RespecPanel::RenderContents()
{
    if (!m_saveLoadAttempted)
    {
        m_saveLoadAttempted = true;
        LoadSaveData();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.72f, 0.30f, 1.0f));
    ImGui::TextWrapped("Respeccing this character will remove all equipment "
                       "from that character.");
    ImGui::PopStyleColor();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("The selected save file is edited directly; no game UI "
                        "or live character memory is used.");
    ImGui::PopTextWrapPos();

    if (ImGui::Button("Refresh saves"))
        LoadSaveData();
    if (!m_saveDataLoaded)
    {
        if (!m_respecStatus.empty())
            ImGui::TextWrapped("%s", m_respecStatus.c_str());
        return;
    }

    static const char* saveLabels[] = { "Save 1", "Save 2" };
    if (ImGui::Combo("Save file", &m_saveSlot, saveLabels, 2))
        StageSelectedCharacter();

    const char* memberLabels[kPartyMembers]{};
    for (int member = 0; member < (int)kPartyMembers; ++member)
    {
        const std::string& name = m_saveNames[m_saveSlot][member];
        memberLabels[member] = name.empty() ? "(empty)" : name.c_str();
    }
    if (ImGui::Combo("Character", &m_saveMember, memberLabels,
                     (int)kPartyMembers))
        StageSelectedCharacter();

    const bool memberPresent =
        !m_saveNames[m_saveSlot][m_saveMember].empty();
    if (memberPresent)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Equipped");
        const EquipmentView equipment = BuildEquipmentView(
            m_saveData[m_saveSlot], m_saveMember, m_itemDefinitions);
        if (equipment.entries.empty())
        {
            ImGui::TextDisabled("None");
        }
        else
        {
            for (const EquipmentViewEntry& item : equipment.entries)
            {
                const char* suffix = item.cachedOnly
                    ? " (equipment table only)"
                    : item.readinessOnly ? " (readiness table only)" : "";
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("%s: %s%s", item.slot.c_str(),
                                   item.name.c_str(), suffix);
            }
        }
        if (!equipment.consistent)
            ImGui::TextWrapped("The save's equipment tables disagree. Applying "
                               "the respec will clear and repair both tables.");

        ImGui::Separator();
        const BYTE* record = m_saveData[m_saveSlot].data() +
            s_saveRespecLayout.saveParty +
            m_saveMember * kPartyRecordBytes;
        const int pointTotal = record[0x10] + record[0x11] + record[0x12];
        const int maximumAttribute = (std::max)(
            kMinimumAttribute,
            (std::min)(255, pointTotal - 2 * kMinimumAttribute));
        ImGui::PushItemWidth(180.0f);
        ImGui::DragInt("STR", &m_respecValues[0], 1.0f,
                       kMinimumAttribute, maximumAttribute, nullptr,
                       ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragInt("DEX", &m_respecValues[1], 1.0f,
                       kMinimumAttribute, maximumAttribute, nullptr,
                       ImGuiSliderFlags_AlwaysClamp);
        ImGui::DragInt("INT", &m_respecValues[2], 1.0f,
                       kMinimumAttribute, maximumAttribute, nullptr,
                       ImGuiSliderFlags_AlwaysClamp);
        ImGui::PopItemWidth();

        const int64_t remaining = (int64_t)pointTotal - m_respecValues[0] -
                                  m_respecValues[1] - m_respecValues[2];
        ImGui::Text("Points remaining: %lld", (long long)remaining);
        ImGui::BeginDisabled(remaining != 0);
        if (ImGui::Button("Apply respec and unequip")) ApplyRespec();
        ImGui::EndDisabled();
    }
    else
    {
        ImGui::TextDisabled("This character slot is empty.");
    }

    if (!m_respecStatus.empty())
        ImGui::TextWrapped("%s", m_respecStatus.c_str());
}

void RespecPanel::Invalidate()
{
    m_saveDataLoaded = false;
    m_saveLoadAttempted = false;
    for (auto& save : m_saveData) save.clear();
    for (auto& names : m_saveNames)
        for (std::string& name : names) name.clear();
    m_itemDefinitions.fill(0);
    m_respecValues = {};
    m_respecStatus.clear();
}

void RespecPanel::Render()
{
    if (!IsNoxRespecAvailable())
    {
        m_open = false;
        m_wasOpen = false;
        Invalidate();
        return;
    }
    if (!m_open)
    {
        m_wasOpen = false;
        return;
    }
    if (!m_wasOpen)
    {
        Invalidate();
        m_wasOpen = true;
    }

    ImGui::SetNextWindowSize(ImVec2(440, 520), ImGuiCond_FirstUseEver);
    const bool visible = ImGui::Begin("Respec (main menu only)", &m_open,
                                      ImGuiWindowFlags_NoCollapse);
    if (visible && m_open) RenderContents();
    ImGui::End();

    if (!m_open)
    {
        m_wasOpen = false;
        Invalidate();
    }
}

void HackPanel::Render()
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(360, 460), ImGuiCond_FirstUseEver);
    const bool windowVisible =
        ImGui::Begin("Nox hack", &m_open, ImGuiWindowFlags_NoCollapse);
    if (!windowVisible)
    {
        ImGui::End();
        return;
    }

    if (cpuconstants.PC_PRINTSTR == 0)
    {
        nac::ui::TextDisabledWrapped(
            "Insert a Nox Archaist HDV first — these fields read from "
            "version-specific addresses.");
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
