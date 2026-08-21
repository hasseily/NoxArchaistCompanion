#pragma once

#include <filesystem>

struct ImFont;
struct ImFontAtlas;

namespace nac
{

// Loads the original runic FONT1 data used by Nox Archaist. The source file
// contains 96 consecutive 7x8 glyphs ($20-$7F), one byte per scanline.
::ImFont* LoadNoxFont1(::ImFontAtlas& atlas,
                       const std::filesystem::path& path);

::ImFont* LoadNoxSpriteFont(::ImFontAtlas& atlas,
                            const std::filesystem::path& path,
                            const char* name = "Nox Archaist a2sharp");

} // namespace nac
