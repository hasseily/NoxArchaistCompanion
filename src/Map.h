#pragma once

namespace nac
{

// Internal automap. Phase A: a tiny diagnostic window that reads the
// five RAM bytes documented by GridCartographer-Mapping's
// Profiles/Nox_Archaist.xml peek line and displays them raw, so we can
// confirm the addresses agree with what the live game stores before
// later phases (translation, fog-of-war, render) build on them.
//
// Addresses (1 byte each, all CPU-visible — read through memshadow so
// soft switches are honoured):
//   $2AF9  mapID
//   $0000  region
//   $267D  maptype
//   $6CEC  xpos
//   $6CED  ypos
class MapPanel
{
public:
    bool* OpenFlag()    { return &m_open; }
    bool& OpenRef()     { return m_open; }
    void  Render();

private:
    bool m_open = false;
};

} // namespace nac
