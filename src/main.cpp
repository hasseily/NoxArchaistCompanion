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
#include "Emulator/Keyboard.h"
#include "Emulator/Memory.h"
#include "Emulator/NTSC.h"
#include "Emulator/RGBMonitor.h"
#include "Emulator/Speaker.h"
#include "Emulator/Video.h"

extern bool g_bDisableDirectSound;
extern bool g_bDisableDirectSoundMockingboard;

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
    // NAC has no logo / pause / debug UI; the //e runs continuously from
    // boot. CPU::CpuExecute asserts on MODE_LOGO (its default), so set
    // MODE_RUNNING before any CpuExecute call.
    g_nAppMode = MODE_RUNNING;

    // No audio yet — SpkrInitialize still has to run though, otherwise
    // g_pSpeakerBuffer stays NULL and the //e ROM's BELL routine
    // ($FBE4..$FBEF, which toggles $C030) crashes inside SpkrToggle ->
    // UpdateSpkr. The g_bDisableDirectSound flag tells SpkrInitialize to
    // mute the voice instead of talking to DirectSound.
    g_bDisableDirectSound = true;
    g_bDisableDirectSoundMockingboard = true;
    SpkrInitialize();

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

    // //e Enhanced ships with Caps Lock UP — lowercase letters pass
    // through, Shift produces uppercase. The emulator default of true
    // forces every letter uppercase, which surprises modern users.
    KeybSetCapsLock(false);
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

    SDL_StartTextInput(state->renderer.Window());

    if (GameLink::Init(false) && GameLink::AllocRAM(kSimRamSize))
    {
        GameLink::SetProgramInfo("Nox Archaist Companion", 0, 0, 0, 0);
        state->gamelink_up = true;
    }

    InitEmulator();

    *appstate = state.release();
    return SDL_APP_CONTINUE;
}

namespace
{

// Control chars the //e firmware reads as ASCII (Win32 routes these via
// WM_CHAR, not WM_KEYDOWN). SDL3 SDL_EVENT_TEXT_INPUT doesn't fire for
// control chars, so we send them from KEY_DOWN through the ASCII path.
BYTE SdlKeyToAscii(SDL_Keycode k)
{
    switch (k)
    {
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0D;
    case SDLK_BACKSPACE:                  return 0x08;
    case SDLK_TAB:                        return 0x09;
    case SDLK_ESCAPE:                     return 0x1B;
    default:                              return 0;
    }
}

// Map SDL3 keycodes for non-printable keys to the Win32 VK_* codes that
// KeybQueueKeypress(..., NOT_ASCII) expects.
WPARAM SdlKeyToVK(SDL_Keycode k)
{
    switch (k)
    {
    case SDLK_LEFT:                           return VK_LEFT;
    case SDLK_RIGHT:                          return VK_RIGHT;
    case SDLK_UP:                             return VK_UP;
    case SDLK_DOWN:                           return VK_DOWN;
    case SDLK_DELETE:                         return VK_DELETE;
    case SDLK_INSERT:                         return VK_INSERT;
    case SDLK_HOME:                           return VK_HOME;
    case SDLK_END:                            return VK_END;
    case SDLK_PAGEUP:                         return VK_PRIOR;
    case SDLK_PAGEDOWN:                       return VK_NEXT;
    case SDLK_F1:  return VK_F1;  case SDLK_F2:  return VK_F2;
    case SDLK_F3:  return VK_F3;  case SDLK_F4:  return VK_F4;
    case SDLK_F5:  return VK_F5;  case SDLK_F6:  return VK_F6;
    case SDLK_F7:  return VK_F7;  case SDLK_F8:  return VK_F8;
    case SDLK_F9:  return VK_F9;  case SDLK_F10: return VK_F10;
    case SDLK_F11: return VK_F11; case SDLK_F12: return VK_F12;
    default:                                  return 0;
    }
}

} // namespace

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    auto* state = static_cast<AppState*>(appstate);
    (void)state;

    switch (event->type)
    {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN:
    {
        // Alt+F4 quits.
        if (event->key.key == SDLK_F4 && (event->key.mod & SDL_KMOD_ALT))
            return SDL_APP_SUCCESS;

        // Caps Lock toggles the //e's emulated caps state.
        if (event->key.key == SDLK_CAPSLOCK)
        {
            KeybToggleCapsLock();
            break;
        }

        KeybUpdateCtrlShiftStatus();

        // Return / Backspace / Tab / Esc come in via the ASCII path —
        // that's how the //e firmware reads them.
        if (BYTE ascii = SdlKeyToAscii(event->key.key))
        {
            KeybQueueKeypress(ascii, ASCII);
            break;
        }

        // Arrows / Insert / Delete / F-keys go through the VK path.
        if (WPARAM vk = SdlKeyToVK(event->key.key))
        {
            KeybQueueKeypress(vk, NOT_ASCII);
        }
        break;
    }

    case SDL_EVENT_TEXT_INPUT:
    {
        for (const char* p = event->text.text; p && *p; ++p)
        {
            const unsigned char c = static_cast<unsigned char>(*p);
            if (c < 0x80)
                KeybQueueKeypress(c, ASCII);
        }
        break;
    }

    default:
        break;
    }
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
