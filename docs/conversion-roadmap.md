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
✅ Done (window-only). New `src/main.cpp` uses `SDL_MAIN_USE_CALLBACKS`
(`SDL_AppInit/Event/Iterate/Quit`). New `src/Renderer.{cpp,h}` owns the
`SDL_Window` and the `SDL_GLContext`, requests OpenGL 3.3 core, clears the
backbuffer black, and swaps. The `nac` executable now links the
`nac_emulator` static lib + `SDL3::SDL3` + `OpenGL::GL` and is built as a
GUI app (`WIN32` flag — no console window). Esc or window-close exits
cleanly. Gamelink still comes up at startup.

SDL3 is pulled via `find_package(SDL3 CONFIG QUIET)` with a `FetchContent`
fallback to `release-3.2.20` so a fresh clone builds without external
setup. On Windows a POST_BUILD step copies `SDL3.dll` next to `nac.exe`.

ImGui (sidebar / hack / log windows) is **deferred** — it lands when there's
real UI to render (Phase 6/7), not while the window is still empty.

Acceptance met: `nac.exe` opens a black 800×600 GL window, pumps events,
shuts down cleanly. Phase 5 wires the Apple //e framebuffer (BGRA 32bpp)
into a GL texture and blits it.

Commit: "SDL3 main loop and GL context; black window opens".

## Phase 5 — wire the emulator into the renderer
✅ Done. The SDL main loop runs `CpuExecute(kCyclesPerFrame, true)` then
`GetFrame().VideoRedrawScreen()` each iteration; the resulting BGRA
framebuffer is uploaded to a GL texture and drawn as a letterboxed full-
window quad. New `src/Frame.{cpp,h}` is a minimal `FrameBase` that owns
the framebuffer, loads ROMs from `Resources/`, and stubs everything else.
`src/PropertySheet.h` is an all-defaults `IPropertySheet`.

ROM loading is real now: `MemInitializeROM` and `make_csbits` (in
`NTSC_CharSet`) call `GetFrame().GetResource(IDR_*, ...)` against the
files in `Emulator/Resources/`. The resource IDs live in
`Emulator/ResourceIds.h`. Apple //e Enhanced video ROM
(`Apple2e_Enhanced_Video.rom`) was copied in from upstream — NAC's
original `CHARSET8C.bmp` was a Pravets bitmap, not the Enhanced //e
video ROM.

`DSAvailable()` is stubbed to `false` in `src/Frame.cpp` so the
`Speaker` / `Mockingboard` / `SSI263` init paths bail out cleanly without
audio — sound arrives in a later phase via SDL_audio.

Renderer still uses an OpenGL **compatibility profile** + immediate-mode
quad; Phase 6 (post-processor) swaps to 3.3 core + glad + shaders.

Acceptance met: `nac.exe` shows the Apple //e boot screen.

Commit: "wire the emulator framebuffer through a plain GL blit".

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
⏳ Keyboard done. `src/main.cpp::SDL_AppEvent` translates SDL3 events into
the emulator's `KeybQueueKeypress(key, ASCII | NOT_ASCII)` API:
* `SDL_EVENT_TEXT_INPUT` → ASCII path for printable chars (UTF-8 → 7-bit).
* `SDL_EVENT_KEY_DOWN` for control chars (Return, Backspace, Tab, Esc) →
  ASCII path with `0x0D` / `0x08` / `0x09` / `0x1B`. The //e firmware
  reads these as keyboard-latch chars, not as VK events.
* `SDL_EVENT_KEY_DOWN` for arrows / Insert / Delete / Home / End /
  PageUp/Down / F-keys → VK path via a small `SdlKeyToVK` table.
* `SDLK_CAPSLOCK` toggles `KeybToggleCapsLock`.
* Initial `KeybSetCapsLock(false)` matches the //e Enhanced shipping
  default (Caps Lock up).
* Alt+F4 closes the window. Esc is **not** a quit shortcut — the //e
  firmware uses it.

Still TODO: mouse, gamepad, and the `RemoteControlManager.cpp` DIK→VK
keyboard-table rewrite in `SDL_Scancode` terms (deferred until the
RemoteControl frontend is wired in).

Acceptance (partial): you can type at the BASIC prompt — `print 2+2`
↵ → `4`. Game-playability acceptance waits on RemoteControlManager + HDD.

Commit: "SDL3 keyboard input — typing works at the BASIC prompt".

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
