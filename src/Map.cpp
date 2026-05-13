#include "Emulator/StdAfx.h"

#include "Map.h"

#include "Emulator/CPU.h"     // cpuconstants
#include "Emulator/Memory.h"
#include "RamSnapshot.h"

#include <imgui.h>
#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>

// On the nac executable target NOMINMAX isn't defined, so windows.h
// (pulled in via StdAfx.h) leaves min / max as macros. Wrapping the
// std calls in extra parens prevents the macro expansion at the call
// site without affecting the resulting code.

namespace nac
{

namespace
{

uint8_t Peek(uint16_t addr)
{
    return SnapshotPeek(addr);
}

// Returns true once Nox has a real party loaded (i.e. the user has
// actually started or restored a game). Each of the six party slots
// is 0x80 bytes starting at MEM_PARTY, with the member's name as a
// high-ASCII NUL-terminated string at offset 0x4B; an empty slot has
// 0x00 there. We gate live automap observation on this so the BASIC
// prompt / boot-screen garbage doesn't paint into the discovered map.
bool HasParty()
{
    if (!g_ramSnapshot.valid || cpuconstants.MEM_PARTY == 0) return false;
    for (uint32_t k = 0; k < 6; ++k)
    {
        const uint32_t addr = cpuconstants.MEM_PARTY + k * 0x80u + 0x4Bu;
        if (addr >= 0x20000u) continue;
        const uint8_t b = (addr < 0x10000u)
            ? g_ramSnapshot.main[addr]
            : g_ramSnapshot.aux[addr - 0x10000u];
        if (b != 0) return true;
    }
    return false;
}

// Vision footprint. Nox draws a 17×11 viewport around the avatar when
// outdoors; we reveal the same footprint into the fog bitmap each time
// the safe-sample callback fires.
constexpr int kVisionW = 17;
constexpr int kVisionH = 11;

// Map-type byte at main $267D — Nox writes which kind of map is being
// rendered into the visible-tile buffer at $0800. $FF means "combat
// map", which we treat as "don't observe / don't paint into the
// player's discovered overworld map". The other values are useful as
// labels in the diag readout.
constexpr uint16_t kAddrMapType = 0x267D;
constexpr uint8_t  kMapTypeCombat = 0xFF;

const char* MapTypeName(uint8_t t)
{
    switch (t)
    {
    case 0x00: return "Surface";
    case 0x02: return "Undermap";
    case 0x03: return "Ruin";
    case 0x04: return "Undermap T";
    case 0x05: return "Town2";
    case 0x06: return "Town";
    case 0x07: return "Castle";
    case 0x08: return "Keep";
    case 0xFF: return "Combat";
    default:   return "?";
    }
}

} // namespace

// ---------------------------------------------------------------------------
// MapTranslator
// ---------------------------------------------------------------------------

void MapTranslator::Load(const std::filesystem::path& assetsDir)
{
    m_records.clear();
    const auto path = assetsDir / "maps" / "maps_index.json";
    if (!std::filesystem::exists(path)) return;
    try
    {
        std::ifstream f(path);
        nlohmann::json j; f >> j;
        if (!j.is_array()) return;
        m_records.reserve(j.size());
        for (const auto& r : j)
        {
            Record rec;
            rec.id      = r.value("id",      0);
            rec.region  = r.value("region",  0);
            rec.name    = r.value("name",    std::string{});
            rec.floor   = r.value("floor",   std::string{});
            rec.width   = r.value("width",   0);
            rec.height  = r.value("height",  0);
            rec.xmin    = r.value("xmin",    0);
            rec.xmax    = r.value("xmax",    255);
            rec.ymin    = r.value("ymin",    0);
            rec.ymax    = r.value("ymax",    255);
            rec.xoffset = r.value("xoffset", 0);
            rec.yoffset = r.value("yoffset", 0);
            m_records.push_back(std::move(rec));
        }
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "MapTranslator: parse failed for %s: %s\n",
                     path.string().c_str(), e.what());
        m_records.clear();
        return;
    }

    // Resolve is first-match-wins, so narrower xy ranges must come
    // before catch-alls that share the same mapID — otherwise a
    // 0..255 / 0..255 sibling shadows the specific records. The
    // generator's emission order isn't reliable; sort here by xy
    // area ascending so the runtime behaviour holds regardless.
    std::stable_sort(m_records.begin(), m_records.end(),
        [](const Record& a, const Record& b) {
            const int aa = (a.xmax - a.xmin + 1) * (a.ymax - a.ymin + 1);
            const int ba = (b.xmax - b.xmin + 1) * (b.ymax - b.ymin + 1);
            return aa < ba;
        });
}

std::optional<MapLocation> MapTranslator::Resolve(uint8_t mapID,
                                                  uint8_t xpos,
                                                  uint8_t ypos) const
{
    // Linear scan — 89 records, called once per frame at most. Load()
    // sorts the records by xy-range area ascending, so a catch-all
    // entry for a mapID only matches when none of its narrower
    // siblings did.
    for (const auto& r : m_records)
    {
        if (r.id != mapID)                       continue;
        if (xpos < r.xmin || xpos > r.xmax)      continue;
        if (ypos < r.ymin || ypos > r.ymax)      continue;

        MapLocation loc;
        loc.region      = r.region;
        loc.region_name = r.name;
        loc.floor       = r.floor;
        loc.x           = (int)xpos + r.xoffset;
        loc.y           = (int)ypos + r.yoffset;
        loc.width       = r.width;
        loc.height      = r.height;
        return loc;
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// MapData
// ---------------------------------------------------------------------------

void MapData::Load(const std::filesystem::path& assetsDir)
{
    m_blob.clear();
    m_index.clear();

    const auto path = assetsDir / "maps" / "maps.bin";
    if (!std::filesystem::exists(path))
    {
        std::fprintf(stderr, "MapData: %s not found\n", path.string().c_str());
        return;
    }

    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return;
    const auto size = f.tellg();
    f.seekg(0);
    m_blob.resize((size_t)size);
    f.read(reinterpret_cast<char*>(m_blob.data()), size);

    if (m_blob.size() < 12 || std::memcmp(m_blob.data(), "NMAP", 4) != 0)
    {
        std::fprintf(stderr, "MapData: bad magic / too short\n");
        m_blob.clear();
        return;
    }

    const uint32_t version = *reinterpret_cast<const uint32_t*>(m_blob.data() + 4);
    const uint32_t count   = *reinterpret_cast<const uint32_t*>(m_blob.data() + 8);
    if (version != 1)
    {
        std::fprintf(stderr, "MapData: unsupported version %u\n", version);
        m_blob.clear();
        return;
    }

    constexpr size_t kHeaderSize = 12;
    constexpr size_t kRecordSize = 20;
    if (m_blob.size() < kHeaderSize + (size_t)count * kRecordSize)
    {
        std::fprintf(stderr, "MapData: truncated record table\n");
        m_blob.clear();
        return;
    }

    for (uint32_t i = 0; i < count; ++i)
    {
        const uint8_t* p = m_blob.data() + kHeaderSize + i * kRecordSize;
        FloorRecord r;
        r.region_id = *reinterpret_cast<const uint16_t*>(p + 0);
        char fbuf[5] = {};
        std::memcpy(fbuf, p + 2, 4);
        r.floor    = fbuf;
        r.width    = *reinterpret_cast<const uint16_t*>(p + 6);
        r.height   = *reinterpret_cast<const uint16_t*>(p + 8);
        r.origin_x = *reinterpret_cast<const int16_t* >(p + 10);
        r.origin_y = *reinterpret_cast<const int16_t* >(p + 12);
        const uint32_t doff = *reinterpret_cast<const uint32_t*>(p + 14);
        const size_t need = (size_t)r.width * (size_t)r.height;
        if (doff + need > m_blob.size())
        {
            std::fprintf(stderr, "MapData: record %u tile data out of bounds\n", i);
            continue;
        }
        r.tiles = m_blob.data() + doff;
        m_index[{r.region_id, r.floor}] = r;
    }

}

const FloorRecord* MapData::Find(int region_id, const std::string& floor) const
{
    auto it = m_index.find({region_id, floor});
    return it == m_index.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------
// TilesetTexture
// ---------------------------------------------------------------------------

void TilesetTexture::EnsureTexture()
{
    if (m_tex && m_texColor) return;
    auto makeTex = [&]() {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        // MAG = NEAREST keeps the pixel-art look crisp when zoomed in
        // or at 1:1. MIN = trilinear over the mip chain so down-scaled
        // tiles get a pre-filtered sample at the correct LOD instead of
        // dropping texels — fixes the aliasing/moiré visible at low
        // zoom on the AutoMap. Mip levels are built CPU-side with
        // alpha-weighted RGB averaging (see Refresh) rather than via
        // glGenerateMipmap, because the GPU's box filter averages RGB
        // and alpha independently — for an atlas of opaque tiles on a
        // transparent background that double-attenuates intensity at
        // every mip level, making the map fade out as you zoom out.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Pre-allocate every mip level so Refresh can glTexSubImage2D
        // into each one without re-allocating storage every frame.
        int w = kAtlasW, h = kAtlasH;
        int level = 0;
        for (;;)
        {
            glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, w, h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            if (w == 1 && h == 1) break;
            w = (std::max)(1, w / 2);
            h = (std::max)(1, h / 2);
            ++level;
        }
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL,  level);
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    };
    if (!m_tex)      m_tex      = makeTex();
    if (!m_texColor) m_texColor = makeTex();
}

void TilesetTexture::DecodeTile(uint8_t id, const uint8_t* src)
{
    // 16 lines × 2 bytes × 7 pixels each (LSB = leftmost). Two atlases
    // are filled in lockstep: a monochrome one (white-on-transparent,
    // tinted at draw-time for the white/green/amber CRT modes) and an
    // Apple ][ HGR-coloured one for the "Color" mode.
    //
    // HGR colour rule (the simple version — no NTSC fringe / sub-pixel
    // shift): a lit pixel whose immediate left or right neighbour is
    // also lit renders as WHITE; otherwise its colour is picked from
    // its in-tile column parity plus the high bit of the byte it lives
    // in:
    //   high bit 0 → even col VIOLET, odd col GREEN
    //   high bit 1 → even col BLUE,   odd col ORANGE
    // Bits 6 and 8 (last pixel of byte 0, first pixel of byte 1) are
    // screen-adjacent so the white test crosses the byte boundary.
    constexpr uint8_t kBlack [4] = {   0,   0,   0,   0 };
    constexpr uint8_t kViolet[4] = { 255,  68, 253, 255 };
    constexpr uint8_t kGreen [4] = {  20, 245,  60, 255 };
    constexpr uint8_t kBlue  [4] = {  20, 207, 253, 255 };
    constexpr uint8_t kOrange[4] = { 255, 106,  60, 255 };
    constexpr uint8_t kWhiteM[4] = { 255, 255, 255, 255 };

    const int x0 = (id % kCols) * kTileW;
    const int y0 = (id / kCols) * kTileH;
    for (int y = 0; y < kTileH; ++y)
    {
        const uint8_t bytes[2] = { src[y * 2 + 0], src[y * 2 + 1] };
        bool    on[kTileW];
        bool    hi[kTileW];
        for (int col = 0; col < kTileW; ++col)
        {
            const int     bi  = col / 7;
            const int     bit = col % 7;
            const uint8_t b   = bytes[bi];
            on[col] = (b & (1 << bit)) != 0;
            hi[col] = (b & 0x80) != 0;
        }
        uint8_t* dstM = m_pixels      + ((size_t)(y0 + y) * kAtlasW + x0) * 4;
        uint8_t* dstC = m_pixelsColor + ((size_t)(y0 + y) * kAtlasW + x0) * 4;
        for (int col = 0; col < kTileW; ++col)
        {
            const uint8_t* mono = on[col] ? kWhiteM : kBlack;

            const uint8_t* color;
            if (!on[col])
                color = kBlack;
            else
            {
                const bool leftOn  = (col > 0)             && on[col - 1];
                const bool rightOn = (col < kTileW - 1)    && on[col + 1];
                if (leftOn || rightOn)
                    color = kWhiteM;
                else
                {
                    const bool even = ((col & 1) == 0);
                    color = hi[col] ? (even ? kBlue   : kOrange)
                                    : (even ? kViolet : kGreen);
                }
            }

            std::memcpy(dstM, mono,  4); dstM += 4;
            std::memcpy(dstC, color, 4); dstC += 4;
        }
    }
    m_has[id] = true;
}

void TilesetTexture::Refresh()
{
    if (!g_ramSnapshot.valid) return;
    EnsureTexture();

    // Static tiles: $7000 + N * $20 for N in 0..$7F.
    for (int id = 0; id <= 0x7F; ++id)
        DecodeTile((uint8_t)id, &g_ramSnapshot.aux[0x7000 + id * 0x20]);

    // Animated tiles: $8000 + (N - $80) * $80 for N in $80..$FF, each
    // 128 bytes laid out as 4 × 32-byte frames. Decode frame 0 only —
    // animation cycling is a TODO once we know how Nox tracks the
    // current frame.
    for (int id = 0x80; id <= 0xFF; ++id)
        DecodeTile((uint8_t)id, &g_ramSnapshot.aux[0x8000 + (id - 0x80) * 0x80]);

    // Build mip chain CPU-side with alpha-weighted RGB averaging. The
    // box filter weights each source pixel's RGB by its alpha so a
    // 2×2 region containing 1 lit pixel + 3 transparent ones produces
    // a fully-bright output texel at quarter alpha — instead of the
    // quarter-bright + quarter-alpha that the GPU's straight box
    // filter would yield (which compounds to a darker tile every level
    // when ImGui composites with standard alpha blend).
    auto uploadWithMips = [&](GLuint tex, const uint8_t* level0) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kAtlasW, kAtlasH,
                        GL_RGBA, GL_UNSIGNED_BYTE, level0);

        std::vector<uint8_t> src(level0, level0 + kAtlasW * kAtlasH * 4);
        std::vector<uint8_t> dst;
        int sw = kAtlasW, sh = kAtlasH;
        int level = 0;
        while (sw > 1 || sh > 1)
        {
            const int dw = (std::max)(1, sw / 2);
            const int dh = (std::max)(1, sh / 2);
            dst.assign((size_t)dw * dh * 4, 0);
            for (int y = 0; y < dh; ++y)
            {
                const int sy0 = (std::min)(y * 2,     sh - 1);
                const int sy1 = (std::min)(y * 2 + 1, sh - 1);
                for (int x = 0; x < dw; ++x)
                {
                    const int sx0 = (std::min)(x * 2,     sw - 1);
                    const int sx1 = (std::min)(x * 2 + 1, sw - 1);
                    const uint8_t* p00 = src.data() + (sy0 * sw + sx0) * 4;
                    const uint8_t* p01 = src.data() + (sy0 * sw + sx1) * 4;
                    const uint8_t* p10 = src.data() + (sy1 * sw + sx0) * 4;
                    const uint8_t* p11 = src.data() + (sy1 * sw + sx1) * 4;
                    const uint32_t aSum =
                        (uint32_t)p00[3] + p01[3] + p10[3] + p11[3];
                    uint8_t* d = dst.data() + (y * dw + x) * 4;
                    if (aSum == 0)
                        continue;          // fully transparent, leave zeros
                    const uint32_t r = p00[0]*p00[3] + p01[0]*p01[3]
                                     + p10[0]*p10[3] + p11[0]*p11[3];
                    const uint32_t g = p00[1]*p00[3] + p01[1]*p01[3]
                                     + p10[1]*p10[3] + p11[1]*p11[3];
                    const uint32_t b = p00[2]*p00[3] + p01[2]*p01[3]
                                     + p10[2]*p10[3] + p11[2]*p11[3];
                    d[0] = (uint8_t)(r / aSum);
                    d[1] = (uint8_t)(g / aSum);
                    d[2] = (uint8_t)(b / aSum);
                    d[3] = (uint8_t)(aSum / 4);
                }
            }
            ++level;
            glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, dw, dh,
                            GL_RGBA, GL_UNSIGNED_BYTE, dst.data());
            src.swap(dst);
            sw = dw;
            sh = dh;
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    };

    uploadWithMips(m_tex,      m_pixels);
    uploadWithMips(m_texColor, m_pixelsColor);
}

// ---------------------------------------------------------------------------
// TileMap
// ---------------------------------------------------------------------------

namespace
{

// Tiny inline base64 — same scheme the old fogofwar.json used so the
// switch to seen_tiles.json doesn't need a real codec dependency.
const std::string kB64 =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string EncodeB64(const std::vector<uint8_t>& bytes)
{
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    int val = 0, valb = -6;
    for (uint8_t c : bytes)
    {
        val = (val << 8) | c;
        valb += 8;
        while (valb >= 0)
        {
            out.push_back(kB64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(kB64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::vector<uint8_t> DecodeB64(const std::string& b64)
{
    std::vector<int> tab(256, -1);
    for (int i = 0; i < 64; ++i) tab[(uint8_t)kB64[i]] = i;
    std::vector<uint8_t> out;
    out.reserve(b64.size() * 3 / 4);
    int val = 0, valb = -8;
    for (char c : b64)
    {
        if (c == '=') break;
        if (tab[(uint8_t)c] < 0) continue;
        val = (val << 6) | tab[(uint8_t)c];
        valb += 6;
        if (valb >= 0) { out.push_back((uint8_t)((val >> valb) & 0xFF)); valb -= 8; }
    }
    return out;
}

} // namespace

void TileMap::Load(const std::filesystem::path& prefDir)
{
    m_path = prefDir / "seen_tiles.json";
    m_floors.clear();
    m_dirty = false;
    if (!std::filesystem::exists(m_path)) return;
    try
    {
        std::ifstream f(m_path);
        nlohmann::json j; f >> j;
        if (!j.is_array()) return;
        for (const auto& fjson : j)
        {
            // Skip pre-rc3 entries (keyed by region/floor, no map_id).
            if (!fjson.contains("map_id")) continue;
            const int mapID = fjson.value("map_id", -1);
            if (mapID < 0 || mapID > 255) continue;
            const int region = fjson.value("region_id", 0);
            const std::string floor = fjson.value("floor", std::string{});
            const std::string regionName = fjson.value("region_name", std::string{});
            const int w = fjson.value("w", 0);
            const int h = fjson.value("h", 0);
            const std::string b64 = fjson.value("tiles_b64", std::string{});
            if (w <= 0 || h <= 0) continue;
            Floor& fl = EnsureFloor((uint8_t)mapID, region, floor, regionName, w, h);
            const auto bytes = DecodeB64(b64);
            const size_t n = (std::min)(bytes.size(), (size_t)w * (size_t)h);
            std::memcpy(fl.tiles.data(), bytes.data(), n);
        }
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "TileMap: parse failed: %s\n", e.what());
    }
}

void TileMap::Save() const
{
    if (!m_dirty || m_path.empty()) return;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [mapID, fl] : m_floors)
    {
        arr.push_back({
            {"map_id",      (int)mapID},
            {"region_id",   fl.region_id},
            {"floor",       fl.floor},
            {"region_name", fl.region_name},
            {"w",           fl.width},
            {"h",           fl.height},
            {"tiles_b64",   EncodeB64(fl.tiles)},
        });
    }
    try
    {
        std::ofstream f(m_path);
        f << arr.dump(2);
        m_dirty = false;
    }
    catch (...) {}
}

TileMap::Floor& TileMap::EnsureFloor(uint8_t mapID,
                                     int region_id, const std::string& floor,
                                     const std::string& region_name,
                                     int floorWidth, int floorHeight)
{
    auto& fl = m_floors[mapID];
    if ((int)fl.tiles.size() != floorWidth * floorHeight)
    {
        fl.width  = floorWidth;
        fl.height = floorHeight;
        fl.tiles.assign((size_t)floorWidth * (size_t)floorHeight, 0);
    }
    // Refresh the labels in case the rule mapping changed (also lets
    // a freshly-observed mapID pick up the names from this Observe).
    fl.region_id   = region_id;
    fl.floor       = floor;
    fl.region_name = region_name;
    return fl;
}

void TileMap::Observe(uint8_t mapID,
                      int region_id, const std::string& floor,
                      const std::string& region_name,
                      int playerX, int playerY,
                      int floorWidth, int floorHeight,
                      const uint8_t* vis, const uint8_t* fog)
{
    if (!vis || !fog || floorWidth <= 0 || floorHeight <= 0) return;
    Floor& fl = EnsureFloor(mapID, region_id, floor, region_name,
                            floorWidth, floorHeight);

    // Nox's $0800 window is always centred on the avatar: vis[8][5] is
    // the tile under the player's feet even when the player stands one
    // tile from a map edge. Cells past the edge in the buffer fall
    // outside the floor bounds and the per-cell check below skips
    // them. No clamp here.
    const int x0 = playerX - kVisW / 2;
    const int y0 = playerY - kVisH / 2;

    for (int row = 0; row < kVisH; ++row)
    {
        const int y = y0 + row;
        if (y < 0 || y >= floorHeight) continue;
        for (int col = 0; col < kVisW; ++col)
        {
            const int x = x0 + col;
            if (x < 0 || x >= floorWidth) continue;
            const int idx = row * kVisW + col;
            if (fog[idx] != 0) continue;       // hidden — skip
            const uint8_t id = vis[idx];
            if (id == 0) continue;             // visible but empty buffer slot
            uint8_t& cell = fl.tiles[y * floorWidth + x];
            if (cell != id) { cell = id; m_dirty = true; }
        }
    }
}

uint8_t TileMap::TileAt(uint8_t mapID, int x, int y) const
{
    auto it = m_floors.find(mapID);
    if (it == m_floors.end()) return 0;
    const Floor& fl = it->second;
    if (x < 0 || x >= fl.width || y < 0 || y >= fl.height) return 0;
    return fl.tiles[y * fl.width + x];
}

bool TileMap::Dims(uint8_t mapID, int& width, int& height) const
{
    auto it = m_floors.find(mapID);
    if (it == m_floors.end()) return false;
    width  = it->second.width;
    height = it->second.height;
    return true;
}

std::vector<TileMap::Entry> TileMap::ObservedMaps() const
{
    std::vector<Entry> out;
    out.reserve(m_floors.size());
    for (const auto& [mapID, fl] : m_floors)
    {
        for (uint8_t b : fl.tiles)
            if (b != 0)
            {
                out.push_back({mapID, fl.region_id, fl.floor, fl.region_name});
                break;
            }
    }
    return out;
}

// ---------------------------------------------------------------------------
// MapAnnotations
// ---------------------------------------------------------------------------

namespace
{
const std::vector<MapNote> kEmptyNotes;
}

void MapAnnotations::Load(const std::filesystem::path& prefDir)
{
    m_path = prefDir / "annotations.json";
    m_notes.clear();
    m_dirty = false;
    if (!std::filesystem::exists(m_path)) return;
    try
    {
        std::ifstream f(m_path);
        nlohmann::json j; f >> j;
        if (!j.is_array()) return;
        for (const auto& nj : j)
        {
            const int mapID = nj.value("map_id", -1);
            if (mapID < 0 || mapID > 255) continue;
            MapNote n;
            n.x              = nj.value("x", 0);
            n.y              = nj.value("y", 0);
            n.text           = nj.value("text", std::string{});
            n.always_visible = nj.value("always_visible", false);
            if (n.text.empty()) continue;
            m_notes[(uint8_t)mapID].push_back(std::move(n));
        }
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "MapAnnotations: parse failed: %s\n", e.what());
    }
}

void MapAnnotations::Save() const
{
    if (!m_dirty || m_path.empty()) return;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [mapID, notes] : m_notes)
    {
        for (const auto& n : notes)
        {
            arr.push_back({
                {"map_id",         (int)mapID},
                {"x",              n.x},
                {"y",              n.y},
                {"text",           n.text},
                {"always_visible", n.always_visible},
            });
        }
    }
    try
    {
        std::ofstream f(m_path);
        f << arr.dump(2);
        m_dirty = false;
    }
    catch (...) {}
}

const MapNote* MapAnnotations::Find(uint8_t mapID, int x, int y) const
{
    auto it = m_notes.find(mapID);
    if (it == m_notes.end()) return nullptr;
    for (const auto& n : it->second)
        if (n.x == x && n.y == y) return &n;
    return nullptr;
}

void MapAnnotations::Set(uint8_t mapID, int x, int y,
                         std::string text, bool always_visible)
{
    if (text.empty()) { Erase(mapID, x, y); return; }
    auto& v = m_notes[mapID];
    for (auto& n : v)
        if (n.x == x && n.y == y)
        {
            n.text           = std::move(text);
            n.always_visible = always_visible;
            m_dirty = true;
            return;
        }
    v.push_back(MapNote{x, y, std::move(text), always_visible});
    m_dirty = true;
}

void MapAnnotations::Erase(uint8_t mapID, int x, int y)
{
    auto it = m_notes.find(mapID);
    if (it == m_notes.end()) return;
    auto& v = it->second;
    for (auto i = v.begin(); i != v.end(); ++i)
        if (i->x == x && i->y == y)
        {
            v.erase(i);
            m_dirty = true;
            if (v.empty()) m_notes.erase(it);
            return;
        }
}

const std::vector<MapNote>& MapAnnotations::NotesFor(uint8_t mapID) const
{
    auto it = m_notes.find(mapID);
    return (it == m_notes.end()) ? kEmptyNotes : it->second;
}

// ---------------------------------------------------------------------------
// MapPanel
// ---------------------------------------------------------------------------

void MapPanel::Render(const MapTranslator& tx, MapData& /*md*/,
                      TilesetTexture& tileset, TileMap& tiles,
                      MapAnnotations& notes)
{
    if (!m_open) return;

    ImGui::SetNextWindowSize(ImVec2(720, 540), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("AutoMap###Map", &m_open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    // Avatar tile X/Y live in main RAM at version-specific addresses
    // (1.3.7 sits one byte lower than 1.1.4 / 1.1.9). Versions.json
    // → cpuconstants resolves them; we read raw bytes here.
    const uint8_t mapID   = Peek(0x2AF9);
    const uint8_t xpos    = (uint8_t)Peek((uint16_t)cpuconstants.MEM_XPOS);
    const uint8_t ypos    = (uint8_t)Peek((uint16_t)cpuconstants.MEM_YPOS);
    const uint8_t mapType = Peek(kAddrMapType);

    ImGui::Text("mapID $%02X  X=%u  Y=%u  type $%02X (%s)",
                mapID, xpos, ypos, mapType, MapTypeName(mapType));

    // Live observation always runs, regardless of which floor the
    // panel happens to be rendering — switching the view pulldown to
    // a stored map shouldn't pause discovery on the live one. Skipped
    // on combat maps so battle tiles ($0800 repurposed) don't pollute
    // the overworld, and skipped before the player has a party so
    // boot / BASIC-prompt junk in $0800 doesn't paint into the map.
    auto liveLoc = tx.Resolve(mapID, xpos, ypos);
    if (liveLoc && g_ramSnapshot.valid && mapType != kMapTypeCombat && HasParty())
    {
        const int liveW = (std::max)(1, liveLoc->width);
        const int liveH = (std::max)(1, liveLoc->height);
        const uint8_t* vis = &g_ramSnapshot.main[0x0800];
        const uint8_t* fog = &g_ramSnapshot.main[0x08BB];
        tiles.Observe(mapID, liveLoc->region, liveLoc->floor,
                      liveLoc->region_name,
                      liveLoc->x, liveLoc->y, liveW, liveH, vis, fog);
    }

    // Map pulldown: "Live (auto)" plus every observed mapID. Multiple
    // mapIDs can share the same (region, floor) label (e.g. Bayport's
    // ground floor is four quadrant mapIDs), so the mapID hex suffix
    // disambiguates them.
    auto observed = tiles.ObservedMaps();
    {
        std::vector<std::string> labels;
        labels.reserve(observed.size() + 1);
        labels.emplace_back("Live (auto)");
        for (const auto& e : observed)
        {
            std::string name = e.region_name;
            if (name.empty()) name = std::to_string(e.region_id);
            char suffix[16];
            std::snprintf(suffix, sizeof(suffix), " ($%02X)", e.map_id);
            labels.push_back(name + " / " + e.floor + suffix);
        }
        int sel = 0;
        if (m_viewMapID >= 0)
        {
            for (size_t i = 0; i < observed.size(); ++i)
                if ((int)observed[i].map_id == m_viewMapID)
                { sel = (int)(i + 1); break; }
        }
        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::BeginCombo("Map", labels[sel].c_str()))
        {
            for (int i = 0; i < (int)labels.size(); ++i)
            {
                const bool s = (i == sel);
                if (ImGui::Selectable(labels[i].c_str(), s) && i != sel)
                {
                    if (i == 0) { m_viewMapID = -1; }
                    else
                    {
                        m_viewMapID = (int)observed[i - 1].map_id;
                        // Fresh stored-map view → frame the whole map
                        // and reset any previous pan from another map.
                        m_needFit = true;
                    }
                    m_panX = m_panY = 0.0f;
                }
                if (s) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    // CRT-style monochrome tints + a "Color" mode using the baked
    // Apple ][ HGR-coloured atlas.
    {
        const char* csNames[] = { "White", "Green", "Amber", "Color" };
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo("##cs", &m_colorScheme_int, csNames, IM_ARRAYSIZE(csNames));
        if (m_colorScheme_int < 0 || m_colorScheme_int > CS_Color)
            m_colorScheme_int = (int)CS_Green;
    }
    const ColorScheme colorScheme = (ColorScheme)m_colorScheme_int;

    // Resolve the map we're actually rendering — live or stored.
    uint8_t     renderMapID = 0;
    int         renderW = 0, renderH = 0;
    float       centreTileX = 0.0f, centreTileY = 0.0f;
    bool        showAvatar = false;
    const bool  liveView = (m_viewMapID < 0);

    if (liveView)
    {
        if (!liveLoc)
        {
            ImGui::TextDisabled("(no rule matched - likely on the BASIC prompt)");
            ImGui::End();
            return;
        }
        renderMapID  = mapID;
        renderW      = (std::max)(1, liveLoc->width);
        renderH      = (std::max)(1, liveLoc->height);
        centreTileX  = (float)liveLoc->x;
        centreTileY  = (float)liveLoc->y;
        showAvatar   = true;

        ImGui::Text("Region %d (%s) - Floor %s - Tile %d, %d",
                    liveLoc->region, liveLoc->region_name.c_str(),
                    liveLoc->floor.c_str(), liveLoc->x, liveLoc->y);
    }
    else
    {
        renderMapID = (uint8_t)m_viewMapID;
        if (!tiles.Dims(renderMapID, renderW, renderH))
        {
            // The stored entry was removed under us — snap back to live.
            m_viewMapID = -1;
            ImGui::End();
            return;
        }
        if (renderW <= 0) renderW = 1;
        if (renderH <= 0) renderH = 1;
        // Stored view starts framed on the floor's centre; m_panX/Y
        // shifts that as the user drags inside the canvas.
        centreTileX = renderW * 0.5f + m_panX;
        centreTileY = renderH * 0.5f + m_panY;

        // Pick the human label off the matching ObservedMaps entry.
        std::string regionName, floorLabel;
        int regionId = 0;
        for (const auto& e : observed)
        {
            if (e.map_id == renderMapID)
            {
                regionName = e.region_name;
                floorLabel = e.floor;
                regionId   = e.region_id;
                break;
            }
        }
        ImGui::Text("Region %d (%s) - Floor %s - mapID $%02X - stored",
                    regionId, regionName.c_str(),
                    floorLabel.c_str(), renderMapID);
    }

    ImGui::Separator();

    static float s_zoom = 1.0f;
    ImGui::SetNextItemWidth(140.0f);
    ImGui::InputFloat("Zoom", &s_zoom, 0.05f, 0.25f, "%.2fx");
    s_zoom = (std::max)(0.05f, (std::min)(8.0f, s_zoom));
    ImGui::SameLine();
    const bool fitClicked = ImGui::Button("Fit");

    // Pull fresh tile bitmaps out of aux RAM ($7000 / $8000) before
    // drawing — Nox patches the shape tables when entering a new
    // dungeon / town, so the atlas tracks whatever's currently loaded.
    tileset.Refresh();

    // Avatar-centred render. No scroll, no canvas larger than the
    // visible region — we just compute where the avatar would sit at
    // the centre of the panel and offset every tile relative to it.
    // For a stored-map view we centre on the floor's middle plus pan,
    // and let the user drag inside the canvas to move that pan.
    ImGui::BeginChild("##map_canvas", ImVec2(0, 0),
                      ImGuiChildFlags_Borders);

    constexpr float kTileW = (float)TilesetTexture::kTileW;   // 14
    constexpr float kTileH = (float)TilesetTexture::kTileH;   // 16
    constexpr float kUvW   = 1.0f / TilesetTexture::kCols;
    constexpr float kUvH   = 1.0f / TilesetTexture::kRows;

    // GetContentRegionAvail rather than GetWindowSize: the latter
    // includes the child's borders / padding, so sizing the
    // InvisibleButton from it overshoots the usable area by a few
    // pixels and the child grows a vertical scrollbar.
    const ImVec2 view = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    // Cover the canvas with an InvisibleButton so left-drag inside it
    // is consumed as an item interaction and never bubbles up to the
    // ImGui window-move handler — without this, dragging on the map
    // also drags the whole "Map" window around. Tile drawing below
    // uses absolute screen coords from `origin` so the button advancing
    // the cursor doesn't matter.
    ImGui::InvisibleButton("##map_canvas_hit", view);
    const bool canvasActive  = ImGui::IsItemActive();
    const bool canvasHovered = ImGui::IsItemHovered();
    const bool canvasRClick  = canvasHovered &&
                               ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    // One-shot zoom-to-fit. Auto-fires the first frame after the user
    // picks a stored map (m_needFit was set in the combo handler) and
    // also when they explicitly click Fit. Live view is never auto-fit
    // — it would yank the user's chosen zoom out from under them.
    if (fitClicked || (m_needFit && !liveView))
    {
        const float zx = view.x / (renderW * kTileW);
        const float zy = view.y / (renderH * kTileH);
        s_zoom = (std::max)(0.05f, (std::min)(zx, zy));
        m_needFit = false;
    }

    // Pan: drag inside the canvas (stored-map view only — live view
    // stays locked to the avatar).
    if (!liveView && canvasActive &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        const ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        m_panX -= d.x / (kTileW * s_zoom);
        m_panY -= d.y / (kTileH * s_zoom);
        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        centreTileX = renderW * 0.5f + m_panX;
        centreTileY = renderH * 0.5f + m_panY;
    }

    const float centreX = origin.x + view.x * 0.5f;
    const float centreY = origin.y + view.y * 0.5f;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 fogCol = IM_COL32(20, 20, 24, 255);

    dl->AddRectFilled(origin,
                      ImVec2(origin.x + view.x, origin.y + view.y),
                      fogCol);

    // CS_Color samples the Apple ][ HGR-coloured atlas with a white
    // tint; the monochrome modes draw the mono atlas with a flat
    // phosphor-style tint.
    ImU32 tint = IM_COL32_WHITE;
    switch (colorScheme)
    {
    case CS_White: tint = IM_COL32(235, 235, 235, 255); break;
    case CS_Green: tint = IM_COL32( 80, 255, 120, 255); break;
    case CS_Amber: tint = IM_COL32(255, 180,  50, 255); break;
    case CS_Color: tint = IM_COL32_WHITE;               break;
    }
    const unsigned glTex = (colorScheme == CS_Color)
                         ? tileset.ColorTex()
                         : tileset.Tex();
    const auto tileTexId = static_cast<ImTextureID>((uintptr_t)glTex);

    const int spanX = (int)(view.x / (kTileW * s_zoom)) / 2 + 2;
    const int spanY = (int)(view.y / (kTileH * s_zoom)) / 2 + 2;
    const int x0 = (std::max)(0,           (int)std::floor(centreTileX) - spanX);
    const int x1 = (std::min)(renderW - 1, (int)std::ceil (centreTileX) + spanX);
    const int y0 = (std::max)(0,           (int)std::floor(centreTileY) - spanY);
    const int y1 = (std::min)(renderH - 1, (int)std::ceil (centreTileY) + spanY);

    for (int ty = y0; ty <= y1; ++ty)
    {
        for (int tx_ = x0; tx_ <= x1; ++tx_)
        {
            const uint8_t id = tiles.TileAt(renderMapID, tx_, ty);
            if (id == 0) continue;
            const float px0 = centreX + (tx_ - centreTileX - 0.5f) * kTileW * s_zoom;
            const float py0 = centreY + (ty  - centreTileY - 0.5f) * kTileH * s_zoom;
            const ImVec2 p0(px0, py0);
            const ImVec2 p1(px0 + kTileW * s_zoom, py0 + kTileH * s_zoom);
            if (glTex && tileset.Has(id))
            {
                const ImVec2 uv0((id % TilesetTexture::kCols) * kUvW,
                                 (id / TilesetTexture::kCols) * kUvH);
                const ImVec2 uv1(uv0.x + kUvW, uv0.y + kUvH);
                dl->AddImage(tileTexId, p0, p1, uv0, uv1, tint);
            }
            else
            {
                dl->AddRectFilled(p0, p1, tint);
            }
        }
    }

    if (showAvatar)
    {
        const ImVec2 c(centreX, centreY);
        const float r = (std::max)(3.0f, kTileW * s_zoom * 0.45f);
        dl->AddCircleFilled(c, r,        IM_COL32(255, 60, 60, 255));
        dl->AddCircle      (c, r + 1.5f, IM_COL32(0, 0, 0, 255), 0, 1.5f);
    }

    // ----- annotations: markers, always-visible labels, hover tooltips ---

    auto tileToScreen = [&](int tx_, int ty) {
        const float px0 = centreX + (tx_ - centreTileX - 0.5f) * kTileW * s_zoom;
        const float py0 = centreY + (ty  - centreTileY - 0.5f) * kTileH * s_zoom;
        return ImVec2(px0, py0);
    };

    const ImU32 markerCol  = IM_COL32(255, 215,  80, 255);
    const ImU32 markerEdge = IM_COL32( 30,  20,   0, 255);
    const ImU32 labelBgCol = IM_COL32(  0,   0,   0, 180);
    const ImU32 labelTxtCol = IM_COL32(255, 235, 180, 255);

    const ImVec2 mouse = ImGui::GetMousePos();
    const char* hoverNoteText = nullptr;

    for (const auto& n : notes.NotesFor(renderMapID))
    {
        if (n.x < 0 || n.x >= renderW || n.y < 0 || n.y >= renderH) continue;
        const ImVec2 tp0 = tileToScreen(n.x, n.y);
        const ImVec2 tp1(tp0.x + kTileW * s_zoom, tp0.y + kTileH * s_zoom);

        // Cull clearly-offscreen notes so a large grid of pins doesn't
        // cost ImDrawList calls per frame for unseen tiles.
        if (tp1.x < origin.x || tp0.x > origin.x + view.x ||
            tp1.y < origin.y || tp0.y > origin.y + view.y)
            continue;

        // Upward-pointing filled triangle, centred on the tile centre.
        const float cx = (tp0.x + tp1.x) * 0.5f;
        const float cy = (tp0.y + tp1.y) * 0.5f;
        const float sz = (std::max)(4.0f, kTileW * s_zoom * 0.45f);
        const ImVec2 a(cx,             cy - sz * 0.6f);    // top
        const ImVec2 b(cx - sz * 0.5f, cy + sz * 0.4f);    // bottom-left
        const ImVec2 c(cx + sz * 0.5f, cy + sz * 0.4f);    // bottom-right
        dl->AddTriangleFilled(a, b, c, markerCol);
        dl->AddTriangle      (a, b, c, markerEdge, 1.0f);

        if (n.always_visible && !n.text.empty())
        {
            // Split on '\n' and lay out a multi-line label centred on
            // the tile centre, with each line centred within the pill.
            // The pill background is sized to the widest line.
            std::vector<std::pair<const char*, const char*>> lines;
            const char* p   = n.text.c_str();
            const char* end = p + n.text.size();
            while (p <= end)
            {
                const char* nl = (p < end) ? std::strchr(p, '\n') : nullptr;
                const char* lineEnd = nl ? nl : end;
                lines.push_back({p, lineEnd});
                if (!nl) break;
                p = nl + 1;
            }

            const float lineH = ImGui::GetTextLineHeight();
            float maxW = 0.0f;
            for (const auto& [a, b] : lines)
            {
                const float w = ImGui::CalcTextSize(a, b).x;
                if (w > maxW) maxW = w;
            }

            const float pad     = 4.0f;
            const float labelW  = maxW + pad * 2.0f;
            const float labelH  = lineH * (float)lines.size() + pad * 2.0f;
            const ImVec2 lp0(cx - labelW * 0.5f, cy - labelH * 0.5f);
            const ImVec2 lp1(lp0.x + labelW,     lp0.y + labelH);
            dl->AddRectFilled(lp0, lp1, labelBgCol, 3.0f);
            dl->AddRect      (lp0, lp1, markerEdge, 3.0f);

            for (size_t i = 0; i < lines.size(); ++i)
            {
                const float lw = ImGui::CalcTextSize(lines[i].first,
                                                     lines[i].second).x;
                dl->AddText(ImVec2(cx - lw * 0.5f,
                                   lp0.y + pad + lineH * (float)i),
                            labelTxtCol,
                            lines[i].first, lines[i].second);
            }
        }

        if (mouse.x >= tp0.x && mouse.x < tp1.x &&
            mouse.y >= tp0.y && mouse.y < tp1.y &&
            canvasHovered)
            hoverNoteText = n.text.c_str();
    }

    if (hoverNoteText)
    {
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(hoverNoteText);
        ImGui::EndTooltip();
    }

    // Right-click on the canvas → open the note editor for the clicked
    // tile. Compute the tile by inverting the on-screen tile transform.
    if (canvasRClick)
    {
        const float fx = (mouse.x - centreX) / (kTileW * s_zoom)
                       + centreTileX + 0.5f;
        const float fy = (mouse.y - centreY) / (kTileH * s_zoom)
                       + centreTileY + 0.5f;
        const int tx_ = (int)std::floor(fx);
        const int ty  = (int)std::floor(fy);
        if (tx_ >= 0 && tx_ < renderW && ty >= 0 && ty < renderH)
        {
            m_noteEditMapID = renderMapID;
            m_noteEditX     = tx_;
            m_noteEditY     = ty;
            if (const MapNote* existing = notes.Find(renderMapID, tx_, ty))
            {
                std::snprintf(m_noteEditBuf, sizeof(m_noteEditBuf),
                              "%s", existing->text.c_str());
                m_noteEditAlwaysVisible = existing->always_visible;
            }
            else
            {
                m_noteEditBuf[0]        = 0;
                m_noteEditAlwaysVisible = false;
            }
            m_noteEditOpen = true;
        }
    }

    if (m_noteEditOpen)
    {
        ImGui::OpenPopup("##note_editor");
        m_noteEditOpen = false;
    }

    ImGui::SetNextWindowSize(ImVec2(420, 220), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("##note_editor", nullptr,
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoSavedSettings))
    {
        ImGui::Text("Note at (%d, %d) — map $%02X",
                    m_noteEditX, m_noteEditY, m_noteEditMapID);
        ImGui::Separator();
        ImGui::InputTextMultiline("##note_text", m_noteEditBuf,
                                  sizeof(m_noteEditBuf),
                                  ImVec2(-FLT_MIN,
                                         ImGui::GetTextLineHeight() * 4));
        ImGui::Checkbox("Always show on map", &m_noteEditAlwaysVisible);
        ImGui::Spacing();

        if (ImGui::Button("Save", ImVec2(80, 0)))
        {
            notes.Set(m_noteEditMapID, m_noteEditX, m_noteEditY,
                      std::string(m_noteEditBuf), m_noteEditAlwaysVisible);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete", ImVec2(80, 0)))
        {
            notes.Erase(m_noteEditMapID, m_noteEditX, m_noteEditY);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // Discoverability hint, anchored to the bottom-left of the canvas.
    // Half-opaque so it doesn't compete with the map content.
    {
        const char* hint = "R-CLK to annotate";
        const float th = ImGui::GetTextLineHeight();
        const ImVec2 pos(origin.x + 6.0f,
                         origin.y + view.y - th - 6.0f);
        dl->AddText(ImVec2(pos.x + 1.0f, pos.y + 1.0f),
                    IM_COL32(0, 0, 0, 128), hint);
        dl->AddText(pos, IM_COL32(255, 255, 255, 128), hint);
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace nac
