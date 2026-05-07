#pragma once

#include <cstdint>

namespace nac
{

// 128 KiB latched snapshot of the //e's underlying main + aux backing
// stores (NOT the soft-switch-mapped CPU-visible space — see
// RamSnapshot.cpp for why). Taken at PC_PRINTSTR so multi-byte
// writes have settled. Panels read from here so:
//   * RAMRD / RAMWRT / STORE80 / PAGE2 toggles can't flip the bank
//     out from under the read (the source of the visible flicker on
//     hires-affected pages like $29 / $2A where mapID lives),
//   * mid-update fields (xpos/ypos, party stats) never appear half-
//     written.
struct RamSnapshot
{
    uint8_t main[0x10000];   // soft-switch-resolved CPU-visible main bank
    uint8_t aux [0x10000];   // raw aux bank
    bool    valid = false;   // false until the first snapshot lands
};

extern RamSnapshot g_ramSnapshot;

// Invoked from CPU::Fetch via g_noxSampleCallback. memcpys the active
// memshadow pages and the aux bank into g_ramSnapshot. Cheap — single-
// digit microseconds. No-op if the emulator isn't initialised.
void TakeRamSnapshot();

// One-byte read from the snapshot. addr < 0x10000 hits main; 0x10000-
// 0x1FFFF hits aux. Returns 0 if the snapshot isn't valid yet.
uint8_t SnapshotPeek(uint32_t addr);

} // namespace nac
