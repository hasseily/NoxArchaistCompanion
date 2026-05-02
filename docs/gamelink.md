# Gamelink (cross-platform shared-memory bridge)

NAC plays the *server* role in the Gamelink protocol. A companion app — most
commonly Grid Cartographer (GC) — opens the same named shared-memory region
and named mutex, reads frames + RAM peeks that NAC writes, and writes back
input + terminal commands.

## Protocol

Defined by the byte-identical `sSharedMemoryMap_R4` struct in
`Gamelink.h`. The struct is packed (`#pragma pack(1)`) and uses fixed-width
integer types (`uint8_t` / `uint16_t` / `uint32_t`), so the layout is
identical on every platform we support. Names exposed to the OS:

| Resource          | Name                       |
|-------------------|----------------------------|
| Shared memory     | `DWD_GAMELINK_MMAP_R4`     |
| Named mutex       | `DWD_GAMELINK_MUTEX_R4`    |

NAC announces itself as `"AppleWin"` in the `system` field so GC's existing
profile machinery just works.

## Layering

```
Gamelink.h               public API
Gamelink.cpp             protocol logic (no OS calls)
Gamelink_backend.h       private OS-primitive interface
Gamelink_win32.cpp       implements backend with CreateFileMapping + named mutex
Gamelink_posix.cpp       implements backend with shm_open + sem_open (Linux)
```

The two backend translation units are mutually exclusive — each is wrapped
in `#if defined(_WIN32)` / `#if !defined(_WIN32) && !defined(__APPLE__)` so
exactly one contains code for any given build. macOS gets neither; Gamelink
is unsupported there because Grid Cartographer doesn't run on macOS.

The backend interface is six functions:

```cpp
namespace GameLink::backend {
    bool create_mutex(const char* name);
    void destroy_mutex();
    bool create_shared_memory(const char* name, std::size_t size, void** out);
    void destroy_shared_memory(std::size_t size);
    bool lock(uint32_t timeout_ms);
    void unlock();
}
```

Anything beyond those six (Win32 handles, POSIX FDs, shm_unlink bookkeeping,
etc.) lives entirely inside the backend file.

## Remote commands

GC can drive a few host-level actions through the `:reset` / `:pause` /
`:shutdown` terminal commands. The protocol layer can't act on these
directly — on Windows it would mean `PostMessageW`, on Linux an SDL
event push — so it forwards them through a host-supplied callback:

```cpp
GameLink::SetRemoteCommandHandler([](GameLink::RemoteCommand c) { … });
```

`Main.cpp` registers a callback that posts the equivalent `WM_COMMAND`
messages, preserving the existing "no confirmation dialog on remote
reset" behaviour (`lParam == 1`).

## Linux specifics

* `shm_open` and `sem_open` names must start with `/` (POSIX); the backend
  prepends it transparently. The struct names on the wire remain
  `DWD_GAMELINK_*` so a Linux build of GC sees the same identifiers as the
  Windows build.
* The semaphore is opened with `O_CREAT | O_EXCL`. If `EEXIST` comes back,
  another instance of NAC is already running and we refuse to start.
* The shared region is `shm_unlink`'d on shutdown so a clean exit doesn't
  leave `/dev/shm/DWD_GAMELINK_MMAP_R4` behind.
* Linking requires `-lrt -lpthread` (built into the CMake target on Linux).

## What's deferred

`RemoteControlManager.cpp` still uses Win32 (`UINT`, `LPARAM`, `VK_*`,
`GetTickCount64`, virtual-key translation tables). It's compiled only on
Windows today; a follow-up pass alongside the SDL3 conversion will replace
the keyboard-state translation with `SDL_Scancode` and the timing helpers
with `SDL_GetTicks`.
