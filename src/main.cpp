// Phase 5: SDL3 main loop drives the Apple //e core. Each iteration runs
// roughly one frame of CPU cycles, the emulator writes its BGRA framebuffer,
// we upload it to a GL texture and draw a full-screen quad. No audio, no
// sidebar, no input wiring yet — that's Phase 6+.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

// StdAfx must precede the emulator headers — they rely on its Windows-types
// + STL pulldown for BYTE/WORD/HWND etc.
#include "Emulator/StdAfx.h"

#include "Renderer.h"
#include "Frame.h"
#include "RemoteControl/Gamelink.h"

#include "Emulator/CardManager.h"
#include "Emulator/Common.h"
#include "Emulator/Core.h"
#include "Emulator/CPU.h"
#include "Emulator/Interface.h"
#include "Emulator/Memory.h"
#include "Emulator/NTSC.h"
#include "Emulator/RGBMonitor.h"
#include "Emulator/Video.h"

#include <cstdio>
#include <filesystem>
#include <memory>

namespace
{

constexpr int kWindowWidth  = 800;
constexpr int kWindowHeight = 600;
constexpr uint32_t kSimRamSize = 128 * 1024;

// One frame of //e cycles at 1.0205 MHz / 60 Hz. The emulator's own
// CpuCalcCycles loop will refine this with timing feedback.
constexpr uint32_t kCyclesPerFrame = 17030;

struct AppState
{
    nac::Renderer renderer;
    bool          gamelink_up = false;
};

// Walk up from the executable directory looking for Resources/. Lets the
// app run both from build-win/ and from a packaged install.
std::filesystem::path FindResourcesDir()
{
    // SDL3 returns a process-owned string — do NOT free.
    const char* base = SDL_GetBasePath();
    std::filesystem::path dir = base ? base : ".";

    for (int i = 0; i < 5; ++i)
    {
        const auto candidate = dir / "Resources";
        if (std::filesystem::exists(candidate / "Apple2e_Enhanced.rom"))
            return candidate;
        const auto nacCandidate = dir / "NoxArchaistCompanion" / "Emulator" / "Resources";
        if (std::filesystem::exists(nacCandidate / "Apple2e_Enhanced.rom"))
            return nacCandidate;
        dir = dir.parent_path();
        if (dir.empty()) break;
    }
    return "Resources";
}

void InitEmulator()
{
    SetApple2Type(A2TYPE_APPLE2EENHANCED);
    RGB_SetVideocard(Video7_SL7, 15, 0);

    Video& video = GetVideo();
    video.SetVideoType(VT_COLOR_IDEALIZED);
    video.SetVideoStyle(VS_COLOR_VERTICAL_BLEND);
    video.SetVideoRefreshRate(VR_60HZ);

    SetCurrentCLK6502();

    GetFrame().Initialize(true);   // allocates the BGRA framebuffer + Video::Initialize
    MemInitialize();               // loads ROMs + sets up cards
    GetFrame().VideoRedrawScreen();
}

void ShutdownEmulator()
{
    MemDestroy();
    GetFrame().Destroy();
}

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char** /*argv*/)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetAppMetadata("Nox Archaist Companion", "0.1.0", "com.asseily.nac");

    nac::Frame::SetResourceDir(FindResourcesDir());

    auto state = std::make_unique<AppState>();
    if (!state->renderer.Init("Nox Archaist Companion", kWindowWidth, kWindowHeight))
    {
        return SDL_APP_FAILURE;
    }

    if (GameLink::Init(false) && GameLink::AllocRAM(kSimRamSize))
    {
        GameLink::SetProgramInfo("Nox Archaist Companion", 0, 0, 0, 0);
        state->gamelink_up = true;
    }

    InitEmulator();

    *appstate = state.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* /*appstate*/, SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto* state = static_cast<AppState*>(appstate);

    CpuExecute(kCyclesPerFrame, /*bVideoUpdate*/ true);
    GetFrame().VideoRedrawScreen();

    Video& video = GetVideo();
    state->renderer.BeginFrame();
    state->renderer.UploadFramebuffer(video.GetFrameBuffer(),
                                      static_cast<int>(video.GetFrameBufferWidth()),
                                      static_cast<int>(video.GetFrameBufferHeight()));
    state->renderer.DrawFramebuffer();
    state->renderer.EndFrame();

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* state = static_cast<AppState*>(appstate);
    if (state)
    {
        ShutdownEmulator();
        if (state->gamelink_up) GameLink::Term();
        state->renderer.Shutdown();
        delete state;
    }
    SDL_Quit();
}
