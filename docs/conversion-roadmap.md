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
✅ Done. `src/Sidebar.{cpp,h}` reads the
JSON profiles under `NoxArchaistCompanion/Profiles/`, supports the same
var types as the old `SidebarContent` (ascii, ascii_high, int_*, lookup),
and renders each sidebar as an ImGui window anchored to the host window
edge (Right / Left / Top / Bottom). Profile picker lives in the main
menu bar (`Profile` menu); profiles are keyed by directory + meta name
since every Nox version's profiles use the same `meta.name`.

ImGui (docking branch v1.91.6-docking) is pulled via FetchContent and
built as a small static lib with the SDL3 + OpenGL3 backends. Renderer
gained `BeginImGui`/`EndImGui`; `SDL_AppEvent` forwards to
`ImGui_ImplSDL3_ProcessEvent` and swallows keyboard events when ImGui
wants capture, so the //e doesn't double-receive them.

Memory dispatch: NAC profiles use a contiguous 128 KiB convention
(0x00000-0x0FFFF = main RAM, 0x10000-0x1FFFF = aux RAM). Upstream
emulator stores main + aux as separate allocations, so `SerializeVar`
splits by offset and uses `MemGetMainPtr` or `MemGetAuxPtr` accordingly.

Main-RAM reads now go through `memshadow[addr>>8]` instead of raw
`memmain[off]`, so they honour the //e's STORE80 / RAMRD / ALTZP
soft-switches at read time (the same page table the CPU itself
dereferences). Aux-half profile addresses still go straight to
`MemGetAuxPtr`.

Per-block debounce: the formatted text only replaces the displayed
value once it has been seen `kStableFrames` (4) consecutive
SDL_AppIterate calls in a row. The CPU is often mid-update of a
sidebar buffer or has just flipped a soft switch when our iterate
wakes, so the raw read flickers; the debounce hides that glitching
without blocking legitimate gameplay changes (~67 ms extra latency).

Acceptance: sidebar shows live values for both aux-RAM (party stats,
skills) and main-RAM-via-soft-switch (coords, torches, location,
picks, spells), without flicker.

Profile picker keys by `<version-dir> / <file-stem>` (multiple .json
files in one version dir all have `meta.name = "NOXARCHAIST"`, so the
file stem disambiguates Solo / Full / Full Six Party).

Commits:
* "port sidebar to ImGui — party stats live, bank-switched fields TBD"
* "soft-switch-aware sidebar reads + per-block debounce"
* "key sidebar profiles by file stem so Solo / Full all show up".

## Phase 8 — port keyboard / mouse / gamepad input
✅ Done (mouse-as-paddle and gamepad both deferred indefinitely as
out-of-scope for Nox). `src/main.cpp::SDL_AppEvent` translates SDL3 events into
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

**Grid Cartographer keyboard input** → `src/RemoteInput.{cpp,h}`. Each
iterate (before `CpuExecute`) pulls the 256-bit DIK scancode bitmap
from Gamelink shared memory via `GameLink::In`, diffs against the
previous sample, translates fresh / repeating scancodes to Win32 VK
using the same 256-byte table the old NAC `RemoteControlManager` used,
then dispatches via `KeybQueueKeypress` (ASCII path for Return /
Backspace / Tab / Esc / letters / digits / space; NOT_ASCII for arrow
keys). Repeats throttled to one fire per 400 ms per scancode so a
held-down GC key doesn't blast the //e's single-strobe keyboard latch.
A small exclusion set drops the F-keys / page-nav VKs that the old
NAC used for window-menu shortcuts.

Deferred:
* **Mouse-as-paddle** — niche feature.
* **Gamepad** — gamepad axes routing was tried and reverted (user
  preference); local keyboard is enough for Nox.

Acceptance: typing works at the BASIC prompt; Grid Cartographer's
"send keypress" feature drives the //e through Gamelink shared mem.

Commits:
* "SDL3 keyboard input — typing works at the BASIC prompt"
* "wire Grid Cartographer keyboard input through Gamelink shared mem".

## Phase 9 — verify on Windows + Linux
✅ Done. `.github/workflows/cmake.yml` runs a matrix CMake build:
`ubuntu-latest` with Ninja, `windows-latest` with the VS 17 2022
generator. Both build SDL3 + ImGui from source via FetchContent.
Linux's apt step installs the X11 / Wayland / audio / GL dev headers
SDL3's CMake probes for.

Cross-platform plumbing landed alongside CI:
* `Emulator/libwindows/` vendored from upstream AppleWin — small
  shim that provides the Win32 types / handles / time / GDI / dsound
  decls the emulator core's StdAfx pulls in on `!_WIN32`.
* `Registry_posix.cpp` — stub for the Win32 registry surface; NAC
  persists its own settings via SDL_GetPrefPath.
* `Keyboard_posix.cpp` — simple queue replacement; NAC's frontend
  feeds it via SDL3 in `src/main.cpp`, so the upstream Win32
  GetKeyState / clipboard / AltGr / Pravets paths aren't needed.
* `Joystick_posix.cpp` — stubs every paddle/button read as
  "no joystick" (Nox is a keyboard game; gamepad was already deferred
  in Phase 8).
* libwindows VK constant table expanded to cover the codes NAC's
  `RemoteInput` and main key handler reference.

Commit: "Phase 9: GitHub Actions CMake build on Windows + Linux"
plus a handful of small follow-ups for each compile error the first
Linux run surfaced.

## Phase 10 — macOS
Disable Gamelink (`#define NAC_GAMELINK_ENABLED 0` for macOS), wire up
`.app` bundle (Info.plist + icon + asset copying like the AppleWin
`pp` branch does for sa2).

Acceptance: `cmake --build` produces `nac.app`; emulator + sidebar work.

Commit: "macOS .app bundle; Gamelink disabled per project policy".

## Phase 11 — cleanup
✅ Done. The Win32 / DX12 scaffolding (`.sln`, `.vcxproj*`, `DirectXTK12-feb2023/`,
`packages/`, `pch.*`, `d3dx12.h`, `ATGColors.h`, `FindMedia.h`, `ReadData.h`,
`StepTimer.h`, `DeviceResources.*`, `resource.{aps,h,rc}`, `settings.manifest`,
`targetver.h`, `NoxArchaistCompanion.ico`) and the superseded frontend modules
(`Sidebar.*`, `SidebarContent.*`, `SidebarManager.*`, `Main.cpp`) were dropped
in the Phase-11 commit. The reference files we kept around as templates for
later phases (`Game.*`, `HackWindow.*`, `LogWindow.*`, `NonVolatile.*`,
`HAUtils.*`, `RemoteControl/RemoteControlManager.*`) were deleted in a
follow-up cleanup pass once their SDL3/ImGui replacements all shipped.

Remaining `#include <windows.h>` references live in `src/Renderer.cpp`
(Windows OpenGL function table), `Emulator/StdAfx.h` (cross-platform
Win32-types pulldown for the emulator core, plus the libwindows shim
on POSIX) and `Emulator/minizip/iowin32.h` (Win32-only file for the
vendored ZIP library). All justified — none of them are the old
DX12-frontend Windows.h pollution the roadmap was worried about.
