#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nac
{

// JSON-driven window templates. Each template defines one ImGui window's
// blocks (Header / Content / Empty) with template strings and `vars` that
// read live from emulator main / aux RAM. Two kinds:
//
//   * "character" — vars carry `scope: "character"` and their `memstart`
//     is interpreted as an offset into the selected party member's
//     0x80-byte slot (base = cpuconstants.MEM_PARTY + member * 0x80).
//   * "party" — vars use absolute addresses; no member selection.
//
// A TemplateRegistry loads templates and the shared lookup-table file
// once at boot. TemplateInstance is the runtime object per open window;
// the user can spawn arbitrarily many of any template (e.g. six
// Character (full) windows for six members). ImGui persists position +
// size per instance via a stable window ID; nac.json persists the list
// of open instances + each character instance's selected member.

struct WindowTemplate
{
    std::string    name;            // e.g. "Character (full)"
    std::string    kind;            // "character" or "party"
    nlohmann::json blocks;           // array of block objects
    float          default_w = 240.f;
    float          default_h = 320.f;
};

class TemplateRegistry
{
public:
    // Load all templates from <assetsDir>/Templates/*.json and the
    // lookup tables from <assetsDir>/nox-tables.json.
    void Load(const std::filesystem::path& assetsDir);

    const WindowTemplate* Find(const std::string& name) const;
    std::vector<std::string> Names() const;
    const nlohmann::json& Tables() const { return m_tables; }

private:
    std::map<std::string, WindowTemplate> m_templates;
    nlohmann::json                        m_tables;
};

class TemplateInstance
{
public:
    TemplateInstance(const TemplateRegistry& registry,
                     const std::string&      templateName,
                     int                     instanceId,
                     int                     memberIndex = 0);

    void Render();

    bool        IsOpen()       const { return m_open; }
    bool&       OpenRef()            { return m_open; }
    int         InstanceId()   const { return m_instanceId; }
    int         MemberIndex()  const { return m_memberIndex; }
    const std::string& TemplateName() const { return m_templateName; }

private:
    std::string SerializeVar(const nlohmann::json& var) const;
    std::string FormatBlockText(int blockKey, const nlohmann::json& block);

    // Per-block debounce — same scheme the old Sidebar used: a freshly-
    // formatted text only replaces the displayed text after kStableFrames
    // consecutive identical samples. The //e mid-updates a buffer or flips
    // STORE80 inside our iterate window, so the raw read flickers; the
    // debounce hides that without blocking real changes (~67 ms latency).
    static constexpr int kStableFrames = 4;
    struct BlockCache
    {
        std::string pending;
        int         pendingCount = 0;
        std::string displayed;
    };

    const TemplateRegistry&             m_registry;
    const WindowTemplate*               m_template     = nullptr;
    std::string                         m_templateName;
    int                                 m_instanceId   = 0;
    int                                 m_memberIndex  = 0;       // character only
    bool                                m_open         = true;
    std::unordered_map<int, BlockCache> m_blockCache;
};

} // namespace nac
