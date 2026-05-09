#pragma once

#include <cstdint>
#include <vector>

namespace nac
{

// Hex / ASCII viewer over the //e's main + aux 64 KiB banks. Reads
// straight from MemGetBankPtr (flushed canonical pointers — same
// access path the RAM snapshot uses) so values match what the panels
// see. 16 bytes per row, jump-to-address + bank toggle in the header.
//
// Each byte that changes since the last frame is tinted red and fades
// back to normal over kHighlightSeconds, so writes are easy to spot.
class MemoryViewerPanel
{
public:
    bool* OpenFlag()  { return &m_open; }
    bool& OpenRef()   { return m_open; }
    void  Render();

    static constexpr double kHighlightSeconds = 1.0;

private:
    bool m_open    = false;
    int  m_bank    = 0;       // 0 = main, 1 = aux
    int  m_jumpTo  = 0;       // hex input target

    bool   m_changeInit = false;
    uint8_t m_prev    [2][0x10000] = {};
    double  m_changeAt[2][0x10000] = {};

    // Search state. m_searchInput is the text the user typed; on
    // pressing Search we parse it into m_searchPattern (whitespace-
    // tolerant hex) and scan the active bank for every match,
    // marking each matched byte true in m_highlight. Render tints
    // those bytes' background yellow so the user can watch them
    // change via the existing change-tint overlay. Clear empties
    // both the input and the highlight bitmap.
    char    m_searchInput[128] = {};
    std::vector<uint8_t> m_searchPattern;
    bool    m_highlight[2][0x10000] = {};
    int     m_matchCount = 0;
};

} // namespace nac
