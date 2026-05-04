# Compiling NAC

CMake is the single source of truth for the build on every platform.
SDL3 and Dear ImGui are pulled in via `FetchContent` if a system copy
isn't already installed, so a fresh clone needs no manual dependency
fetch beyond a working compiler + CMake + the platform's GL / audio /
windowing dev headers.

Minimum tool versions: **CMake 3.20**, **C++17**.

```
git clone https://github.com/hasseily/NoxArchaistCompanion.git
cd NoxArchaistCompanion
git checkout sdl3-multiplatform   # until the conversion lands on master
```

The first configure pulls SDL3 and ImGui from upstream (a few minutes);
subsequent builds reuse them out of `build/_deps/`.

## Windows

The repo's CI builds with Visual Studio 17 2022 (`windows-latest`).
Local development uses **Visual Studio 18** with **Ninja** because
CMake doesn't yet ship a VS 18 generator — the `cl.exe` from VS 18 is
fine, you just need to drive it through `vcvars64.bat`.

```powershell
# In a regular PowerShell prompt (the bat call sets up the MSVC env
# inside the cmd subshell, then hands cmake the right cl.exe / link.exe):
$vsBat = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vsBat`" >nul && cmake -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release"
cmd /c "`"$vsBat`" >nul && cmake --build build-win"
```

The exe lands at `build-win\nac.exe`. `SDL3.dll` and the runtime data
(`assets/`, `presets/`, `shaders/`, `Assets/`, `Profiles/`,
`Resources/`) are staged next to it by a POST_BUILD step so launches
from the build tree just work.

If you have VS 17 / 2022 instead of VS 18, the standard generator
works without the vcvars dance:

```powershell
cmake -B build-win -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
cmake --build build-win --config Release
```

The exe lands at `build-win\Release\nac.exe`.

## Linux

SDL3 is built from source on Linux, so it needs the X11 / Wayland /
audio / GL dev headers its own CMake probes for. On Debian / Ubuntu:

```bash
sudo apt-get install -y \
    cmake ninja-build build-essential \
    libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
    libxfixes-dev libxi-dev libxss-dev libxkbcommon-dev \
    libwayland-dev libdecor-0-dev libdrm-dev libgbm-dev \
    libasound2-dev libpulse-dev libdbus-1-dev libudev-dev \
    libibus-1.0-dev
```

Then the standard CMake invocation:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/nac
```

If your distro packages SDL3 directly (recent Arch / Fedora / Ubuntu
24.10+), `find_package(SDL3 CONFIG)` picks it up and the FetchContent
fallback is skipped — the build is much quicker that way.

## macOS

Not yet wired up — the Phase 10 commit will add `MACOSX_BUNDLE`,
`Info.plist`, and asset-copying so `cmake --build` produces `nac.app`.

For development on a Mac today:

```bash
brew install cmake ninja sdl3
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Grid Cartographer integration is **off** on macOS (GC doesn't run
there). `Gamelink_posix.cpp` is excluded from the macOS link, and
`GameLink::Init` returns false at startup.

## What gets built

* `nac_emulator` — static lib, vendored AppleWin core (Enhanced //e,
  65C02, 128 KiB, Mockingboard, Smartport HDD, VidHD, NoSlotClock).
  Pulls in `yaml` + `nac_zlib` + `nac_minizip` support libs and, on
  non-Windows, a small `libwindows` shim for the Win32 types its
  StdAfx pulls in.
* `nac_pp` — static lib, post-processor (CRT shaders / monitor
  curvature / bezels) plus glad + ImGuiFileDialog + glm + stb.
* `imgui` — static lib, ImGui docking branch with the SDL3 + GL3
  backends.
* `nac` — the executable, links the three above + SDL3 + OpenGL.

## Common knobs

| Setting                | What it does                                    |
|------------------------|-------------------------------------------------|
| `-DCMAKE_BUILD_TYPE=Debug`   | Debug symbols + assertions in the emulator.  |
| `-DSDL_SHARED=OFF -DSDL_STATIC=ON` | Static-link SDL3 (only when SDL3 is being FetchContent'd; ignored if a system SDL3 was found). |

## CI

`.github/workflows/cmake.yml` runs the same `cmake -B build && cmake
--build build` on `ubuntu-latest` (Ninja) and `windows-latest`
(Visual Studio 17 2022) on every push and PR to `master` and
`sdl3-multiplatform`. The Linux job apt-installs the dev-header set
above; the Windows job uses the runner's pre-installed VS 17.

If your local Linux build fails with missing GL / X11 / audio
symbols, check that workflow file — its apt list is the canonical set
of dev packages SDL3 needs.
