#include "About.h"

#include "Version.h"

#include <imgui.h>

namespace nac
{

void AboutPanel::Render()
{
    if (m_pendingOpen)
    {
        ImGui::OpenPopup("About NAC");
        m_pendingOpen = false;
    }

    // Centre the popup the first time it's shown.
    const ImVec2 centre = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(centre, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("About NAC", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Nox Archaist Companion");
        ImGui::Text("Version %s", NAC_VERSION_STRING);
        ImGui::Spacing();
        ImGui::TextWrapped(
            "A desktop companion for the Apple //e game Nox Archaist.");
        ImGui::Separator();
        ImGui::TextUnformatted("(c) Rikkles - MIT License");
        ImGui::Spacing();
        ImGui::TextUnformatted("Built with:");
        ImGui::BulletText("AppleWin emulator (GPL2)");
        ImGui::BulletText("SDL3");
        ImGui::BulletText("Dear ImGui (docking)");
        ImGui::BulletText("Nox Archaist FONT1 and a2sharp bitmap fonts");
        ImGui::BulletText("stb_image, nlohmann/json");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextUnformatted(
            "https://github.com/hasseily/NoxArchaistCompanion");
        ImGui::Separator();
        ImGui::TextUnformatted(
            "Visit the Nox Archaist Discord for support");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace nac
