#include "NoxFont.h"

#include <imgui.h>

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace nac
{

namespace
{

constexpr std::array<char, 8> kMagic = { 'D', 'X', 'T', 'K', 'f', 'o', 'n', 't' };
constexpr uint32_t kDxgiFormatBc2 = 74;
constexpr size_t kNoxFont1FirstCharacter = 0x20;
constexpr size_t kNoxFont1CharacterCount = 96;
constexpr size_t kNoxFont1Rows = 8;
constexpr size_t kNoxFont1Size =
    kNoxFont1CharacterCount * kNoxFont1Rows;

struct Glyph
{
    uint32_t character;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    float xOffset;
    float yOffset;
    float xAdvance;
};

static_assert(sizeof(Glyph) == 32);

struct SpriteFont
{
    std::vector<Glyph> glyphs;
    float lineSpacing = 0.0f;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> alpha;
};

template<typename T>
bool Read(std::ifstream& input, T& value)
{
    input.read(reinterpret_cast<char*>(&value), sizeof(value));
    return input.good();
}

bool ReadSpriteFont(const std::filesystem::path& path, SpriteFont& font)
{
    std::ifstream input(path, std::ios::binary);
    std::array<char, kMagic.size()> magic{};
    uint32_t glyphCount = 0;
    if (!input.read(magic.data(), magic.size()) || magic != kMagic ||
        !Read(input, glyphCount) || glyphCount == 0 || glyphCount > 1024)
        return false;

    font.glyphs.resize(glyphCount);
    if (!input.read(reinterpret_cast<char*>(font.glyphs.data()),
                    font.glyphs.size() * sizeof(Glyph)))
        return false;

    uint32_t defaultCharacter = 0;
    uint32_t format = 0;
    uint32_t stride = 0;
    uint32_t rows = 0;
    if (!Read(input, font.lineSpacing) || !Read(input, defaultCharacter) ||
        !Read(input, font.width) || !Read(input, font.height) ||
        !Read(input, format) || !Read(input, stride) || !Read(input, rows))
        return false;

    if (font.lineSpacing <= 0.0f || font.width == 0 || font.height == 0 ||
        format != kDxgiFormatBc2 || (font.width & 3) != 0 ||
        (font.height & 3) != 0 || stride != font.width * 4 ||
        rows != font.height / 4)
        return false;

    const size_t payloadSize = static_cast<size_t>(stride) * rows;
    std::vector<uint8_t> payload(payloadSize);
    if (!input.read(reinterpret_cast<char*>(payload.data()), payload.size()))
        return false;

    font.alpha.assign(static_cast<size_t>(font.width) * font.height, 0);
    const uint32_t blocksWide = font.width / 4;
    const uint32_t blocksHigh = font.height / 4;
    for (uint32_t blockY = 0; blockY < blocksHigh; ++blockY)
    {
        for (uint32_t blockX = 0; blockX < blocksWide; ++blockX)
        {
            uint64_t alphaBits = 0;
            const size_t blockOffset =
                (static_cast<size_t>(blockY) * blocksWide + blockX) * 16;
            std::memcpy(&alphaBits, payload.data() + blockOffset,
                        sizeof(alphaBits));

            for (uint32_t y = 0; y < 4; ++y)
            {
                for (uint32_t x = 0; x < 4; ++x)
                {
                    const uint32_t pixel = y * 4 + x;
                    const uint8_t alpha =
                        static_cast<uint8_t>((alphaBits >> (pixel * 4)) & 0x0F);
                    const size_t target =
                        static_cast<size_t>(blockY * 4 + y) * font.width +
                        blockX * 4 + x;
                    font.alpha[target] = static_cast<uint8_t>(alpha * 17);
                }
            }
        }
    }

    for (const Glyph& glyph : font.glyphs)
    {
        if (glyph.character > 0x10FFFF || glyph.left < 0 || glyph.top < 0 ||
            glyph.right <= glyph.left || glyph.bottom <= glyph.top ||
            static_cast<uint32_t>(glyph.right) > font.width ||
            static_cast<uint32_t>(glyph.bottom) > font.height)
            return false;
    }
    return true;
}

void WriteAlpha(ImTextureData& texture, int x, int y, uint8_t alpha)
{
    uint8_t* target = static_cast<uint8_t*>(texture.GetPixelsAt(x, y));
    if (texture.Format == ImTextureFormat_Alpha8)
    {
        *target = alpha;
    }
    else
    {
        target[0] = 255;
        target[1] = 255;
        target[2] = 255;
        target[3] = alpha;
    }
}

ImFont* AddVectorFallback(ImFontAtlas& atlas, const char* name)
{
    ImFontConfig config;
    config.SizePixels = 16.0f;
    std::snprintf(config.Name, sizeof(config.Name), "%s", name);
    return atlas.AddFontDefaultVector(&config);
}

} // namespace

ImFont* LoadNoxFont1(ImFontAtlas& atlas,
                     const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != static_cast<std::streamoff>(kNoxFont1Size))
        return nullptr;

    std::array<uint8_t, kNoxFont1Size> data{};
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(data.data()), data.size()))
        return nullptr;

    ImFont* font = AddVectorFallback(atlas, "Nox Archaist FONT1");
    if (!font) return nullptr;

    constexpr int kScale = 2;
    constexpr int kGlyphWidth = 7 * kScale;
    constexpr int kGlyphHeight = 8 * kScale;
    std::array<ImFontAtlasRectId, 95> rectangles{};
    for (size_t i = 0; i < rectangles.size(); ++i)
    {
        rectangles[i] = atlas.AddCustomRectFontGlyph(
            font,
            static_cast<ImWchar>(kNoxFont1FirstCharacter + i),
            kGlyphWidth, kGlyphHeight, static_cast<float>(kGlyphWidth));
        if (rectangles[i] == ImFontAtlasRectId_Invalid) return nullptr;
    }

    for (size_t i = 0; i < rectangles.size(); ++i)
    {
        ImFontAtlasRect rectangle;
        if (!atlas.GetCustomRect(rectangles[i], &rectangle)) return nullptr;

        bool visible = false;
        const size_t glyphOffset = i * kNoxFont1Rows;
        for (int y = 0; y < kGlyphHeight; ++y)
        {
            const uint8_t row = data[glyphOffset + y / kScale] & 0x7F;
            for (int x = 0; x < kGlyphWidth; ++x)
            {
                const int sourceX = x / kScale;
                const uint8_t alpha =
                    (row & (1u << sourceX)) ? 255 : 0;
                WriteAlpha(*atlas.TexData, rectangle.x + x, rectangle.y + y,
                           alpha);
                visible |= alpha != 0;
            }
        }

        ImFontGlyph* loaded = font->GetFontBaked(font->LegacySize)
                                  ->FindGlyphNoFallback(
                                      static_cast<ImWchar>(
                                          kNoxFont1FirstCharacter + i));
        if (loaded)
        {
            loaded->Colored = false;
            loaded->Visible = visible;
        }
    }

    font->Flags |= ImFontFlags_LockBakedSizes;
    return font;
}

ImFont* LoadNoxSpriteFont(ImFontAtlas& atlas,
                          const std::filesystem::path& path,
                          const char* name)
{
    SpriteFont spriteFont;
    if (!ReadSpriteFont(path, spriteFont)) return nullptr;

    ImFont* font = AddVectorFallback(atlas, name);
    if (!font) return nullptr;

    std::vector<ImFontAtlasRectId> rectangles;
    rectangles.reserve(spriteFont.glyphs.size());
    for (const Glyph& glyph : spriteFont.glyphs)
    {
        const int width = glyph.right - glyph.left;
        const int height = glyph.bottom - glyph.top;
        const float advance = glyph.xOffset + width + glyph.xAdvance;
        const ImFontAtlasRectId id = atlas.AddCustomRectFontGlyph(
            font, static_cast<ImWchar>(glyph.character), width, height,
            advance, ImVec2(glyph.xOffset, glyph.yOffset));
        if (id == ImFontAtlasRectId_Invalid) return nullptr;
        rectangles.push_back(id);
    }

    for (size_t i = 0; i < spriteFont.glyphs.size(); ++i)
    {
        const Glyph& glyph = spriteFont.glyphs[i];
        ImFontAtlasRect rectangle;
        if (!atlas.GetCustomRect(rectangles[i], &rectangle)) return nullptr;

        bool visible = false;
        for (int y = 0; y < glyph.bottom - glyph.top; ++y)
        {
            for (int x = 0; x < glyph.right - glyph.left; ++x)
            {
                const uint8_t alpha = spriteFont.alpha[
                    static_cast<size_t>(glyph.top + y) * spriteFont.width +
                    glyph.left + x];
                WriteAlpha(*atlas.TexData, rectangle.x + x, rectangle.y + y,
                           alpha);
                visible |= alpha != 0;
            }
        }

        ImFontGlyph* loaded = font->GetFontBaked(font->LegacySize)
                                  ->FindGlyphNoFallback(
                                      static_cast<ImWchar>(glyph.character));
        if (loaded)
        {
            loaded->Colored = false;
            loaded->Visible = visible;
        }
    }

    font->Flags |= ImFontFlags_LockBakedSizes;
    return font;
}

} // namespace nac
