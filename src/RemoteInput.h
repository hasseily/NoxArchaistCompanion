#pragma once

namespace nac
{

// Pull keyboard state from Grid Cartographer over Gamelink shared memory
// and forward fresh / repeating keypresses into the //e via
// KeybQueueKeypress. Call once per SDL_AppIterate, before CpuExecute.
//
// GC writes keyboard state as 256 bits (8 uint32) of DIK scancodes; we
// translate to Win32 VK using the same table the old NAC frontend used,
// then dispatch to the //e's keyboard latch (ASCII path for printable +
// control chars; NOT_ASCII path for arrows / function keys).
void RemoteInputPump();

} // namespace nac
