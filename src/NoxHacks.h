#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace nac
{

// Populate the emulator-side noxcpuconstants struct from the matching
// version entry in NoxData/Versions.json. Returns true if a matching
// version was found and the constants were applied. Without this, the
// CPU Fetch trap stays dormant (cpuconstants.PC_PRINTSTR == 0) and the
// hack panel can't address the right party-data offsets.
bool LoadNoxConstants(const std::filesystem::path& assetsDir,
                      const std::string&           version);

// True only while the supported Nox loader is executing its verified
// pre-save main-menu loop.  The respec window and its Apply action both use
// this gate; a static string in loader memory is deliberately not enough.
bool IsNoxRespecAvailable();

// ImGui panels — one window each, toggled from the main menu bar.

// "Nox conversation log": appends each character the game's PRINT
// routines emit to the right scroll panel (NPC conversations,
// look-at descriptions, etc.) into a scrolling text buffer. Combat
// text is gated separately by the Include-combat checkbox — without
// it, only out-of-combat output is captured.
class ConversationLogPanel
{
public:
    ConversationLogPanel();

    bool* OpenFlag()       { return &m_open; }
    void  Render();

    // Called from the emulator's CPU Fetch trap (via the
    // g_noxLogCallback function pointer). Cheap — appends one char and
    // flips a flush flag for the next Render to consume.
    void  Append(char ch, bool flush);

    bool& OpenRef()           { return m_open; }
    bool& AutoScrollRef()     { return m_autoScroll; }
    bool& IncludeCombatRef()  { return m_includeCombat; }
    void  ApplyIncludeCombat();   // re-syncs g_noxLogIncludeCombat after load

private:
    void RebuildWrappedBuffer(float wrapWidth);

    bool        m_open       = false;
    bool        m_autoScroll = true;
    bool        m_includeCombat = false;
    bool        m_followTail = true;        // sticky: true while we're tailing the bottom
    float       m_lastScrollMax = 0.0f;     // ScrollMax.y from the previous frame
    std::string m_buf;

    // m_buf is the raw text from the trap (Nox CRs already collapsed
    // to spaces). m_wrappedBuf is the same content with hard newlines
    // inserted at the InputTextMultiline's content-width boundaries —
    // the input doesn't word-wrap natively, so we pre-wrap and feed
    // the wrapped form to it. Rebuilt only when the source content or
    // the current wrap width changes.
    std::string m_wrappedBuf;
    float       m_wrappedWidth   = 0.0f;
    size_t      m_wrappedBufLen  = 0;
};

// "Nox hack": peek/poke party stats via cpuconstants offsets. Provides
// torches / picks / gold / food editors plus an arbitrary-address
// peek/poke field.
class HackPanel
{
public:
    bool* OpenFlag() { return &m_open; }
    void  Render();

    bool& OpenRef()      { return m_open; }
    bool& HexRef()       { return m_hex; }
    int&  PokeAddrRef()  { return m_pokeAddr; }

private:
    bool m_open      = false;
    bool m_hex       = false;
    int  m_pokeAddr  = 0x6CEC;
    int  m_pokeValue = 0;
    int  m_member    = 0;     // 0..5, current party member in the per-character editor
};

// "Respec (main menu only)": edits one of the two save files directly while
// no save is loaded. Applying a respec also removes all equipment from the
// selected character in every save representation used by the game.
class RespecPanel
{
public:
    bool* OpenFlag() { return &m_open; }
    void  Render();

private:
    bool LoadSaveData();
    void StageSelectedCharacter();
    bool ApplyRespec();
    void RenderContents();
    void Invalidate();

    static constexpr size_t kInventoryDefinitionBytes = 256 * 0x20;
    std::array<std::vector<uint8_t>, 2> m_saveData{};
    std::array<std::array<std::string, 6>, 2> m_saveNames{};
    std::array<uint8_t, kInventoryDefinitionBytes> m_itemDefinitions{};
    std::array<int, 3> m_respecValues{};
    bool        m_open = false;
    int         m_saveSlot = 0;
    int         m_saveMember = 0;
    bool        m_saveDataLoaded = false;
    bool        m_saveLoadAttempted = false;
    bool        m_wasOpen = false;
    std::string m_respecStatus;
};

} // namespace nac
