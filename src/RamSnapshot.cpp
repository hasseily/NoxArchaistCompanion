#include "Emulator/StdAfx.h"

#include "RamSnapshot.h"

#include "Emulator/Memory.h"

#include <cstring>

namespace nac
{

RamSnapshot g_ramSnapshot;

void TakeRamSnapshot()
{
    // Walk the 256 CPU-visible pages; memshadow[p] is whatever bank
    // the soft switches currently route page p to. A page can briefly
    // be null during init / reset — skip and try again next call.
    for (int p = 0; p < 256; ++p)
    {
        const uint8_t* page = memshadow[p];
        if (!page) return;
        std::memcpy(g_ramSnapshot.main + p * 0x100, page, 0x100);
    }
    const uint8_t* aux = reinterpret_cast<const uint8_t*>(MemGetAuxPtr(0));
    if (!aux) return;
    std::memcpy(g_ramSnapshot.aux, aux, sizeof(g_ramSnapshot.aux));
    g_ramSnapshot.valid = true;
}

uint8_t SnapshotPeek(uint32_t addr)
{
    if (!g_ramSnapshot.valid) return 0;
    if (addr < 0x10000u)  return g_ramSnapshot.main[addr];
    if (addr < 0x20000u)  return g_ramSnapshot.aux [addr - 0x10000u];
    return 0;
}

} // namespace nac
