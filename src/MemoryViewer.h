#pragma once

#include <cstdint>

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

    // Per-bank previous-frame snapshot + last-change timestamps for
    // the change-tint overlay. Only allocated on first open.
    bool   m_changeInit = false;
    uint8_t m_prev    [2][0x10000] = {};
    double  m_changeAt[2][0x10000] = {};
};

} // namespace nac
