#include "Emulator/StdAfx.h"

#include "Sidebar.h"

#include "Emulator/Memory.h"

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <fstream>

namespace nac
{

namespace
{

constexpr float kSideWidth   = 220.0f;
constexpr float kStripHeight = 110.0f;

ImVec4 ParseColor(const nlohmann::json& block, ImVec4 fallback = ImVec4(0.85f, 0.85f, 0.85f, 1.0f))
{
    if (!block.contains("color")) return fallback;
    const auto& c = block["color"];
    if (!c.is_array() || c.size() < 3) return fallback;
    return ImVec4(c[0].get<float>(),
                  c[1].get<float>(),
                  c[2].get<float>(),
                  c.size() >= 4 ? c[3].get<float>() : 1.0f);
}

} // namespace

void Sidebar::LoadProfilesFromDir(const std::filesystem::path& profileDir)
{
    m_profiles.clear();
    if (!std::filesystem::exists(profileDir))
    {
        std::fprintf(stderr, "Sidebar: profile dir not found: %s\n",
                     profileDir.string().c_str());
        return;
    }

    for (const auto& entry : std::filesystem::recursive_directory_iterator(profileDir))
    {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        try
        {
            std::ifstream f(entry.path());
            nlohmann::json j;
            f >> j;

            // Display key: "<version-dir> / <profile-name>" — same JSON
            // profile name appears in every version directory, so prefix
            // with the directory path relative to profileDir to keep them
            // distinct in the picker.
            const std::string baseName = j.value("/meta/name"_json_pointer, std::string{});
            if (baseName.empty()) continue;

            const auto rel    = std::filesystem::relative(entry.path(), profileDir);
            const auto parent = rel.parent_path();
            const std::string key = parent.empty()
                ? baseName
                : parent.string() + " / " + baseName;
            m_profiles[key] = std::move(j);
        }
        catch (const std::exception& e)
        {
            std::fprintf(stderr, "Sidebar: parse failed for %s: %s\n",
                         entry.path().string().c_str(), e.what());
        }
    }
}

bool Sidebar::SetActiveProfile(const std::string& name)
{
    if (name.empty())
    {
        m_activeName.clear();
        m_active = nlohmann::json{};
        return true;
    }
    auto it = m_profiles.find(name);
    if (it == m_profiles.end()) return false;
    m_activeName = name;
    m_active     = it->second;
    return true;
}

std::vector<std::string> Sidebar::ProfileNames() const
{
    std::vector<std::string> names;
    names.reserve(m_profiles.size());
    for (const auto& kv : m_profiles) names.push_back(kv.first);
    return names;
}

std::string Sidebar::SerializeVar(const nlohmann::json& var) const
{
    if (!var.is_object())                                              return {};
    if (!var.contains("memstart") || !var["memstart"].is_string())     return {};
    if (!var.contains("type")     || !var["type"].is_string())         return {};
    if (!var.contains("length")   || !var["length"].is_number_integer())return {};

    const int length = var["length"].get<int>();
    if (length <= 0 || length > 256) return {};

    uint32_t memoffset = 0;
    try { memoffset = std::stoul(var["memstart"].get<std::string>(), nullptr, 0); }
    catch (...) { return {}; }

    // The NAC profile convention treats memory as one contiguous 128 KiB
    // block: 0x00000-0x0FFFF = main RAM, 0x10000-0x1FFFF = aux RAM. The
    // upstream emulator stores the two halves in separate allocations,
    // so dispatch by offset.
    const uint8_t* p = nullptr;
    if (memoffset + (uint32_t)length <= 0x10000u)
    {
        const uint8_t* mem = reinterpret_cast<const uint8_t*>(MemGetMainPtr(0));
        if (!mem) return {};
        p = mem + memoffset;
    }
    else if (memoffset >= 0x10000u && memoffset + (uint32_t)length <= 0x20000u)
    {
        const uint8_t* aux = reinterpret_cast<const uint8_t*>(MemGetAuxPtr(0));
        if (!aux) return {};
        p = aux + (memoffset - 0x10000u);
    }
    else
    {
        return {};   // straddling the main/aux boundary — invalid
    }

    const std::string type = var["type"].get<std::string>();

    if (type == "ascii")
    {
        std::string s;
        s.reserve(length);
        for (int i = 0; i < length && p[i]; ++i) s.push_back((char)p[i]);
        return s;
    }
    if (type == "ascii_high")
    {
        std::string s;
        s.reserve(length);
        for (int i = 0; i < length && p[i]; ++i) s.push_back((char)(p[i] & 0x7F));
        return s;
    }
    if (type == "int_bigendian")
    {
        // memstart is the low byte, then increasing addresses are higher significance
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
        // Bytes are BCD-ish; treat each as two hex digits, low byte first.
        std::string s;
        char buf[3];
        for (int i = 0; i < length; ++i)
        {
            std::snprintf(buf, sizeof(buf), "%02x", p[i]);
            s.insert(0, buf);
        }
        return s;
    }
    if (type == "int_littleendian_literal")
    {
        std::string s;
        char buf[3];
        for (int i = 0; i < length; ++i)
        {
            std::snprintf(buf, sizeof(buf), "%02x", p[i]);
            s.append(buf);
        }
        return s;
    }
    if (type == "lookup")
    {
        if (!var.contains("lookup") || !var["lookup"].is_string()) return {};
        const int x = p[0];
        char keyBuf[16];
        std::snprintf(keyBuf, sizeof(keyBuf), "/0x%02x", x);
        try
        {
            const std::string ptr = var["lookup"].get<std::string>() + keyBuf;
            return m_active.value(nlohmann::json::json_pointer{ptr}, std::string("-"));
        }
        catch (...) { return "-"; }
    }
    return {};
}

// Wrap the whole thing in a try/catch — JSON / stoul / pointer construction
// can all throw on profile fields that don't match expectations.
namespace { struct SerializeVarCatch {}; }

std::string Sidebar::FormatBlockText(const nlohmann::json& block) const
{
    try
    {
        std::string text = block.value("template", std::string{});
        if (!block.contains("vars") || !block["vars"].is_array()) return text;

        const std::string placeholder = "{}";
        for (const auto& v : block["vars"])
        {
            const auto pos = text.find(placeholder);
            if (pos == std::string::npos) break;
            text.replace(pos, placeholder.size(), SerializeVar(v));
        }
        return text;
    }
    catch (...)
    {
        return "<format error>";
    }
}

void Sidebar::DrawSidebar(const nlohmann::json& sidebar, const char* anchor)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float menuH = ImGui::GetFrameHeight();   // top main-menu bar

    ImVec2 pos, size;
    if (std::strcmp(anchor, "Right") == 0)
    {
        size = ImVec2(kSideWidth, vp->WorkSize.y - menuH);
        pos  = ImVec2(vp->WorkPos.x + vp->WorkSize.x - size.x, vp->WorkPos.y + menuH);
    }
    else if (std::strcmp(anchor, "Left") == 0)
    {
        size = ImVec2(kSideWidth, vp->WorkSize.y - menuH);
        pos  = ImVec2(vp->WorkPos.x, vp->WorkPos.y + menuH);
    }
    else if (std::strcmp(anchor, "Top") == 0)
    {
        size = ImVec2(vp->WorkSize.x, kStripHeight);
        pos  = ImVec2(vp->WorkPos.x, vp->WorkPos.y + menuH);
    }
    else // Bottom
    {
        size = ImVec2(vp->WorkSize.x, kStripHeight);
        pos  = ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - size.y);
    }

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);

    char title[128];
    std::snprintf(title, sizeof(title), "##nac_sidebar_%s", anchor);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (!ImGui::Begin(title, nullptr, flags))
    {
        ImGui::End();
        return;
    }

    if (!sidebar.contains("blocks"))
    {
        ImGui::End();
        return;
    }

    for (const auto& block : sidebar["blocks"])
    {
        const std::string type = block.value("type", std::string("Content"));
        if (type == "Empty")
        {
            ImGui::Spacing();
            continue;
        }
        const std::string text  = FormatBlockText(block);
        const ImVec4      color = ParseColor(block);

        if (type == "Header")
        {
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::SeparatorText(text.c_str());
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

void Sidebar::Render()
{
    if (m_active.empty() || !m_active.contains("sidebars")) return;
    if (!m_active["sidebars"].is_array()) return;
    try
    {
        for (const auto& sb : m_active["sidebars"])
        {
            const std::string anchor = sb.value("type", std::string("Right"));
            DrawSidebar(sb, anchor.c_str());
        }
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "Sidebar::Render exception: %s\n", e.what());
    }
}

} // namespace nac
