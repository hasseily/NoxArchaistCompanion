#pragma once

#include <cstdint>

#include <SDL3/SDL_video.h>

namespace nac
{

// Owns the SDL window, the OpenGL context, and the framebuffer texture(s).
// Phase 4 only opens the window and clears it; Phase 5 wires the Apple //e
// framebuffer (BGRA 32bpp) into a GL texture and blits it.
class Renderer
{
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Init(const char* title, int width, int height);
    void Shutdown();

    // Clear backbuffer + (later) draw the framebuffer texture + any overlays.
    void BeginFrame();
    void EndFrame();

    SDL_Window* Window() const { return m_window; }

private:
    SDL_Window*   m_window  = nullptr;
    SDL_GLContext m_glctx   = nullptr;
};

} // namespace nac
