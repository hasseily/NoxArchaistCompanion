#include "Renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>

// Bring in just glClear / glClearColor / glViewport. These are OpenGL 1.x
// entry points that opengl32.lib (or libGL on Linux) exports directly, so
// no extension loader (glad) is needed yet — that arrives with Phase 5
// when we start binding textures and shaders.
#if defined(_WIN32)
    #include <windows.h>
    #include <GL/gl.h>
#elif defined(__APPLE__)
    #include <OpenGL/gl.h>
#else
    #include <GL/gl.h>
#endif

#include <cstdio>

namespace nac
{

Renderer::Renderer() = default;

Renderer::~Renderer()
{
    Shutdown();
}

bool Renderer::Init(const char* title, int width, int height)
{
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);

    m_window = SDL_CreateWindow(title, width, height,
                                SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!m_window)
    {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return false;
    }

    m_glctx = SDL_GL_CreateContext(m_window);
    if (!m_glctx)
    {
        std::fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
        return false;
    }

    SDL_GL_MakeCurrent(m_window, m_glctx);
    SDL_GL_SetSwapInterval(1);

    return true;
}

void Renderer::Shutdown()
{
    if (m_glctx)
    {
        SDL_GL_DestroyContext(m_glctx);
        m_glctx = nullptr;
    }
    if (m_window)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

void Renderer::BeginFrame()
{
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(m_window, &w, &h);
    glViewport(0, 0, w, h);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void Renderer::EndFrame()
{
    SDL_GL_SwapWindow(m_window);
}

} // namespace nac
