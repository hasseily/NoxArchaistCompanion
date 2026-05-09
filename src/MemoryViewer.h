#pragma once

#include <cstdint>

namespace nac
{

// Hex / ASCII viewer over the //e's main + aux 64 KiB banks. Reads
// straight from MemGetBankPtr (flushed canonical pointers — same
// access path the RAM snapshot uses) so values match what the panels
// see. 16 bytes per row, jump-to-address + bank toggle in the header.
class MemoryViewerPanel
{
public:
    bool* OpenFlag()  { return &m_open; }
    bool& OpenRef()   { return m_open; }
    void  Render();

private:
    bool m_open    = false;
    int  m_bank    = 0;       // 0 = main, 1 = aux
    int  m_jumpTo  = 0;       // hex input target
};

} // namespace nac
