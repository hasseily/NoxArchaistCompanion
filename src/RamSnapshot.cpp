#include "Emulator/StdAfx.h"

#include "RamSnapshot.h"

#include "Emulator/Memory.h"

#include <cstring>

namespace nac
{

RamSnapshot g_ramSnapshot;

void TakeRamSnapshot()
{
    // Copy from the raw 2 × 64 KiB physical banks, NOT from memshadow.
    // memshadow[p] follows the //e's RAMRD / RAMWRT / STORE80 / PAGE2
    // soft switches, so on hires-affected pages it flips between the
    // main and aux backing every time the CPU toggles PAGE2 (which Nox
    // does often — that's the source of the flicker we used to see on
    // $2AF9 mapID etc.).
    //
    // MemGetBankPtr (NOT MemGetMainPtr/MemGetAuxPtr) is the right call
    // here. The plain MemGet*Ptr calls return either the `mem` working
    // cache or the canonical bank depending on shadowing — without
    // flushing first. So a Nox write to $6CEC may live in the cache
    // unflushed when we sample, and reading raw memmain misses it.
    // MemGetBankPtr calls BackMainImage() which copies every dirty
    // page from `mem` back to its shadow target before returning, so
    // memmain/memaux are guaranteed current. This is what the original
    // NAC's RemoteControlManager used (GameLink::Out(MemGetBankPtr(0))).
    const uint8_t* main = reinterpret_cast<const uint8_t*>(MemGetBankPtr(0));
    const uint8_t* aux  = reinterpret_cast<const uint8_t*>(MemGetBankPtr(1));
    if (!main || !aux) return;   // not yet initialised
    std::memcpy(g_ramSnapshot.main, main, sizeof(g_ramSnapshot.main));
    std::memcpy(g_ramSnapshot.aux,  aux,  sizeof(g_ramSnapshot.aux));
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
