// Phase 3 stub. Brings Gamelink up, allocates the shared-memory region, then
// tears it back down so a Linux build green is provable end-to-end.
// Replaced in Phase 4 by the SDL3 platform layer.

#include "RemoteControl/Gamelink.h"

#include <cstdint>
#include <cstdio>

int main(int /*argc*/, char** /*argv*/)
{
    if (!GameLink::Init(false))
    {
        std::fprintf(stderr, "Gamelink: Init failed\n");
        return 1;
    }

    constexpr uint32_t kSimRamSize = 128 * 1024;
    if (!GameLink::AllocRAM(kSimRamSize))
    {
        std::fprintf(stderr, "Gamelink: AllocRAM failed\n");
        GameLink::Term();
        return 2;
    }

    GameLink::SetProgramInfo("Nox Archaist Companion (Phase 3 stub)", 0, 0, 0, 0);
    std::printf("Gamelink ready (%u bytes RAM); shutting down.\n", kSimRamSize);

    GameLink::Term();
    return 0;
}
