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
#include "Emulator/Card.h"
#include "Emulator/Common.h"
#include "Emulator/Core.h"
#include "Emulator/CPU.h"
#include "Emulator/Harddisk.h"
#include "Emulator/Interface.h"
#include "Emulator/Keyboard.h"
#include "Emulator/Memory.h"
#include "Emulator/NTSC.h"
#include "Emulator/RGBMonitor.h"
#include "Emulator/Speaker.h"
#include "Emulator/Video.h"


#include <cstdio>
#include <filesystem>
#include <memory>

namespace
{

constexpr int kWindowWidth  = 800;
constexpr int kWindowHeight = 600;
constexpr uint32_t kSimRamSize = 128 * 1024;

// 1.02 MHz Apple //e clock — used to convert elapsed wall-clock time
// into cycles to feed CpuExecute. Pacing by real time (not by SDL_AppIterate
// invocation count) keeps the //e at native speed regardless of the host
// monitor's refresh rate.
constexpr double kClockHz = 1020484.45;

struct AppState
{
    nac::Renderer         renderer;
    bool                  gamelink_up = false;
    std::filesystem::path hdv_path;          // optional HDV from argv[1]
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

void InitEmulator(const std::filesystem::path& hdvPath)
{
    // NAC has no logo / pause / debug UI; the //e runs continuously from
    // boot. CPU::CpuExecute asserts on MODE_LOGO (its default), so set
    // MODE_RUNNING before any CpuExecute call.
    g_nAppMode = MODE_RUNNING;

    // SDL3 audio: Frame::CreateSoundBuffer hands back an AudioOutput
    // (LinuxSoundBuffer + SDL_AudioStream on the default playback device).
    SpkrInitialize();

    SetApple2Type(A2TYPE_APPLE2EENHANCED);
    RGB_SetVideocard(Video7_SL7, 15, 0);

    Video& video = GetVideo();
    video.SetVideoType(VT_COLOR_IDEALIZED);
    video.SetVideoStyle(VS_COLOR_VERTICAL_BLEND);
    video.SetVideoRefreshRate(VR_60HZ);

    SetCurrentCLK6502();

    // SmartPort HDD card in slot 7. Must be in place *before* MemInitialize
    // so MemInitializeIO wires it up and loads its firmware blob.
    GetCardMgr().Insert(SLOT7, CT_GenericHDD, /*updateRegistry*/ false);

    // Mockingboard in slots 4 & 5 (NAC convention; Nox Archaist drives
    // both for stereo). MockingboardCardManager lazy-inits its SoundBuffer
    // the first time the game touches the chips.
    GetCardMgr().Insert(SLOT4, CT_MockingboardC, /*updateRegistry*/ false);
    GetCardMgr().Insert(SLOT5, CT_MockingboardC, /*updateRegistry*/ false);

    GetFrame().Initialize(true);   // allocates the BGRA framebuffer + Video::Initialize
    MemInitialize();               // loads ROMs + cards' firmware

    // Power-cycle the cards. Without this, the Mockingboard's AY chips
    // never get their initial Reset() and the timer / audio path stays
    // dormant — so games that program MB at boot (Nox Archaist) end up
    // making no sound. Mirrors upstream's ResetMachineState ordering.
    GetCardMgr().Reset(/*powerCycle*/ true);

    if (!hdvPath.empty())
    {
        auto* hdc = static_cast<HarddiskInterfaceCard*>(GetCardMgr().GetObj(SLOT7));
        if (hdc && !hdc->Insert(HARDDISK_1, hdvPath.string()))
        {
            std::fprintf(stderr, "HD_Insert failed for %s\n", hdvPath.string().c_str());
        }
    }

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

SDL_AppResult SDL_AppInit(void** appstate, int argc, char** argv)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
    {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetAppMetadata("Nox Archaist Companion", "0.1.0", "com.asseily.nac");

    nac::Frame::SetResourceDir(FindResourcesDir());

    auto state = std::make_unique<AppState>();
    if (argc >= 2)
        state->hdv_path = argv[1];

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

    InitEmulator(state->hdv_path);

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

    // Pace the CPU by real elapsed time so the emulator runs at //e speed
    // (~1.02 MHz) regardless of the host's vsync rate. Cap per-tick cycles
    // to ~4 frames worth so a paused / debugger-broken host doesn't trigger
    // a long catch-up sprint.
    static uint64_t lastNs = SDL_GetTicksNS();
    const uint64_t  nowNs  = SDL_GetTicksNS();
    const uint64_t  deltaNs = nowNs - lastNs;
    lastNs = nowNs;

    uint32_t cycles = (uint32_t)(deltaNs * kClockHz * 1e-9);
    if (cycles == 0)        cycles = 1;
    if (cycles > 17030 * 4) cycles = 17030 * 4;

    // Mirror upstream's CommonFrame::ExecuteOneFrame: split the run into
    // ~1 ms batches and tick the cards + speaker after each batch, so
    // SpkrUpdate / Mockingboard / SSI263 land samples in their ring
    // buffers at the right rate.
    const uint32_t cyclesPerMs = (uint32_t)(g_fCurrentCLK6502 * 1e-3);
    const UINT     cyclesPerFrame = NTSC_GetCyclesPerFrame();
    uint32_t totalExecuted = 0;
    do
    {
        const uint32_t batch = (cyclesPerMs < cycles - totalExecuted) ? cyclesPerMs : cycles - totalExecuted;
        const uint32_t executed = CpuExecute(batch, /*bVideoUpdate*/ true);
        totalExecuted += executed;

        GetCardMgr().Update(executed);
        SpkrUpdate(executed);

        g_dwCyclesThisFrame = (g_dwCyclesThisFrame + executed) % cyclesPerFrame;
    } while (totalExecuted < cycles);

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
