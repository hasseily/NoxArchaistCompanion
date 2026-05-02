// Phase 4: SDL3 main loop + OpenGL context. Opens a black window, pumps
// events, shuts down cleanly. Phase 5 wires the Apple //e framebuffer into
// the GL texture path; Phase 6+ adds the post-processor and ImGui overlays.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "Renderer.h"
#include "RemoteControl/Gamelink.h"

#include <cstdio>
#include <memory>

namespace
{

constexpr int kWindowWidth  = 800;
constexpr int kWindowHeight = 600;

// 128 KiB matches the Apple //e main+aux footprint that Memory.cpp will
// allocate in Phase 5. Sized here so Gamelink's shared-memory region is
// the right shape from the start.
constexpr uint32_t kSimRamSize = 128 * 1024;

struct AppState
{
    nac::Renderer renderer;
    bool          gamelink_up = false;
};

} // namespace

SDL_AppResult SDL_AppInit(void** appstate, int /*argc*/, char** /*argv*/)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetAppMetadata("Nox Archaist Companion", "0.1.0", "com.asseily.nac");

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
    else
    {
        std::fprintf(stderr, "Gamelink init failed; continuing without it\n");
    }

    *appstate = state.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    (void)appstate;
    if (event->type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
    {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    auto* state = static_cast<AppState*>(appstate);
    state->renderer.BeginFrame();
    state->renderer.EndFrame();
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* state = static_cast<AppState*>(appstate);
    if (state)
    {
        if (state->gamelink_up)
        {
            GameLink::Term();
        }
        state->renderer.Shutdown();
        delete state;
    }
    SDL_Quit();
}
