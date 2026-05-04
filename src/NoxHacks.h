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

// "Nox combat log": appends each character the game's PRINT routines
// emit to the right scroll panel (conversations) into a scrolling text
// buffer. Lines from inside combat are gated on the Include-combat
// checkbox.
class CombatLogPanel
{
public:
    CombatLogPanel();

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
    bool        m_open       = false;
    bool        m_autoScroll = true;
    bool        m_includeCombat = false;
    std::string m_buf;
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
};

} // namespace nac
