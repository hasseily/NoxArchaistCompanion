#pragma once

struct ImFont;

namespace nac
{

// Stored in nac.json (key "interface_color"). Order is locked — these
// are written as plain ints.
enum InterfaceColor
{
    InterfaceColor_White = 0,
    InterfaceColor_Green = 1,
    InterfaceColor_Amber = 2,
};

// Stored in nac.json (key "interface_font"). Order is locked because these
// values are persisted as plain integers.
enum InterfaceFont
{
    InterfaceFont_NoxFont1 = 0,
    InterfaceFont_A2Sharp = 1,
};

// Configures the active ImGui context to look like an Apple //e text
// terminal: the selected Nox bitmap font + chosen phosphor palette + zero
// rounding / single-pixel borders. Safe to call repeatedly; both fonts are
// loaded on the first call and later calls switch the active face or palette.
//
// FONT1 is the game's original first/runic font. a2sharp is retained as the
// cleaner alternate. Missing glyphs fall back to Dear ImGui's embedded font.
void ApplyAppleStyle(InterfaceColor color, InterfaceFont font);

// Secondary 10px ProggyForever instance. Used by panels that want denser
// text, such as the memory hex viewer. Falls back to the default font.
::ImFont* MonoFont();

} // namespace nac
