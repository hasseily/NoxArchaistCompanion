#include "Emulator/StdAfx.h"

#include "Templates.h"

#include "Emulator/CPU.h"
#include "Emulator/Memory.h"
#include "RamSnapshot.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <fstream>

namespace nac
{

namespace
{

ImVec4 ParseColor(const nlohmann::json& block,
                  ImVec4 fallback = ImVec4(0.85f, 0.85f, 0.85f, 1.0f))
{
    if (!block.contains("color")) return fallback;
    const auto& c = block["color"];
    if (!c.is_array() || c.size() < 3) return fallback;
    return ImVec4(c[0].get<float>(), c[1].get<float>(), c[2].get<float>(),
                  c.size() >= 4 ? c[3].get<float>() : 1.0f);
}

// All Nox-state reads come from the PC_PRINTSTR-latched RAM snapshot
// (see RamSnapshot.h). The snapshot is captured at a known-stable
// CPU phase, so multi-byte fields (xpos/ypos pairs, party stats, name
// strings) never appear half-written.
bool ReadByte(uint32_t off, uint8_t& out)
{
    if (!g_ramSnapshot.valid)  return false;
    if (off < 0x10000u)        { out = g_ramSnapshot.main[off];               return true; }
    if (off < 0x20000u)        { out = g_ramSnapshot.aux [off - 0x10000u];    return true; }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// TemplateRegistry
// ---------------------------------------------------------------------------

void TemplateRegistry::Load(const std::filesystem::path& assetsDir)
{
    m_templates.clear();
    m_tables = nlohmann::json::object();

    const auto tablesPath = assetsDir / "nox-tables.json";
    if (std::filesystem::exists(tablesPath))
    {
        try
        {
            std::ifstream f(tablesPath);
            f >> m_tables;
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "Templates: failed to load nox-tables.json: %s\n", e.what());
        }
    }

    const auto templatesDir = assetsDir / "Templates";
    if (!std::filesystem::exists(templatesDir))
    {
        std::fprintf(stderr, "Templates: directory not found: %s\n",
                     templatesDir.string().c_str());
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(templatesDir))
    {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        try
        {
            std::ifstream f(entry.path());
            nlohmann::json j;
            f >> j;

            WindowTemplate t;
            t.name = j.value("menu_name", entry.path().stem().string());
            t.kind = j.value("kind",      std::string("party"));
            t.blocks = j.value("blocks",  nlohmann::json::array());
            if (j.contains("default_size") && j["default_size"].is_array() &&
                j["default_size"].size() >= 2)
            {
                t.default_w = j["default_size"][0].get<float>();
                t.default_h = j["default_size"][1].get<float>();
            }
            m_templates[t.name] = std::move(t);
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "Templates: parse failed for %s: %s\n",
                         entry.path().string().c_str(), e.what());
        }
    }
}

const WindowTemplate* TemplateRegistry::Find(const std::string& name) const
{
    auto it = m_templates.find(name);
    return it == m_templates.end() ? nullptr : &it->second;
}

std::vector<std::string> TemplateRegistry::Names() const
{
    std::vector<std::string> out;
    out.reserve(m_templates.size());
    for (const auto& kv : m_templates) out.push_back(kv.first);
    return out;
}

// ---------------------------------------------------------------------------
// TemplateInstance
// ---------------------------------------------------------------------------

TemplateInstance::TemplateInstance(const TemplateRegistry& registry,
                                   const std::string&      templateName,
                                   int                     instanceId,
                                   int                     memberIndex)
    : m_registry(registry)
    , m_templateName(templateName)
    , m_instanceId(instanceId)
    , m_memberIndex(memberIndex)
{
    m_template = m_registry.Find(templateName);
}

std::string TemplateInstance::SerializeVar(const nlohmann::json& var) const
{
    if (!var.is_object())                                              return {};
    if (!var.contains("memstart") || !var["memstart"].is_string())     return {};
    if (!var.contains("type")     || !var["type"].is_string())         return {};
    if (!var.contains("length")   || !var["length"].is_number_integer()) return {};

    const int length = var["length"].get<int>();
    if (length <= 0 || length > 256) return {};

    uint32_t memoffset = 0;
    try { memoffset = std::stoul(var["memstart"].get<std::string>(), nullptr, 0); }
    catch (...) { return {}; }

    // Character-scope vars: memstart is an offset into the selected
    // member's 0x80-byte party slot.
    const std::string scope = var.value("scope", std::string());
    if (scope == "character")
    {
        if (cpuconstants.MEM_PARTY == 0) return "?";
        memoffset += cpuconstants.MEM_PARTY + (uint32_t)m_memberIndex * 0x80u;
    }

    std::vector<uint8_t> buf(length);
    for (int i = 0; i < length; ++i)
        if (!ReadByte(memoffset + (uint32_t)i, buf[i])) return {};

    const uint8_t* p = buf.data();
    const std::string type = var["type"].get<std::string>();

    if (type == "ascii")
    {
        std::string s;
        for (int i = 0; i < length && p[i]; ++i) s.push_back((char)p[i]);
        return s;
    }
    if (type == "ascii_high")
    {
        std::string s;
        for (int i = 0; i < length && p[i]; ++i) s.push_back((char)(p[i] & 0x7F));
        return s;
    }
    if (type == "int_bigendian")
    {
        uint64_t x = 0, mul = 1;
        for (int i = 0; i < length; ++i, mul *= 0x100) x += p[i] * mul;
        return std::to_string(x);
    }
    if (type == "int_littleendian")
    {
        uint64_t x = 0;
        for (int i = 0; i < length; ++i) x = x * 0x100 + p[i];
        return std::to_string(x);
    }
    if (type == "int_bigendian_literal")
    {
        std::string s;
        char b[3];
        for (int i = 0; i < length; ++i)
        {
            std::snprintf(b, sizeof(b), "%02x", p[i]);
            s.insert(0, b);
        }
        return s;
    }
    if (type == "int_littleendian_literal")
    {
        std::string s;
        char b[3];
        for (int i = 0; i < length; ++i)
        {
            std::snprintf(b, sizeof(b), "%02x", p[i]);
            s.append(b);
        }
        return s;
    }
    if (type == "lookup")
    {
        if (!var.contains("lookup") || !var["lookup"].is_string()) return {};
        char keyBuf[16];
        std::snprintf(keyBuf, sizeof(keyBuf), "/0x%02x", (int)p[0]);
        try
        {
            const std::string ptr = "/" + var["lookup"].get<std::string>() + keyBuf;
            return m_registry.Tables().value(nlohmann::json::json_pointer{ptr},
                                             std::string("-"));
        }
        catch (...) { return "-"; }
    }
    return {};
}

std::string TemplateInstance::FormatBlockText(int blockKey, const nlohmann::json& block)
{
    std::string fresh;
    try
    {
        fresh = block.value("template", std::string{});
        if (block.contains("vars") && block["vars"].is_array())
        {
            const std::string placeholder = "{}";
            for (const auto& v : block["vars"])
            {
                const auto pos = fresh.find(placeholder);
                if (pos == std::string::npos) break;
                fresh.replace(pos, placeholder.size(), SerializeVar(v));
            }
        }
    }
    catch (...) { fresh = "<format error>"; }

    BlockCache& c = m_blockCache[blockKey];
    if (fresh == c.displayed)
    {
        c.pending.clear();
        c.pendingCount = 0;
        return c.displayed;
    }
    if (fresh == c.pending)
    {
        if (++c.pendingCount >= kStableFrames)
        {
            c.displayed = std::move(c.pending);
            c.pending.clear();
            c.pendingCount = 0;
        }
    }
    else
    {
        c.pending      = std::move(fresh);
        c.pendingCount = 1;
    }
    return c.displayed;
}

void TemplateInstance::Render()
{
    if (!m_open || !m_template) return;

    // Stable per-instance ID so ImGui persists position+size separately
    // per window. The triple-hash form means ImGui hashes only the
    // "###..." suffix for the window ID; the visible title comes from
    // the template name. Character windows show the per-member name in
    // their own combo below, so we don't repeat it in the title.
    char title[160];
    std::snprintf(title, sizeof(title), "%s###nac_tpl_%d",
                  m_template->name.c_str(), m_instanceId);

    ImGui::SetNextWindowSize(ImVec2(m_template->default_w, m_template->default_h),
                             ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(title, &m_open, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    // Character windows get a member-picker combo at the top. Names come
    // from RAM, so an empty slot shows "(empty)" instead of nothing.
    if (m_template->kind == "character" && cpuconstants.MEM_PARTY != 0)
    {
        char  names[6][17] = {};
        const char* items[6] = {};
        for (int k = 0; k < 6; ++k)
        {
            const uint32_t base = cpuconstants.MEM_PARTY + (uint32_t)k * 0x80u + 0x4Bu;
            for (int i = 0; i < 16; ++i)
            {
                uint8_t b;
                if (!ReadByte(base + (uint32_t)i, b)) break;
                if (b == 0) break;
                names[k][i] = (char)(b & 0x7F);
            }
            items[k] = names[k][0] ? names[k] : "(empty)";
        }
        if (m_memberIndex < 0 || m_memberIndex > 5) m_memberIndex = 0;
        if (ImGui::Combo("##member", &m_memberIndex, items, 6))
            m_blockCache.clear();   // values now belong to a different slot
        ImGui::Separator();
    }

    int blockIndex = 0;
    for (const auto& block : m_template->blocks)
    {
        const std::string type = block.value("type", std::string("Content"));
        if (type == "Empty")
        {
            ImGui::Spacing();
            ++blockIndex;
            continue;
        }
        const int    blockKey = blockIndex++;
        const std::string text  = FormatBlockText(blockKey, block);
        const ImVec4      color = ParseColor(block);

        if (type == "Header")
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            if (ImGui::CalcTextSize(text.c_str()).x <=
                ImGui::GetContentRegionAvail().x)
            {
                ImGui::SeparatorText(text.c_str());
            }
            else
            {
                ImGui::TextWrapped("%s", text.c_str());
                ImGui::Separator();
            }
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", text.c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::End();
}

} // namespace nac
