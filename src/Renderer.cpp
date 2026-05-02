#include "Renderer.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>

#if defined(_WIN32)
    #include <windows.h>
    #include <GL/gl.h>
#elif defined(__APPLE__)
    #include <OpenGL/gl.h>
#else
    #include <GL/gl.h>
#endif

// GL/gl.h on Windows is locked to OpenGL 1.1 — these constants come in at
// 1.2 / 1.3 and aren't auto-loaded yet (glad arrives in Phase 6).
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
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
    // Phase 5: compatibility profile + immediate-mode quads. Phase 6 swaps
    // to 3.3 core with shaders + glad when the post-processor lands.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
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

    glGenTextures(1, &m_framebufferTex);
    glBindTexture(GL_TEXTURE_2D, m_framebufferTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void Renderer::Shutdown()
{
    if (m_framebufferTex)
    {
        glDeleteTextures(1, &m_framebufferTex);
        m_framebufferTex = 0;
    }
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

void Renderer::UploadFramebuffer(const void* bgra, int w, int h)
{
    if (!bgra || w <= 0 || h <= 0) return;

    glBindTexture(GL_TEXTURE_2D, m_framebufferTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    if (w != m_texWidth || h != m_texHeight)
    {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_BGRA_EXT, GL_UNSIGNED_BYTE, bgra);
        m_texWidth  = w;
        m_texHeight = h;
    }
    else
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                        GL_BGRA_EXT, GL_UNSIGNED_BYTE, bgra);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Renderer::DrawFramebuffer()
{
    if (!m_framebufferTex || m_texWidth == 0) return;

    int winW = 0, winH = 0;
    SDL_GetWindowSizeInPixels(m_window, &winW, &winH);

    // Letterbox / pillarbox to preserve the framebuffer's aspect ratio.
    const float texAspect = float(m_texWidth) / float(m_texHeight);
    const float winAspect = float(winW)       / float(winH);

    float drawW = float(winW), drawH = float(winH);
    if (winAspect > texAspect)
    {
        drawW = float(winH) * texAspect;
    }
    else
    {
        drawH = float(winW) / texAspect;
    }
    const float x0 = (float(winW) - drawW) * 0.5f;
    const float y0 = (float(winH) - drawH) * 0.5f;

    // Map pixel coords into normalized device coords.
    auto px = [&](float p) { return (p / float(winW)) * 2.0f - 1.0f; };
    auto py = [&](float p) { return 1.0f - (p / float(winH)) * 2.0f; }; // flip Y

    const float left   = px(x0);
    const float right  = px(x0 + drawW);
    const float top    = py(y0);
    const float bottom = py(y0 + drawH);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, m_framebufferTex);

    glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(left,  top);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(right, top);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(left,  bottom);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(right, bottom);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

void Renderer::EndFrame()
{
    SDL_GL_SwapWindow(m_window);
}

} // namespace nac
