// Disabled Gamelink backend. Linked on macOS, where Grid Cartographer
// doesn't run. create_mutex returns false so GameLink::Init() reports
// failure and the host frontend leaves gamelink_up cleared — every code
// path that touches shared memory is already gated on that flag.

#include "RemoteControl/Gamelink_backend.h"

namespace GameLink::backend
{
    bool create_mutex(const char*)                              { return false; }
    void destroy_mutex()                                        {}
    bool create_shared_memory(const char*, std::size_t, void**) { return false; }
    void destroy_shared_memory(std::size_t)                     {}
    bool lock(uint32_t)                                         { return false; }
    void unlock()                                               {}
}
