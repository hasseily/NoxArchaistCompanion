# SDL3 / OpenGL conversion roadmap

The conversion is too large for a single session. This file breaks the
remaining work into independently committable phases. Each phase ends
with a build that works on at least one platform, so the tree is never
left in a broken state.

Status legend: ✅ done · ⏳ in progress · ◻ todo

## Phase 0 — foundation
- ✅ Branch `sdl3-multiplatform` created from `master`
- ✅ `CLAUDE.md` captures scope, target platforms, coding rules
- ✅ `docs/architecture.md` describes the post-conversion layout
- ✅ Cross-platform Gamelink (`Gamelink.h` + `Gamelink_{win32,posix}.cpp`)
- ✅ `docs/gamelink.md`

## Phase 3 — CMake build system
Add top-level `CMakeLists.txt` and per-target sub-lists. Targets:
- `nac_emulator` (static lib): vendored AppleWin core + pp.
- `nac` (executable): app layer + emulator + Gamelink.

Use `find_package(SDL3 CONFIG REQUIRED)` (FetchContent fallback for
local builds), `find_package(OpenGL REQUIRED)`. ImGui via FetchContent
or vcpkg.

Acceptance: `cmake -B build && cmake --build build` succeeds on Linux
(headless — main.cpp can be a stub that just initialises Gamelink and
exits).

✅ Done out of order, ahead of Phase 1, so the rest of the conversion
has a Linux-side build target to validate against (we live on WSL2).
The Phase 3 commit lands a minimal CMake (no SDL3/OpenGL/ImGui yet —
they arrive in Phase 4 when the platform layer needs them) plus a tiny
decoupling of `Gamelink.cpp` from the emulator globals so the protocol
layer compiles standalone.

Commit: "add CMake build, headless Linux build green".

## Phase 1 — vendor cross-platform AppleWin core
✅ Done. Replaced `NoxArchaistCompanion/Emulator/` with a stripped subset of
`hasseily/AppleWin` master. The old Win32-only fork is gone. The vendored
upstream files have removed-component refs stripped (Disk II, Mouse, Z80,
parallel, serial, Uthernet, Pravets, FourPlay, SNESMAX, CopyProtectionDongles,
Tape, Speech, debugger, Configuration helpers, ProDOS_*, Tfe, Z80VICE).
Resource-loading (`FindResource(IDR_*)`) is stubbed with `TODO Phase 4`
markers — the SDL3 frontend will load ROMs from disk. The single NAC patch
re-applied on top is `noxcpuconstants` in `CPU.{h,cpp}`. The remaining NAC
hooks (Memory.cpp Gamelink hookup, Video.cpp RemoteControl bracketing,
Harddisk.cpp NonVolatile / RemoteControl notify, DiskImageHelper.cpp WOZ
META + ProDOS HDV title parsing, CPU.cpp `Fetch()` Nox combat trap) are
deferred to the phase that wires the frontend back in.

Acceptance (revised): top-level CMake builds `nac_emulator.lib` (plus
`yaml`, `nac_zlib`, `nac_minizip` support libs) on Windows. The vcxproj is
no longer maintained — CMake is the single source of truth across
Windows/Linux/macOS.

Four commits: "vendor AppleWin core CPU+memory", "vendor AppleWin video",
"vendor AppleWin audio", "vendor AppleWin disk + I/O". Only the last builds
in isolation — `Memory.cpp` pulls peripheral headers that arrive over the
sequence — but each commit is a coherent file group.

## Phase 2 — vendor the post-processor
Copy `source/frontends/sdl/pp/` from the `pp` branch into
`NoxArchaistCompanion/Emulator/pp/`:
- `postprocessor.{cpp,h}`, `shader.{cpp,h}`
- `extras/ImGuiFileDialog.*`
- `glad/`, `glm/`, `nlohmann/json.hpp` go under `third_party/`
- `assets/`, `presets/`, `shaders/` move to `assets/pp/`

Acceptance: post-processor compiles as its own static lib with SDL3 +
OpenGL headers available; no integration yet.

Commit: "vendor post-processor from AppleWin pp branch".

## Phase 4 — SDL3 platform layer
Replace `Main.cpp`, `DeviceResources.{cpp,h}`, the `pch.h` D3D12 includes,
`HackWindow.{cpp,h}`, `LogWindow.{cpp,h}` with SDL3 + ImGui equivalents.

- New `src/main.cpp` using `SDL_MAIN_USE_CALLBACKS` (cf. AppleWin
  `frontends/sdl/sdlappmain.cpp`).
- `src/Renderer.{cpp,h}` owns the `SDL_Window`, the `SDL_GLContext`, and
  the framebuffer GL texture.
- ImGui sidebar / hack / log replace the Win32 dialogs.

Acceptance: NAC starts on Linux, shows a black window, shuts down cleanly.

Commit: "SDL3 main loop and GL context; window opens on Linux".

## Phase 5 — wire the emulator into the renderer
Hook `g_pFramebufferinfo` (BGRA 32bpp) into a GL texture. Run the
emulator tick from the SDL main loop. Don't engage the post-processor
yet — just blit straight.

Acceptance: NAC shows the Apple //e boot screen on Linux.

Commit: "render emulator framebuffer through plain GL blit".

## Phase 6 — engage the post-processor
Route the framebuffer through `PostProcessor::Render` instead of the
direct blit. ImGui menu wires up `RenderImGuiWindow`.

Acceptance: post-processor effects (scanlines, monitor curvature, etc.)
work on Linux.

Commit: "route framebuffer through post-processor".

## Phase 7 — port the sidebar
`SidebarManager` / `SidebarContent` / `Sidebar` currently use
`DirectX::SpriteBatch` and `SpriteFont`. Replace with an ImGui-based
panel — overlay rendering is straightforward in the post-processor
output area.

Acceptance: sidebar tiles, fonts, and hint sheets render on Linux.

Commit: "port sidebar to ImGui".

## Phase 8 — port keyboard / mouse / gamepad input
Replace the Win32 message loop in `Main.cpp` and the
`DirectX::Keyboard` / `DirectX::Mouse` / `DirectX::GamePad` use in
`Game.cpp` with `SDL_Event` translation. `RemoteControlManager.cpp` —
specifically the DIK→VK keyboard table — gets rewritten in
`SDL_Scancode` terms.

Acceptance: the game is fully playable on Linux.

Commit: "SDL3 input handling, scancode-based RemoteControl".

## Phase 9 — verify on Windows
Switch the Windows build to CMake, validate end-to-end (Visual Studio
generator produces a working .exe), update `.github/workflows/`.

Acceptance: GitHub Actions Windows workflow passes; Linux workflow
passes.

Commit: "Windows build via CMake; CI green on Win+Linux".

## Phase 10 — macOS
Disable Gamelink (`#define NAC_GAMELINK_ENABLED 0` for macOS), wire up
`.app` bundle (Info.plist + icon + asset copying like the AppleWin
`pp` branch does for sa2).

Acceptance: `cmake --build` produces `nac.app`; emulator + sidebar work.

Commit: "macOS .app bundle; Gamelink disabled per project policy".

## Phase 11 — cleanup
- Delete `d3dx12.h`, `ATGColors.h`, `FindMedia.h`, `ReadData.h`,
  `StepTimer.h`, `DeviceResources.{cpp,h}` once nothing references them.
- Delete `NoxArchaistCompanion.vcxproj{,.user,.filters}` and `.sln`.
- Delete `DirectXTK12-feb2023/`, `packages/`, `packages.config`.
- Delete every `#include <Windows.h>` outside `Gamelink_win32.cpp`.

Final tree matches `docs/architecture.md`.

Commit: "drop DX12 / Win32-only scaffolding".
