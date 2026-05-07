#pragma once

#include <cstdint>

namespace nac
{

// 128 KiB latched snapshot of CPU-visible RAM, taken at PC_PRINTSTR
// (Nox's print routine entry — after all game-state writes have
// landed). Panels read from here instead of live memshadow / memaux
// so non-atomic multi-byte updates (xpos/ypos pairs, party stats,
// sidebar buffers) can't show as flicker.
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
