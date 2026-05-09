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

    // Search state. Search scans the active bank for the typed hex
    // pattern and stores each match's start offset in m_matches[bank].
    // Refine takes the new pattern and keeps only matches where the
    // bytes at the existing match offsets equal the new pattern —
    // classic memory-scanner narrowing. m_highlight is regenerated
    // from m_matches[bank] + m_patternLen[bank] each time.
    // m_filterRows hides rows with no highlighted bytes.
    char    m_searchInput[128] = {};
    std::vector<int>  m_matches[2];
    int     m_patternLen[2] = {};
    bool    m_highlight[2][0x10000] = {};
    bool    m_filterRows = false;
};

} // namespace nac
