# NAC architecture (post-SDL3 conversion)

NAC wraps a stripped Apple //e emulator and a sidebar UI into a single windowed
application. Three layers, top to bottom:

```
+-------------------------------------------------------------+
|  App layer  (Main, Game, Sidebar*, HackWindow, LogWindow)   |
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
├── CMakeLists.txt                  top-level build
├── docs/                           feature docs (this file, etc.)
├── third_party/                    SDL3, glad, glm, imgui, nlohmann/json
├── src/
│   ├── main.cpp                    SDL_MAIN_USE_CALLBACKS entry
│   ├── App.{cpp,h}                 (was Game.cpp/h) game-loop + state
│   ├── Renderer.{cpp,h}            (was DeviceResources) GL context, FBOs
│   ├── Sidebar*, HackWindow, LogWindow   ImGui-based panels
│   ├── NonVolatile.{cpp,h}         settings persistence
│   ├── HAUtils.{cpp,h}             helpers
│   ├── emulator/                   vendored AppleWin core (stripped)
│   │   ├── CPU/   AY8910  Memory  Mockingboard  Speaker  SoundCore
│   │   ├── Video  NTSC  RGBMonitor  NTSC_CharSet  VidHD
│   │   ├── Harddisk  DiskImage  DiskImageHelper  (Smartport only)
│   │   ├── Keyboard  Joystick  LanguageCard  NoSlotClock
│   │   ├── Card  CardManager  Common
│   │   └── pp/                     post-processor (from AppleWin pp branch)
│   │       ├── postprocessor.{cpp,h}, shader.{cpp,h}
│   │       ├── shaders/  presets/  assets/
│   └── remote/                     (was RemoteControl)
│       ├── Gamelink.{cpp,h}        platform-neutral API
│       ├── Gamelink_win32.cpp      Windows shared memory + named mutex
│       ├── Gamelink_posix.cpp      shm_open + sem_open (Linux)
│       └── RemoteControlManager.{cpp,h}
└── assets/                         Profiles, Background.jpg, fonts, …
```

`NoxArchaistCompanion/` (the old VS-style nested directory) is flattened:
the existing `Emulator/` becomes `src/emulator/`, `RemoteControl/` becomes
`src/remote/`, and the rest of the .cpp/.h files move up to `src/`. The
`d3dx12.h`, `pch.{h,cpp}`, `ATGColors.h`, `StepTimer.h`,
`DeviceResources.{cpp,h}`, `FindMedia.h`, `ReadData.h`, and the
`NoxArchaistCompanion.{vcxproj,filters,user,sln}` files are deleted.

## Embedded emulator scope

Vendored from upstream AppleWin (`hasseily/AppleWin` master), then patched
with the post-processor from the `pp` branch. Files vendored:

**Core (all platforms):**
`AppleWin*`, `CPU.*` + `CPU/`, `Card*`, `Common.h`, `Memory.*`, `MemoryDefs.h`,
`SaveState.*` (only if needed for HDD state), `StdAfx.*`, `StrFormat.*`,
`SynchronousEventManager.*`, `Utilities.*`, `YamlHelper.*`, `libyaml/`.

**Video:** `Video.*`, `NTSC.*`, `NTSC_CharSet.*`, `RGBMonitor.*`, `VidHD.*`.

**Audio:** `AY8910.*`, `Mockingboard.*`, `MockingboardCardManager.*`,
`MockingboardDefs.h`, `SoundCore.*`, `Speaker.*`, `SSI263.*`, `SSI263Phonemes.h`,
`SoundBuffer.h`, `SAM.*`.

**I/O:** `Joystick.*`, `Keyboard.*`, `LanguageCard.*`, `NoSlotClock.*`,
`Harddisk.*`, `DiskImage.*` (Smartport-only paths), `DiskImageHelper.*`,
`DiskDefs.h`.

**Removed entirely (do not vendor):**
`Disk.*`, `Disk2CardManager.*`, `DiskFormatTrack.*`, `DiskLog.h`,
`6522.*`, `6821.*`, `CopyProtectionDongles.*`, `FourPlay.*`, `MouseInterface.*`,
`NetworkCard.h`, `ParallelPrinter.*`, `Peripheral_Clock_*`, `Pravets.*`,
`ProDOS_*`, `Riff.*`, `SerialComms.*`, `SNESMAX.*`, `Speech.*`, `Tape.*`,
`Tfe/`, `Uthernet*`, `W5100.h`, `Z80VICE/`, `z80emu.*`, `Debugger/`,
`Configuration/` (Win32 dialogs), `Windows/`, `frontends/{libretro,ncurses,qt}`,
`source/linux/`, `CommonVICE/`.

For each remaining file: walk it once, delete every Disk II / second-disk-drive
code path and every reference to a removed card. Leave only the minimum needed
for the Enhanced //e + 65C02 + 128 KiB + Mockingboard + Smartport HDD.

## Platform layer (SDL3 + OpenGL)

`SDL3` for window, input, audio, timers, file dialogs (`SDL_ShowOpenFileDialog`).
`SDL3_image` for PNG loading (sidebar tiles, post-processor bezels).
SDL2 is **not** a fall-back — every supported platform has SDL3.

OpenGL **3.3 core** (Windows / Linux desktop) and **OpenGL ES 3.0** (mobile /
WebGL, future). Loaded via `glad`. Renderer entry point is
`Renderer::BeginFrame()` / `Renderer::EndFrame()`. The Apple framebuffer
(`g_pFramebufferinfo->bmiHeader`, BGRA 32-bit, 600×420) is uploaded to a
single GL texture each frame, then either drawn directly or piped through the
post-processor for monitor / scanline / composite effects.

ImGui (docking branch, SDL3 + GL backends) renders the sidebar, the hack
window, the log window, and the post-processor's settings dialog. The native
Win32 menu / accelerator / message handling in `Main.cpp` is replaced by
ImGui menus + `SDL_Event` translation.

## Cross-platform Gamelink

Three implementations of the same `gamelink::Region` interface:

* `Gamelink_win32.cpp` — `CreateFileMappingA` + named mutex (current code).
* `Gamelink_posix.cpp` — `shm_open` + `ftruncate` + `mmap` + `sem_open`. The
  GC name (`DWD_GAMELINK_MMAP_R4` / `DWD_GAMELINK_MUTEX_R4`) is prefixed with
  `/` for `shm_open` per POSIX.
* `Gamelink_stub.cpp` — no-op for macOS (Grid Cartographer doesn't exist on
  macOS). Conditional via `#if defined(NAC_GAMELINK_ENABLED)` in the build.

The shared `sSharedMemoryMap_R4` struct stays byte-identical (it's the wire
format with GC) and uses fixed-width types (`uint8_t`, `uint16_t`, `uint32_t`)
instead of `UINT8`/`UINT16`/`UINT`.

## Build system

Single top-level `CMakeLists.txt`. Targets:

* `nac_emulator` — static lib, vendored AppleWin core + post-processor.
* `nac` — executable, links `nac_emulator`, SDL3, glad, ImGui, nlohmann/json.

Toolchains expected: MSVC 2022 / Ninja on Windows, GCC 13+ / Clang on Linux,
Apple Clang on macOS. CI on `.github/workflows/` will build all three.

## What ships where

| Feature                | Windows | Linux | macOS |
|------------------------|:-------:|:-----:|:-----:|
| Apple //e emulation    |   ✅    |  ✅   |  ✅   |
| Mockingboard sound     |   ✅    |  ✅   |  ✅   |
| Smartport HDD          |   ✅    |  ✅   |  ✅   |
| Post-processor shaders |   ✅    |  ✅   |  ✅   |
| Sidebar / hint sheets  |   ✅    |  ✅   |  ✅   |
| Grid Cartographer link |   ✅    |  ✅   |  ❌   |
