#include "Emulator/StdAfx.h"

#include "RemoteInput.h"

#include "RemoteControl/Gamelink.h"

#include "Emulator/Keyboard.h"

#include <SDL3/SDL_timer.h>

#include <cstdint>
#include <unordered_set>

namespace nac
{

namespace
{

// DIK (DirectInput Keyboard) scancode -> Win32 VK code. Verbatim from the
// old RemoteControlManager.cpp aDIKtoVK[] — Grid Cartographer writes
// scancodes in this layout.
constexpr uint8_t kDikToVK[256] = {
    0x00, 0x1B, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0xBD, 0xBB,
    0x08, 0x09, 0x51, 0x57, 0x45, 0x52, 0x54, 0x59, 0x55, 0x49, 0x4F, 0x50, 0xDB, 0xDD, 0x0D,
    0xA2, 0x41, 0x53, 0x44, 0x46, 0x47, 0x48, 0x4A, 0x4B, 0x4C, 0xBA, 0xDE, 0xC0, 0xA0, 0xDC,
    0x5A, 0x58, 0x43, 0x56, 0x42, 0x4E, 0x4D, 0xBC, 0xBE, 0xBF, 0xA1, 0x6A, 0xA4, 0x20, 0x14,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x13, 0x91, 0x24, 0x26, 0x21,
    0x6D, 0x25, 0x0C, 0x27, 0x6B, 0x23, 0x28, 0x22, 0x2D, 0x2E, 0x2C, 0x00, 0xE2, 0x7A, 0x7B,
    0x0C, 0xEE, 0xF1, 0xEA, 0xF9, 0xF5, 0xF3, 0x00, 0x00, 0xFB, 0x2F, 0x7C, 0x7D, 0x7E, 0x7F,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0xED, 0x00, 0xE9, 0x00, 0xC1, 0x00, 0x00, 0x87,
    0x00, 0x00, 0x00, 0x00, 0xEB, 0x09, 0x00, 0xC2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB1, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xB0, 0x00, 0x00, 0x0D, 0xA3, 0x00, 0x00, 0xAD, 0xB6, 0xB3, 0x00,
    0xB2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAE, 0x00, 0xAF, 0x00, 0xB7,
    0x00, 0x00, 0xBF, 0x00, 0x2A, 0xA5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x90, 0x00, 0x24, 0x26, 0x21, 0x00, 0x25, 0x00, 0x27, 0x00, 0x23, 0x28,
    0x22, 0x2D, 0x2E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5B, 0x5C, 0x5D, 0x00, 0x5F,
    0x00, 0x00, 0x00, 0x00, 0x00, 0xAA, 0xAB, 0xA8, 0xA9, 0xA7, 0xA6, 0xAC, 0xB4, 0xB5, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

// VK codes the //e isn't interested in. Most of these were used by the
// old NAC frontend's window/menu shortcuts; for the //e they'd just turn
// into noise.
const std::unordered_set<uint8_t>& ExclusionSet()
{
    static const std::unordered_set<uint8_t> s = {
        VK_PRIOR, VK_NEXT, VK_END, VK_HOME, VK_SELECT, VK_PRINT, VK_EXECUTE,
        VK_SNAPSHOT, VK_INSERT, VK_DELETE, VK_HELP,
        VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F7, VK_F8, VK_F11, VK_F12,
        VK_NUMLOCK, VK_SCROLL, VK_LWIN, VK_RWIN, VK_APPS,
    };
    return s;
}

// Minimum interval between repeats of a held-down key, in milliseconds.
// Same as the old NAC value — first press fires, then no further fires
// until the key has been held this long.
constexpr uint64_t kRepeatIntervalMs = 400;

// Translate a Win32 VK to a (key, isAscii) pair the //e keyboard latch
// expects. Returns key=0 if VK should be dropped.
//
// The //e firmware reads Return / Backspace / Tab / Esc as ASCII control
// chars (via WM_CHAR upstream). Letters and digits already have ASCII =
// VK on Win32, so they go through the ASCII path too. Arrows / Insert /
// Delete / function keys go via the NOT_ASCII path with their VK code.
struct DispatchKey { uint8_t key; bool isAscii; };
DispatchKey VKToDispatch(uint8_t vk)
{
    switch (vk)
    {
    case VK_RETURN:    return { 0x0D, true };
    case VK_BACK:      return { 0x08, true };
    case VK_TAB:       return { 0x09, true };
    case VK_ESCAPE:    return { 0x1B, true };
    default:           break;
    }
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z') || vk == ' ')
        return { vk, true };
    if (vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN)
        return { vk, false };
    return { 0, false };
}

// Last shared-memory keyboard snapshot we acted on.
uint32_t g_prev[8] = {};
// Per-scancode last-fire tick (ms). Zero means "never fired since release".
uint64_t g_lastTickMs[256] = {};

} // namespace

void RemoteInputPump()
{
    GameLink::sSharedMMapInput_R2 in = {};
    GameLink::sSharedMMapAudio_R1 audio = {};
    if (!GameLink::In(&in, &audio))
        return;

    const uint64_t nowMs = SDL_GetTicks();

    for (int blk = 0; blk < 8; ++blk)
    {
        const uint32_t cur  = in.keyb_state[blk];
        const uint32_t prev = g_prev[blk];
        if (!cur && !prev) continue;

        for (int bit = 0; bit < 32; ++bit)
        {
            const uint32_t mask = 1u << bit;
            const bool nowDown = (cur  & mask) != 0;
            const bool wasDown = (prev & mask) != 0;
            if (!nowDown) { if (wasDown) g_lastTickMs[blk * 32 + bit] = 0; continue; }

            const int sc = blk * 32 + bit;
            // Suppress repeats faster than kRepeatIntervalMs; the //e's
            // keyboard latch can only hold one strobe so blasting it on
            // every iterate would just produce a held-down stutter.
            if (wasDown && (nowMs - g_lastTickMs[sc]) < kRepeatIntervalMs)
                continue;
            g_lastTickMs[sc] = nowMs;

            const uint8_t vk = kDikToVK[sc];
            if (vk == 0 || ExclusionSet().count(vk)) continue;

            const DispatchKey d = VKToDispatch(vk);
            if (d.key == 0) continue;

            KeybUpdateCtrlShiftStatus();
            KeybQueueKeypress(d.key, d.isAscii ? ASCII : NOT_ASCII);
        }

        g_prev[blk] = cur;
    }
}

} // namespace nac
