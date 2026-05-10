#pragma once

#include <filesystem>
#include <string>

namespace nac
{

// Populate the emulator-side noxcpuconstants struct from the matching
// version entry in Assets/Versions.json. Returns true if a matching
// version was found and the constants were applied. Without this, the
// CPU Fetch trap stays dormant (cpuconstants.PC_PRINTSTR == 0) and the
// hack panel can't address the right party-data offsets.
bool LoadNoxConstants(const std::filesystem::path& assetsDir,
                      const std::string&           version);

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

} // namespace nac
