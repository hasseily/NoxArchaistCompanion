#pragma once

#include <cstdint>

namespace nac
{

// Debug HGR renderer that decodes the //e's interleaved HGR layout
// from any 8 KiB-aligned base address (not just $2000) on either
// main or aux bank. Useful for hunting tile-bitmap blobs that Nox
// keeps in main RAM but outside the standard graphics page.
//
// 7 monochrome pixels per byte (LSB = leftmost), 40 bytes per line,
// 192 lines = 280×192 px. The address picker accepts any uint16
// — the panel reads the 8 KiB after that address treating it like a
// graphics page.
class HGRViewerPanel
{
public:
    bool* OpenFlag()  { return &m_open; }
    bool& OpenRef()   { return m_open; }
    void  Render();

private:
    void EnsureTexture();
    void Refresh();

    bool      m_open    = false;
    int       m_baseAddr = 0x2000;  // hex input
    int       m_bank    = 0;        // 0 = main, 1 = aux
    int       m_zoom    = 2;        // integer scale 1..6
    bool      m_invert  = false;    // invert pixels (some tile blobs are stored inverted)

    unsigned  m_tex     = 0;
    static constexpr int kW = 280;
    static constexpr int kH = 192;
    uint8_t   m_pixels[kW * kH * 4] = {};
};

} // namespace nac
