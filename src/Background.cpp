#include "Background.h"

#include <SDL3/SDL_filesystem.h>
#include <glad/glad.h>
#include <imgui.h>
#include <stb_image.h>

#include <algorithm>
#include <filesystem>

namespace nac
{

namespace
{

std::filesystem::path FindCoverPath()
{
    const char* base = SDL_GetBasePath();
    std::filesystem::path dir = base ? base : ".";
    for (int i = 0; i < 5; ++i)
    {
        const auto candidate = dir / "assets" / "nox_cover.jpg";
        if (std::filesystem::exists(candidate)) return candidate;
        const auto srcCandidate = dir / "assets" / "pp" / "assets" / "nox_cover.jpg";
        if (std::filesystem::exists(srcCandidate)) return srcCandidate;
        dir = dir.parent_path();
        if (dir.empty()) break;
    }
    return {};
}

}

bool Background::Init()
{
    const auto path = FindCoverPath();
    if (path.empty()) return false;

    int n = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &m_w, &m_h, &n, 4);
    if (!data) return false;

    glGenTextures(1, &m_tex);
    glBindTexture(GL_TEXTURE_2D, m_tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_w, m_h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
    return true;
}

void Background::Shutdown()
{
    if (m_tex)
    {
        glDeleteTextures(1, &m_tex);
        m_tex = 0;
    }
}

void Background::Render()
{
    if (!m_enabled || !m_tex) return;

    ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 vpos = vp->Pos;
    const ImVec2 vsize = vp->Size;
    if (vsize.x <= 1.0f || vsize.y <= 1.0f) return;

    // Aspect-fit (contain): scale so the longest axis matches the
    // viewport, leaving black letterbox on the other.
    const float imgAspect = (float)m_w / (float)m_h;
    const float vpAspect  = vsize.x / vsize.y;
    float w, h;
    if (imgAspect > vpAspect)
    {
        w = vsize.x;
        h = vsize.x / imgAspect;
    }
    else
    {
        h = vsize.y;
        w = vsize.y * imgAspect;
    }
    const ImVec2 p0(vpos.x + (vsize.x - w) * 0.5f,
                    vpos.y + (vsize.y - h) * 0.5f);
    const ImVec2 p1(p0.x + w, p0.y + h);

    const float a = (std::max)(0.0f, (std::min)(1.0f, m_dim));
    const ImU32 tint = IM_COL32(255, 255, 255, (int)(a * 255.0f));
    ImGui::GetBackgroundDrawList(vp)->AddImage(
        static_cast<ImTextureID>((uintptr_t)m_tex),
        p0, p1, ImVec2(0, 0), ImVec2(1, 1), tint);
}

} // namespace nac
