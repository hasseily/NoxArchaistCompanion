// Phase 5: SDL3 main loop drives the Apple //e core. Each iteration runs
// roughly one frame of CPU cycles, the emulator writes its BGRA framebuffer,
// we upload it to a GL texture and draw a full-screen quad. No audio, no
// sidebar, no input wiring yet — that's Phase 6+.

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>

// StdAfx must precede the emulator headers — they rely on its Windows-types
// + STL pulldown for BYTE/WORD/HWND etc.
#include "Emulator/StdAfx.h"

#include "Renderer.h"
#include "Frame.h"
#include "Sidebar.h"
#include "RemoteInput.h"
#include "NoxHacks.h"
#include "RemoteControl/Gamelink.h"

#include "pp/postprocessor.h"
#include "pp/shader.h"

#include "Emulator/CardManager.h"
#include "Emulator/Card.h"
#include "Emulator/Common.h"
#include "Emulator/Core.h"
#include "Emulator/CPU.h"
#include "Emulator/Harddisk.h"
#include "Emulator/Interface.h"
#include "Emulator/Keyboard.h"
#include "Emulator/Memory.h"
#include "Emulator/Mockingboard.h"
#include "Emulator/MockingboardCardManager.h"
#include "Emulator/NTSC.h"
#include "Emulator/RGBMonitor.h"
#include "Emulator/Speaker.h"
#include "Emulator/SoundCore.h"
#include "Emulator/Video.h"

#include <SDL3/SDL_dialog.h>


#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace
{

constexpr int kWindowWidth  = 800;
constexpr int kWindowHeight = 600;

// Base //e clock; the speed preset multiplies this. Wall-clock pacing
// in SDL_AppIterate converts elapsed time into cycles at the active
// rate so display + game logic stay in sync regardless of host vsync.
constexpr double kBaseClockHz = 1020484.45;

// Speed presets, in CPU cycles/second multiples of the base 1.02 MHz.
// Indices match the "Speed" submenu order.
struct SpeedPreset { const char* label; double cyclesPerSecond; };
constexpr SpeedPreset kSpeedPresets[] = {
    { "Sloth (0.5x)",     510242.225 },
    { "Retro (1x)",      1020484.45  },
    { "Turbo (2x)",      2040968.9   },
    { "Ford GT (4x)",    4081937.8   },
    { "MiG-31 (6x)",     6122906.7   },
    { "Pro (8x)",        8163875.6   },
    { "Demigod (max)",   2.0e8       },
};
constexpr int kSpeedDefault = 1;   // Retro 1x

// Color video preset → VideoType. Picked from the Color submenu.
struct VideoPreset { const char* label; VideoType_e type; };
constexpr VideoPreset kVideoPresets[] = {
    { "Idealized",          VT_COLOR_IDEALIZED },
    { "Text-Optimized RGB", VT_COLOR_VIDEOCARD_RGB },
    { "Composite Monitor",  VT_COLOR_MONITOR_NTSC },
    { "TV Screen",          VT_COLOR_TV },
};

// Monochrome submenu — 0 means "off, use the color preset". Otherwise the
// VT_MONO_* type overrides the color preset until the user picks Off again.
struct MonoPreset { const char* label; VideoType_e type; };
constexpr MonoPreset kMonoPresets[] = {
    { "Off",   VT_COLOR_IDEALIZED },   // type unused when mono is off
    { "White", VT_MONO_WHITE },
    { "Amber", VT_MONO_AMBER },
    { "Green", VT_MONO_GREEN },
};

// Old NAC's "off / soft / medium / loud / extreme" 5-step volume scale.
// Values are *attenuations* (0=loudest, kVolumeMax=silent) — matches the
// upstream NewVolume() / DirectSound semantics that SpkrSetVolume calls
// through to.
constexpr uint32_t kVolumeMax = 99;
constexpr const char* kVolumeLabels[] = { "Off", "Soft", "Medium", "Loud", "Extreme" };
constexpr uint32_t kSpkrAtten[5] = { kVolumeMax, kVolumeMax*40/100, kVolumeMax*30/100, kVolumeMax*25/100, 5 };
constexpr uint32_t kMBAtten[5]   = { kVolumeMax, kVolumeMax*35/100, kVolumeMax*25/100, kVolumeMax*15/100, 0 };

struct AppState
{
    nac::Renderer         renderer;
    nac::Sidebar          sidebar;
    nac::CombatLogPanel   combatLog;        // installs the Fetch-trap callback
    nac::HackPanel        hackPanel;
    bool                  gamelink_up      = false;
    bool                  apple_open       = true;
    bool                  pp_enabled       = true;
    bool                  paused           = false;
    bool                  gamelink_enabled = true;       // user-facing toggle (vs gamelink_up which means GameLink::Init succeeded)
    int                   speed_idx        = kSpeedDefault;
    int                   video_idx        = 0;          // 0..3 color preset
    int                   mono_idx         = 0;          // 0=off, 1=white, 2=amber, 3=green
    int                   vol_speaker      = 3;          // 0..4 (Loud)
    int                   vol_mb           = 3;
    int                   window_w         = 0;          // 0 = use default
    int                   window_h         = 0;
    std::string           imgui_ini_path;               // outlives ImGui's IO struct
    std::filesystem::path pref_dir;                     // SDL pref dir
    std::filesystem::path hdv_path;                     // current HDV (argv[1] or last opened)
};

// Persisted NAC-side settings (post-processor state has its own file).
// Stored as JSON next to imgui.ini under SDL_GetPrefPath().
constexpr const char* kSettingsFilename = "nac.json";
constexpr const char* kPpStateFilename  = "pp_state.json";

// Read just the host window size from nac.json so we can pass it to
// Renderer::Init before the GL context exists. The full LoadSettings()
// runs later (after PP is up) and re-reads the same file.
void LoadWindowSizeOnly(AppState& s)
{
    if (s.pref_dir.empty()) return;
    const auto path = s.pref_dir / kSettingsFilename;
    if (!std::filesystem::exists(path)) return;
    try
    {
        std::ifstream f(path);
        nlohmann::json j; f >> j;
        s.window_w = j.value("window_w", 0);
        s.window_h = j.value("window_h", 0);
    }
    catch (...) {}
}

void SaveSettings(const AppState& s)
{
    if (s.pref_dir.empty()) return;
    nlohmann::json j;
    int curW = 0, curH = 0;
    if (s.renderer.Window())
        SDL_GetWindowSize(s.renderer.Window(), &curW, &curH);
    j["window_w"]            = curW > 0 ? curW : s.window_w;
    j["window_h"]            = curH > 0 ? curH : s.window_h;
    j["apple_open"]          = s.apple_open;
    j["pp_enabled"]          = s.pp_enabled;
    j["paused"]              = s.paused;
    j["gamelink_enabled"]    = s.gamelink_enabled;
    j["speed_idx"]           = s.speed_idx;
    j["video_idx"]           = s.video_idx;
    j["mono_idx"]            = s.mono_idx;
    j["vol_speaker"]         = s.vol_speaker;
    j["vol_mb"]              = s.vol_mb;
    j["hdv_path"]            = s.hdv_path.string();
    j["combat_log_open"] = const_cast<nac::CombatLogPanel&>(s.combatLog).OpenRef();
    j["combat_log_auto_scroll"]    = const_cast<nac::CombatLogPanel&>(s.combatLog).AutoScrollRef();
    j["combat_log_include_combat"] = const_cast<nac::CombatLogPanel&>(s.combatLog).IncludeCombatRef();
    j["hack_open"]       = const_cast<nac::HackPanel&>(s.hackPanel).OpenRef();
    j["hack_hex"]        = const_cast<nac::HackPanel&>(s.hackPanel).HexRef();
    j["hack_poke_addr"]  = const_cast<nac::HackPanel&>(s.hackPanel).PokeAddrRef();
    j["pp_settings_open"] = sa2::PostProcessor::GetInstance()->bImguiWindowIsOpen;
    j["sidebar_profile"]  = s.sidebar.ActiveProfileName();

    try
    {
        std::ofstream f(s.pref_dir / kSettingsFilename);
        f << j.dump(2);
    }
    catch (...) {}

    try
    {
        sa2::PostProcessor::GetInstance()->SaveState((s.pref_dir / kPpStateFilename).string());
    }
    catch (...) {}
}

void LoadSettings(AppState& s)
{
    if (s.pref_dir.empty()) return;

    // PP state — restore quietly if file exists.
    const auto ppPath = s.pref_dir / kPpStateFilename;
    if (std::filesystem::exists(ppPath))
    {
        try { sa2::PostProcessor::GetInstance()->LoadState(ppPath.string()); }
        catch (...) {}
    }

    const auto settingsPath = s.pref_dir / kSettingsFilename;
    if (!std::filesystem::exists(settingsPath)) return;
    try
    {
        std::ifstream f(settingsPath);
        nlohmann::json j; f >> j;
        s.apple_open       = j.value("apple_open",       s.apple_open);
        s.pp_enabled       = j.value("pp_enabled",       s.pp_enabled);
        s.gamelink_enabled = j.value("gamelink_enabled", s.gamelink_enabled);
        s.speed_idx   = j.value("speed_idx",   s.speed_idx);
        s.video_idx   = j.value("video_idx",   s.video_idx);
        s.mono_idx    = j.value("mono_idx",    s.mono_idx);
        s.vol_speaker = j.value("vol_speaker", s.vol_speaker);
        s.vol_mb      = j.value("vol_mb",      s.vol_mb);
        // Don't restore "paused" — wake up running.
        const std::string lastHdv = j.value("hdv_path", std::string{});
        if (s.hdv_path.empty() && !lastHdv.empty() && std::filesystem::exists(lastHdv))
            s.hdv_path = lastHdv;
        s.combatLog.OpenRef()           = j.value("combat_log_open", false);
        s.combatLog.AutoScrollRef()     = j.value("combat_log_auto_scroll", true);
        s.combatLog.IncludeCombatRef()  = j.value("combat_log_include_combat", false);
        s.combatLog.ApplyIncludeCombat();
        s.hackPanel.OpenRef()      = j.value("hack_open", false);
        s.hackPanel.HexRef()       = j.value("hack_hex",  false);
        s.hackPanel.PokeAddrRef()  = j.value("hack_poke_addr", 0x6CEC);
        sa2::PostProcessor::GetInstance()->bImguiWindowIsOpen =
            j.value("pp_settings_open", false);
        sa2::PostProcessor::GetInstance()->SetActive(s.pp_enabled);
        const std::string profile = j.value("sidebar_profile", std::string{});
        if (!profile.empty()) s.sidebar.SetActiveProfile(profile);
    }
    catch (...) {}
}

// 128 KiB matches the //e main+aux footprint. Sized once and shared between
// the Gamelink shared-memory region and the emulator's memmain/memaux
// (g_externalMemMain).
constexpr uint32_t kEmulatorRamBytes = 128 * 1024;

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

// NAC's sidebar profiles live in NoxArchaistCompanion/Profiles/ in the
// repo. Same walk-up logic as FindResourcesDir.
std::filesystem::path FindProfilesDir()
{
    const char* base = SDL_GetBasePath();
    std::filesystem::path dir = base ? base : ".";

    for (int i = 0; i < 5; ++i)
    {
        const auto candidate = dir / "Profiles";
        if (std::filesystem::is_directory(candidate)) return candidate;
        const auto nacCandidate = dir / "NoxArchaistCompanion" / "Profiles";
        if (std::filesystem::is_directory(nacCandidate)) return nacCandidate;
        dir = dir.parent_path();
        if (dir.empty()) break;
    }
    return "Profiles";
}

// Same walk-up for Assets/Versions.json (used by Nox version detection).
std::filesystem::path FindAssetsDir()
{
    const char* base = SDL_GetBasePath();
    std::filesystem::path dir = base ? base : ".";
    for (int i = 0; i < 5; ++i)
    {
        const auto candidate = dir / "Assets";
        if (std::filesystem::exists(candidate / "Versions.json")) return candidate;
        const auto nacCandidate = dir / "NoxArchaistCompanion" / "Assets";
        if (std::filesystem::exists(nacCandidate / "Versions.json")) return nacCandidate;
        dir = dir.parent_path();
        if (dir.empty()) break;
    }
    return "Assets";
}

// Probe an .hdv file for the Nox Archaist version string by reading the
// few bytes Versions.json says contain it for each candidate. The Apple //e
// stores ASCII with the high bit set, so we strip 0x80 before comparing.
// Returns the matching version key (e.g. "1.3.7") or an empty string.
std::string DetectNoxVersion(const std::filesystem::path& hdvPath,
                             const std::filesystem::path& assetsDir)
{
    const auto versionsJsonPath = assetsDir / "Versions.json";
    std::ifstream jf(versionsJsonPath);
    if (!jf) return {};

    nlohmann::json versions;
    try { jf >> versions; }
    catch (...) { return {}; }

    std::ifstream hdv(hdvPath, std::ios::binary);
    if (!hdv) return {};

    for (auto it = versions.begin(); it != versions.end(); ++it)
    {
        const std::string& key = it.key();
        if (!it.value().is_object() || !it.value().contains("VERSION")) continue;

        try
        {
            const uint32_t off = (uint32_t)std::stoul(
                it.value()["VERSION"].get<std::string>(), nullptr, 0);
            hdv.seekg(off);
            std::string buf(key.size(), '\0');
            hdv.read(buf.data(), (std::streamsize)key.size());
            if (!hdv) continue;
            for (char& c : buf) c &= 0x7F;
            if (buf == key) return key;
        }
        catch (...) {}
    }
    return {};
}

// Pack the digits of a version string into a uint32. Matches the old NAC
// scheme: "1.1.9" -> 0x00010109, "1004" -> 0x01000004 (right-to-left,
// one digit per byte, non-digits skipped).
uint32_t PackVersionDigits(const std::string& s)
{
    uint32_t hash = 0;
    int      ix   = 0;
    for (auto it = s.rbegin(); it != s.rend() && ix < 4; ++it)
    {
        if (*it >= '0' && *it <= '9')
        {
            hash |= ((uint32_t)(*it - '0')) << (8 * ix);
            ++ix;
        }
    }
    return hash;
}

constexpr uint32_t kNoxArchaistSig = 0x58C37F8C;

void ApplyVideoSettings(int videoIdx, int monoIdx)
{
    Video& video = GetVideo();
    const VideoType_e t = (monoIdx > 0) ? kMonoPresets[monoIdx].type
                                        : kVideoPresets[videoIdx].type;
    video.SetVideoType(t);
    // Scan lines are handled by the post-processor, so we keep this style
    // bit clear. VS_COLOR_VERTICAL_BLEND only affects color modes — leaving
    // it on for mono types is harmless.
    video.SetVideoStyle(VS_COLOR_VERTICAL_BLEND);
    // SetVideoType only flips a flag; the NTSC pixel-function tables and
    // RGB videocard palette are still wired to whatever was active at init.
    // VideoReinitialize rebuilds them so the new mode actually takes effect
    // (without this, e.g. switching to VT_COLOR_VIDEOCARD_RGB at runtime
    // keeps the previous renderer's text fringing, and switching to a
    // composite mode after a mono boot stays stuck in mono). Mid-run
    // changes pass false here — matching Win32Frame::ApplyVideoModeChange.
    video.VideoReinitialize(false);
    GetFrame().VideoRedrawScreen();
}

void ApplySpeedSetting(int speedIdx)
{
    g_fCurrentCLK6502 = kSpeedPresets[speedIdx].cyclesPerSecond;
    SpkrReinitialize();
}

void ApplyVolumeSettings(int volSpeaker, int volMb)
{
    SpkrSetVolume(kSpkrAtten[volSpeaker], kVolumeMax);
    GetCardMgr().GetMockingboardCardMgr().SetVolume(kMBAtten[volMb], kVolumeMax);
}

void EmulatorPause(AppState& s, bool paused)
{
    s.paused = paused;
    g_nAppMode = paused ? MODE_PAUSED : MODE_RUNNING;
    if (paused) { Spkr_Mute(); GetCardMgr().GetMockingboardCardMgr().MuteControl(true); }
    else        { Spkr_Unmute(); GetCardMgr().GetMockingboardCardMgr().MuteControl(false); }
    if (s.gamelink_up) GameLink::SetPaused(paused);
}

void EmulatorReboot()
{
    // Mirrors the old NAC EmulatorReboot. Power-cycle reset of the //e:
    // memory + CPU + video state + cards + sound. Order matters — Mem
    // before Card so MemReset re-initialises CpuInitialize first.
    g_nAppMode = MODE_RUNNING;
    g_bFullSpeed = false;
    MemReset();
    GetVideo().VideoResetState();
    KeybReset();
    GetCardMgr().Reset(/*powerCycle*/ true);
    SpkrReset();
    SetActiveCpu(GetMainCpu());
    GetFrame().VideoRedrawScreen();
}

// Defined further below — forward-declare so InitEmulator can pre-flight.
void LoadHDV(AppState& state, const std::filesystem::path& path);

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

    // Mockingboard in slot 4 only — Nox Archaist's MB detection probes
    // a single slot and gets confused by a second card. The MB class's
    // SoundBuffer is lazy-inited the first time the game touches the chips.
    GetCardMgr().Insert(SLOT4, CT_MockingboardC, /*updateRegistry*/ false);

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
        else if (hdc)
        {
            // Detect the Nox Archaist version from the HDV bytes so the
            // SetProgramInfo handshake with Grid Cartographer carries
            // the right game sig. GC displays the four hash slots in
            // reverse, so sig goes in program_hash[3] (i4) to render as
            // "58C37F8C:0:0:0". The version string isn't published right
            // now — DetectNoxVersion() returns it for any future consumer
            // (window title, sidebar header) that wants it.
            // The Gamelink program name is the canonical id GC matches
            // against in its profile picker. For Nox Archaist that's
            // "NOXARCHAIST" (the same string the sidebar profiles use in
            // meta.name); fall back to the HDV stem for unknown games.
            const auto        assetsDir = FindAssetsDir();
            const std::string version   = DetectNoxVersion(hdvPath, assetsDir);
            const uint32_t    sig       = version.empty() ? 0u : kNoxArchaistSig;
            const std::string name      = version.empty()
                                             ? hdvPath.stem().string()
                                             : std::string("NOXARCHAIST");
            GameLink::SetProgramInfo(name, 0, 0, 0, sig);
            std::fprintf(stderr, "Gamelink: program=\"%s\" version=\"%s\" sig=0x%08x\n",
                         name.c_str(), version.c_str(), sig);

            // Populate noxcpuconstants for the combat-log Fetch trap and
            // the hack panel's party-stats peek/poke. Quietly no-ops for
            // non-Nox HDVs (version is empty, callback gated off).
            if (nac::LoadNoxConstants(assetsDir, version))
                std::fprintf(stderr, "Nox v%s: cpuconstants loaded\n", version.c_str());
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

// Mid-session HDV change — unload anything currently inserted, load the
// new image, refresh the program-info handshake + cpuconstants, then
// reboot the //e so the game boots cleanly.
void LoadHDV(AppState& state, const std::filesystem::path& path)
{
    auto* hdc = static_cast<HarddiskInterfaceCard*>(GetCardMgr().GetObj(SLOT7));
    if (!hdc) return;
    hdc->Unplug(HARDDISK_1);
    if (!hdc->Insert(HARDDISK_1, path.string()))
    {
        std::fprintf(stderr, "HD_Insert failed for %s\n", path.string().c_str());
        return;
    }
    state.hdv_path = path;

    const auto        assetsDir = FindAssetsDir();
    const std::string version   = DetectNoxVersion(path, assetsDir);
    const uint32_t    sig       = version.empty() ? 0u : kNoxArchaistSig;
    const std::string name      = version.empty() ? path.stem().string()
                                                  : std::string("NOXARCHAIST");
    GameLink::SetProgramInfo(name, 0, 0, 0, sig);
    nac::LoadNoxConstants(assetsDir, version);
    EmulatorReboot();
}

void SDLCALL HdvDialogCallback(void* userdata, const char* const* filelist, int /*filter*/)
{
    auto* state = static_cast<AppState*>(userdata);
    if (!filelist || !filelist[0]) return;   // user cancelled
    LoadHDV(*state, std::filesystem::path(filelist[0]));
}

void OpenHdvDialog(AppState& state)
{
    static const SDL_DialogFileFilter kFilters[] = {
        { "Hard disk image (*.hdv)", "hdv" },
        { "All files",                "*"   },
    };
    SDL_ShowOpenFileDialog(&HdvDialogCallback, &state, state.renderer.Window(),
                           kFilters, (int)(sizeof(kFilters)/sizeof(kFilters[0])),
                           state.hdv_path.empty() ? nullptr
                                                  : state.hdv_path.parent_path().string().c_str(),
                           /*allow_many*/ false);
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

    // Need the pref-dir BEFORE the renderer comes up so we can restore
    // the saved host-window size. Set imgui_ini_path here too.
    if (const char* pref = SDL_GetPrefPath("Asseily", "NoxArchaistCompanion"))
    {
        state->pref_dir       = pref;
        state->imgui_ini_path = std::string(pref) + "imgui.ini";
        SDL_free((void*)pref);
    }
    LoadWindowSizeOnly(*state);

    const int initW = state->window_w > 0 ? state->window_w : kWindowWidth;
    const int initH = state->window_h > 0 ? state->window_h : kWindowHeight;
    if (!state->renderer.Init("Nox Archaist Companion", initW, initH))
    {
        return SDL_APP_FAILURE;
    }

    // Bring up glad against the GL context the renderer just made so the
    // post-processor can call core-profile entry points.
    if (!PP_InitGL(reinterpret_cast<PP_GL_GetProcAddr>(SDL_GL_GetProcAddress)))
    {
        std::fprintf(stderr, "PP_InitGL failed\n");
        return SDL_APP_FAILURE;
    }
    sa2::PostProcessor::GetInstance()->SetActive(true);

    // ImGui's IO struct only exists after Renderer::Init() created the
    // GL context — point it at the same imgui.ini we resolved earlier.
    if (!state->imgui_ini_path.empty())
        ImGui::GetIO().IniFilename = state->imgui_ini_path.c_str();

    SDL_StartTextInput(state->renderer.Window());

    if (GameLink::Init(false))
    {
        if (uint8_t* ram = GameLink::AllocRAM(kEmulatorRamBytes))
        {
            // Hand the Gamelink shared-memory region to the emulator so
            // memmain[0..0xFFFF] and memaux[0..0xFFFF] (which live at
            // ram + 0x10000) become visible to Grid Cartographer in
            // real-time, without copying.
            g_externalMemMain = ram;
            GameLink::SetProgramInfo("Nox Archaist Companion", 0, 0, 0, 0);
            state->gamelink_up = true;
        }
        else
        {
            GameLink::Term();
        }
    }

    state->sidebar.LoadProfilesFromDir(FindProfilesDir());
    // Don't pick a default — every Nox version has a profile with the same
    // "NOXARCHAIST" meta-name and reading the wrong one against live RAM
    // produces garbage. Settings load picks up last session's choice.

    // LoadSettings before InitEmulator so we can carry forward the saved
    // hdv_path and audio volumes into the first init.
    LoadSettings(*state);
    InitEmulator(state->hdv_path);
    ApplyVideoSettings(state->video_idx, state->mono_idx);
    ApplyVolumeSettings(state->vol_speaker, state->vol_mb);

    *appstate = state.release();
    return SDL_APP_CONTINUE;
}

namespace
{

// Control chars the //e firmware reads as ASCII (Win32 routes these via
// WM_CHAR, not WM_KEYDOWN). SDL3 SDL_EVENT_TEXT_INPUT doesn't fire for
// control chars, so we send them from KEY_DOWN through the ASCII path.
//
// Also handles printable ASCII (letters / digits / common punctuation):
// ImGui's SDL3 backend calls SDL_StopTextInput every frame when no
// ImGui text widget is focused, which kills TEXT_INPUT events for the
// //e. We re-derive the character from KEY_DOWN + Shift mod state so
// typing keeps working regardless of ImGui's text-input toggling.
BYTE SdlKeyToAscii(SDL_Keycode k, SDL_Keymod mod)
{
    switch (k)
    {
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0D;
    case SDLK_BACKSPACE:                  return 0x08;
    case SDLK_TAB:                        return 0x09;
    case SDLK_ESCAPE:                     return 0x1B;
    case SDLK_SPACE:                      return ' ';
    default:                              break;
    }

    if (k >= 'a' && k <= 'z')
        return (mod & SDL_KMOD_SHIFT) ? (BYTE)(k - 'a' + 'A') : (BYTE)k;

    // Digits and basic ASCII punctuation pass through. Shift-modified
    // symbols (e.g. ! @ # ...) follow a US-layout convention; for
    // non-US keyboards the TEXT_INPUT path (when not stomped) catches
    // the right character.
    if (k >= '0' && k <= '9')
    {
        if (mod & SDL_KMOD_SHIFT)
        {
            static const char shifted[] = ")!@#$%^&*(";   // 0..9
            return (BYTE)shifted[k - '0'];
        }
        return (BYTE)k;
    }
    return 0;
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

    ImGui_ImplSDL3_ProcessEvent(event);

    // Always forward keystrokes to the //e — except when an ImGui text
    // input widget actually has focus (e.g. a peek/poke address field).
    // WantTextInput is true only for active text editors, unlike
    // WantCaptureKeyboard which is also true whenever any ImGui window
    // is hovered or has nav focus.
    const ImGuiIO& io = ImGui::GetIO();
    if (event->type == SDL_EVENT_KEY_DOWN  && io.WantTextInput) return SDL_APP_CONTINUE;
    if (event->type == SDL_EVENT_TEXT_INPUT && io.WantTextInput) return SDL_APP_CONTINUE;

    switch (event->type)
    {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;

    case SDL_EVENT_KEY_DOWN:
    {
        // Alt+F4 quits.
        if (event->key.key == SDLK_F4 && (event->key.mod & SDL_KMOD_ALT))
            return SDL_APP_SUCCESS;

        // Ctrl+P toggles pause; Alt+R reboots.
        if (event->key.key == SDLK_P && (event->key.mod & SDL_KMOD_CTRL))
        {
            EmulatorPause(*state, !state->paused);
            break;
        }
        if (event->key.key == SDLK_R && (event->key.mod & SDL_KMOD_ALT))
        {
            EmulatorReboot();
            break;
        }

        // Caps Lock toggles the //e's emulated caps state.
        if (event->key.key == SDLK_CAPSLOCK)
        {
            KeybToggleCapsLock();
            break;
        }

        KeybUpdateCtrlShiftStatus();

        // Return / Backspace / Tab / Esc + printable ASCII go through
        // the ASCII path. We derive from KEY_DOWN + Shift mod so this
        // works even when ImGui's backend has stopped TEXT_INPUT.
        if (BYTE ascii = SdlKeyToAscii(event->key.key, event->key.mod))
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
        // Intentionally ignored — KEY_DOWN above already covers
        // letters/digits/punctuation. Routing TEXT_INPUT here too would
        // double-fire each keystroke into the //e keyboard latch.
        break;

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

    // While paused, skip CPU + audio + GC out, but still render ImGui so
    // the user can interact with menus / panels and see the last frame.
    if (!state->paused)
    {
        const double rateHz = kSpeedPresets[state->speed_idx].cyclesPerSecond;
        uint32_t cycles = (uint32_t)(deltaNs * rateHz * 1e-9);
        if (cycles == 0)             cycles = 1;
        if (cycles > 17030u * 16)    cycles = 17030u * 16;   // cap catch-up

        // Pull any Grid Cartographer keypresses (DIK scancodes over Gamelink
        // shared memory) into the //e keyboard latch before we run cycles,
        // so the CPU sees them this batch.
        if (state->gamelink_up) nac::RemoteInputPump();

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
    }

    Video& video = GetVideo();
    const int fbW = static_cast<int>(video.GetFrameBufferWidth());
    const int fbH = static_cast<int>(video.GetFrameBufferHeight());

    // The Apple //e framebuffer is BGRA stored bottom-up (Windows BMP
    // convention). The post-processor was developed against this same
    // convention upstream, so we leave the GL upload bottom-up and just
    // flip V at the ImGui::Image draw step.
    //
    // Grid Cartographer expects top-down, so the GameLink::Out path gets
    // its own one-shot flip into a static scratch.
    if (state->gamelink_up && g_externalMemMain)
    {
        static std::vector<uint8_t> flipped;
        const size_t rowBytes = (size_t)fbW * 4;
        flipped.resize(rowBytes * (size_t)fbH);
        const uint8_t* src = video.GetFrameBuffer();
        for (int y = 0; y < fbH; ++y)
        {
            std::memcpy(flipped.data() + y * rowBytes,
                        src + (fbH - 1 - y) * rowBytes,
                        rowBytes);
        }

        // MemGetBankPtr(0) flushes any dirty pages from the CPU's working
        // page-cache back into memmain, then returns the same pointer.
        // Without this, GC reads stale RAM — coords / direction lag by a
        // frame and look swapped on direction changes.
        const uint8_t* sysmem = MemGetBankPtr(0);

        GameLink::Out(static_cast<uint16_t>(fbW), static_cast<uint16_t>(fbH),
                      static_cast<double>(fbW) / static_cast<double>(fbH),
                      /*need_mouse*/ false,
                      flipped.data(),
                      sysmem);
    }

    state->renderer.BeginFrame();
    state->renderer.UploadFramebuffer(video.GetFrameBuffer(), fbW, fbH);

    state->renderer.BeginImGui();

    // Invisible dockspace covering the whole host window so the user can
    // dock the Apple //e and any panel into the main area, splits, etc.
    // PassthruCentralNode lets the GL clear (black background) show
    // through any unsplit area, so an empty layout still looks clean.
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(),
                                 ImGuiDockNodeFlags_PassthruCentralNode);

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Emulator"))
        {
            if (ImGui::MenuItem("Open HDV...")) OpenHdvDialog(*state);
            ImGui::Separator();
            if (ImGui::MenuItem(state->paused ? "Resume" : "Pause", "Ctrl+P"))
                EmulatorPause(*state, !state->paused);
            if (ImGui::MenuItem("Reboot", "Alt+R")) EmulatorReboot();
            ImGui::Separator();
            if (ImGui::BeginMenu("Speed"))
            {
                for (int i = 0; i < (int)(sizeof(kSpeedPresets)/sizeof(kSpeedPresets[0])); ++i)
                {
                    if (ImGui::MenuItem(kSpeedPresets[i].label, nullptr, state->speed_idx == i))
                    {
                        state->speed_idx = i;
                        ApplySpeedSetting(i);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Color"))
            {
                const bool monoOn = state->mono_idx > 0;
                for (int i = 0; i < (int)(sizeof(kVideoPresets)/sizeof(kVideoPresets[0])); ++i)
                {
                    // Greyed out while a monochrome mode is active.
                    if (ImGui::MenuItem(kVideoPresets[i].label, nullptr,
                                        !monoOn && state->video_idx == i,
                                        !monoOn))
                    {
                        state->video_idx = i;
                        ApplyVideoSettings(i, state->mono_idx);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Monochrome"))
            {
                for (int i = 0; i < (int)(sizeof(kMonoPresets)/sizeof(kMonoPresets[0])); ++i)
                {
                    if (ImGui::MenuItem(kMonoPresets[i].label, nullptr, state->mono_idx == i))
                    {
                        state->mono_idx = i;
                        ApplyVideoSettings(state->video_idx, i);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Volume (Speaker)"))
            {
                for (int i = 0; i < 5; ++i)
                {
                    if (ImGui::MenuItem(kVolumeLabels[i], nullptr, state->vol_speaker == i))
                    {
                        state->vol_speaker = i;
                        ApplyVolumeSettings(i, state->vol_mb);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Volume (Music)"))
            {
                for (int i = 0; i < 5; ++i)
                {
                    if (ImGui::MenuItem(kVolumeLabels[i], nullptr, state->vol_mb == i))
                    {
                        state->vol_mb = i;
                        ApplyVolumeSettings(state->vol_speaker, i);
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Grid Cartographer link", nullptr, state->gamelink_enabled))
            {
                state->gamelink_enabled = !state->gamelink_enabled;
                GameLink::SetGameLinkEnabled(state->gamelink_enabled);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Apple //e", nullptr, &state->apple_open);
            ImGui::MenuItem("Combat log", nullptr, state->combatLog.OpenFlag());
            ImGui::MenuItem("Hack",       nullptr, state->hackPanel.OpenFlag());
            ImGui::Separator();
            if (ImGui::MenuItem("Post-processor", nullptr, &state->pp_enabled))
                sa2::PostProcessor::GetInstance()->SetActive(state->pp_enabled);
            ImGui::MenuItem("Post-processor settings", nullptr,
                            &sa2::PostProcessor::GetInstance()->bImguiWindowIsOpen);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Profile"))
        {
            const auto names = state->sidebar.ProfileNames();
            if (names.empty())
                ImGui::MenuItem("(no profiles found)", nullptr, false, false);
            for (const auto& n : names)
            {
                const bool active = (n == state->sidebar.ActiveProfileName());
                if (ImGui::MenuItem(n.c_str(), nullptr, active))
                    state->sidebar.SetActiveProfile(n);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("(none)", nullptr, state->sidebar.ActiveProfileName().empty()))
                state->sidebar.SetActiveProfile("");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Apple //e screen as a regular dockable ImGui window. Resizable so
    // the user can scale it however they like; the framebuffer texture
    // letterboxes inside the available content area to preserve aspect.
    // When the post-processor is active we route the //e framebuffer
    // through PostProcessor::Render and display its output texture
    // instead of the raw upload.
    if (state->apple_open)
    {
        const int rfbW = state->renderer.FramebufferWidth();
        const int rfbH = state->renderer.FramebufferHeight();
        ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Apple //e", &state->apple_open, ImGuiWindowFlags_NoCollapse))
        {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            if (rfbW > 0 && rfbH > 0 && avail.x > 0 && avail.y > 0)
            {
                const float aspect = float(rfbW) / float(rfbH);
                float w = avail.x, h = avail.x / aspect;
                if (h > avail.y) { h = avail.y; w = avail.y * aspect; }
                ImGui::SetCursorPos(ImVec2(
                    ImGui::GetCursorPosX() + (avail.x - w) * 0.5f,
                    ImGui::GetCursorPosY() + (avail.y - h) * 0.5f));

                // Texture upload is bottom-up; both raw and PP-output
                // need V flipped at draw time. (PP keeps the input
                // orientation in its FBO.)
                auto* pp = sa2::PostProcessor::GetInstance();
                ImTextureID texId;
                if (pp->IsActive())
                {
                    pp->Render((int)w, (int)h,
                               state->renderer.FramebufferTexId(),
                               (uint32_t)rfbW, (uint32_t)rfbH);
                    texId = static_cast<ImTextureID>(pp->GetTextureId());
                }
                else
                {
                    texId = static_cast<ImTextureID>(state->renderer.FramebufferTexId());
                }
                ImGui::Image(texId, ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
            }
        }
        ImGui::End();
    }

    // PostProcessor settings window — its own ImGui::Begin internally.
    if (sa2::PostProcessor::GetInstance()->bImguiWindowIsOpen)
        sa2::PostProcessor::GetInstance()->RenderImGuiWindow();

    state->sidebar.Render();
    state->combatLog.Render();
    state->hackPanel.Render();
    state->renderer.EndImGui();

    state->renderer.EndFrame();

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult /*result*/)
{
    auto* state = static_cast<AppState*>(appstate);
    if (state)
    {
        SaveSettings(*state);
        ShutdownEmulator();
        if (state->gamelink_up) GameLink::Term();
        state->renderer.Shutdown();
        delete state;
    }
    SDL_Quit();
}
