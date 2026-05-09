# NAC architecture

NAC wraps a stripped Apple //e emulator and an ImGui sidebar UI into a
single windowed application. Three layers, top to bottom:

```
+-------------------------------------------------------------+
|  App layer  (main, Sidebar, NoxHacks, RemoteInput)          |
+-------------------------------------------------------------+
|  Platform layer  (SDL3 window/input/audio  +  OpenGL render)|
+-------------------------------------------------------------+
|  Emulator layer  (vendored AppleWin core, post-processor)   |
+-------------------------------------------------------------+
|  RemoteControl  (Gamelink shared-memory bridge to GC)       |
+-------------------------------------------------------------+
```

## Source layout

```
NoxArchaistCompanion/
├── CMakeLists.txt                  top-level build (single source of truth)
├── docs/                           feature docs (this file, etc.)
├── third_party/                    glad, glm, stb, nlohmann/json, ImGuiFileDialog
├── assets/pp/                      post-processor presets / shaders / bezels
├── src/
│   ├── main.cpp                    SDL_MAIN_USE_CALLBACKS entry, menus, settings
│   ├── Renderer.{cpp,h}            SDL3 window + GL 4.1 core context, framebuffer texture, ImGui plumbing
│   ├── Frame.{cpp,h}               minimal FrameBase: ROM loading, AudioOutput::Create
│   ├── AudioOutput.{cpp,h}         SDL3 audio backend extending LinuxSoundBuffer
│   ├── Sidebar.{cpp,h}             JSON profile loader + ImGui sidebar windows
│   ├── NoxHacks.{cpp,h}            ConversationLogPanel + HackPanel + LoadNoxConstants
│   ├── RemoteInput.{cpp,h}         Grid Cartographer DIK keypresses → //e keyboard latch
│   └── PropertySheet.h             all-defaults IPropertySheet stub
└── NoxArchaistCompanion/
    ├── Assets/                     Versions.json (per-version PCs / addresses)
    ├── Profiles/                   per-Nox-version sidebar profile JSON
    ├── Emulator/                   vendored AppleWin core (stripped)
    │   ├── CPU/  Memory  Card  CardManager  SaveState  ...
    │   ├── Video  NTSC  RGBMonitor  NTSC_CharSet  VidHD
    │   ├── Speaker  Mockingboard  AY8910  SSI263  SoundCore  SAM
    │   ├── Harddisk  DiskImage  DiskImageHelper  (Smartport only)
    │   ├── Keyboard  Joystick  LanguageCard  NoSlotClock  Common
    │   ├── libyaml/  zlib/  minizip/
    │   ├── Resources/              Apple //e ROMs (loaded at boot)
    │   └── pp/                     post-processor (from AppleWin pp branch)
    │       ├── postprocessor.{cpp,h}, shader.{cpp,h}
    └── RemoteControl/
        ├── Gamelink.{cpp,h}        platform-neutral protocol layer
        ├── Gamelink_backend.h      shm/mutex backend interface
        ├── Gamelink_win32.cpp      CreateFileMapping + named mutex
        └── Gamelink_posix.cpp      shm_open + sem_open (Linux)
```

## Embedded emulator scope

Vendored from upstream AppleWin (`hasseily/AppleWin` master) plus the
post-processor from the `pp` branch. NAC-specific patches re-applied on
top of the vendored core:

* `CPU.{h,cpp}` — `noxcpuconstants` struct + `g_noxLogCallback` Fetch trap
  for the combat-log panel.
* `Memory.{h,cpp}` — `g_externalMemMain` so the CPU's main RAM can be
  Gamelink shared memory (zero-copy export to Grid Cartographer).
* `Harddisk.cpp` — Smartport firmware load restored.
* `DiskImageHelper.{cpp,h}` — `szTitle` / `szSubtitle` / `szVersion` /
  `szVolumeName` populated from WOZ META and ProDOS HDV headers.
* `RGBMonitor.cpp` — text-fringe suppression in `UpdateHiResRGBCell`
  (the "Text-Optimized RGB" feature).

**Removed entirely from the upstream vendoring:**
Disk II floppy emulation, all expansion cards other than Mockingboard /
Smartport HDD / VidHD / NoSlotClock, debugger UI, Configuration property
sheets, every frontend except the new SDL3 one, and every platform other
than Windows / Linux / macOS.

## Platform layer (SDL3 + OpenGL)

`SDL3` (FetchContent fallback to `release-3.2.20`) for window, input,
audio, timers, and the async file dialog (`SDL_ShowOpenFileDialog`).
SDL2 is **not** a fall-back — every supported platform has SDL3.

OpenGL **4.1 core** context. The Apple //e framebuffer (BGRA 32bpp,
600×420 default) is uploaded to a single GL texture each frame and
either drawn directly or piped through the post-processor for monitor
curvature / scanlines / composite effects.

ImGui (docking branch v1.91.6-docking, FetchContent + small static lib
with the SDL3 + OpenGL3 backends) renders the sidebar, the hack panel,
the combat-log panel, the post-processor settings panel, and the main
menu bar. The Apple //e screen is itself a dockable ImGui window. All
SDL3 keyboard events go through `ImGui_ImplSDL3_ProcessEvent` first;
the //e only sees keys when no ImGui text widget has focus.

## Cross-platform Gamelink

Two implementations of the same `gamelink::Region` interface:

* `Gamelink_win32.cpp` — `CreateFileMappingA` + named mutex.
* `Gamelink_posix.cpp` — `shm_open` + `ftruncate` + `mmap` + `sem_open`.
  The GC name (`DWD_GAMELINK_MMAP_R4` / `DWD_GAMELINK_MUTEX_R4`) is
  prefixed with `/` for `shm_open` per POSIX.

macOS doesn't link a backend (Grid Cartographer doesn't run there) — the
top-level `CMakeLists.txt` adds `Gamelink_posix.cpp` only on non-Apple
non-Windows targets. Calls to `GameLink::Init` on macOS just return false
and the rest of the app keeps running.

The shared `sSharedMemoryMap_R4` struct stays byte-identical with GC (it's
the wire format) and uses fixed-width types (`uint8_t` / `uint16_t` /
`uint32_t`) instead of `UINT8` / `UINT16` / `UINT`.

## Settings persistence

`SDL_GetPrefPath("hasseily", "NoxArchaistCompanion")` resolves to the
per-user config dir on each platform. Three files live there:

* `imgui.ini` — ImGui's own window layout / dock state (written by ImGui).
* `pp_state.json` — post-processor state (written by `PostProcessor::SaveState`).
* `nac.json` — everything else: window size, last HDV path, speed/colour/
  mono/volume preset indices, panel open flags, sidebar profile choice,
  hack-panel hex/poke-address, combat-log auto-scroll/include-combat.

`LoadSettings` runs before `InitEmulator` so the saved volumes and HDV
path can flow into the first init pass. `SaveSettings` runs from
`SDL_AppQuit`.

## Build system

Single top-level `CMakeLists.txt`. Targets:

* `nac_emulator` — static lib, vendored AppleWin core.
* `nac_pp` — static lib, post-processor + glad + ImGuiFileDialog + glm + stb.
* `imgui` — static lib, ImGui + SDL3 / GL3 backends (FetchContent).
* `nac` — executable; links the three above + SDL3 + OpenGL.

Toolchains: MSVC 19 (Visual Studio 18) on Windows via Ninja + vcvars64;
GCC 13+ / Clang on Linux; Apple Clang on macOS.

POST_BUILD steps copy `SDL3.dll` (Windows) and stage `assets/`,
`presets/`, `shaders/`, `Assets/`, `Profiles/`, `Resources/` next to the
exe so launches from the build tree just work.

## What ships where

| Feature                | Windows | Linux | macOS |
|------------------------|:-------:|:-----:|:-----:|
| Apple //e emulation    |   ✅    |  ✅   |  ✅   |
| Mockingboard sound     |   ✅    |  ✅   |  ✅   |
| Smartport HDD          |   ✅    |  ✅   |  ✅   |
| Post-processor shaders |   ✅    |  ✅   |  ✅   |
| Sidebar / hint sheets  |   ✅    |  ✅   |  ✅   |
| Grid Cartographer link |   ✅    |  ✅   |  ❌   |
