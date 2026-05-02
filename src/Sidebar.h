#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace nac
{

// Manages the JSON sidebar profiles (under Resources/Profiles/) and renders
// them as ImGui windows attached to the edges of the host window. Each
// profile defines one or more sidebars (Left / Right / Top / Bottom), each
// holding ordered Header / Content / Empty blocks. Content blocks have a
// template ("Hits: {}") and a list of `vars` that read from emulator main
// RAM each frame.
class Sidebar
{
public:
    // Walk profileDir (typically Resources/Profiles/) recursively and
    // load every *.json found. Profile name comes from "meta.name".
    void LoadProfilesFromDir(const std::filesystem::path& profileDir);

    // Switch the active profile. Returns false if no profile by that name
    // is loaded. Pass empty string to clear.
    bool SetActiveProfile(const std::string& name);

    // List of all loaded profile names (for the picker UI).
    std::vector<std::string> ProfileNames() const;
    const std::string& ActiveProfileName() const { return m_activeName; }

    // Per-frame: render the active profile's sidebars as ImGui windows
    // anchored to the host window edges. Reads emulator memory on each
    // call to keep displayed values live.
    void Render();

private:
    std::string SerializeVar(const nlohmann::json& var) const;
    std::string FormatBlockText(const nlohmann::json& block) const;
    void DrawSidebar(const nlohmann::json& sidebar, const char* anchor);

    std::map<std::string, nlohmann::json> m_profiles;
    std::string                           m_activeName;
    nlohmann::json                        m_active;   // copy of m_profiles[m_activeName]
};

} // namespace nac
