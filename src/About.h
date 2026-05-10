#pragma once

namespace nac
{

// Modal "About NAC" dialog. Opened from the Help menu, closed via
// the Close button or by clicking outside (modal-popup behaviour).
class AboutPanel
{
public:
    void Open()    { m_pendingOpen = true; }
    void Render();

private:
    bool m_pendingOpen = false;
};

} // namespace nac
