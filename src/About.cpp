#include "About.h"

#include "ImGuiHelpers.h"
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
    ui::CenterNextWindowOnMainViewport();
    const float popupWidth = ui::PopupWidthForText(
        { "A desktop companion for the Apple //e game Nox Archaist.",
          "Nox Archaist FONT1 and a2sharp bitmap fonts",
          "https://github.com/hasseily/NoxArchaistCompanion",
          "Visit the Nox Archaist Discord for support" },
        420.0f);
    ImGui::SetNextWindowSize(ImVec2(popupWidth, 0.0f),
                             ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("About NAC", nullptr,
                               ImGuiWindowFlags_NoResize |
                               ImGuiWindowFlags_NoSavedSettings))
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
        ui::BulletTextWrapped("AppleWin emulator (GPL2)");
        ui::BulletTextWrapped("SDL3");
        ui::BulletTextWrapped("Dear ImGui (docking)");
        ui::BulletTextWrapped(
            "Nox Archaist FONT1 and a2sharp bitmap fonts");
        ui::BulletTextWrapped("stb_image, nlohmann/json");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextWrapped(
            "https://github.com/hasseily/NoxArchaistCompanion");
        ImGui::Separator();
        ImGui::TextWrapped(
            "Visit the Nox Archaist Discord for support");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace nac
