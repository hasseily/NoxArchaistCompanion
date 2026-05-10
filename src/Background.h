#pragma once

namespace nac
{

// Cover-art background: loads assets/nox_cover.jpg into a GL texture
// and paints it into the ImGui viewport's background draw list each
// frame. Aspect-fit (object-fit: contain) and dimmed so panels stay
// readable. Pure paint job — no input or layout effects.
class Background
{
public:
    bool Init();      // load the texture; safe to call once after GL/ImGui are up
    void Shutdown();  // delete the texture
    void Render();    // call inside an active ImGui frame
    bool& EnabledRef() { return m_enabled; }
    float& DimRef()    { return m_dim; }   // 0..1, alpha applied as a tint

private:
    unsigned m_tex     = 0;
    int      m_w       = 0;
    int      m_h       = 0;
    bool     m_enabled = true;
    float    m_dim     = 0.30f;
};

} // namespace nac
