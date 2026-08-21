#pragma once

#include <imgui.h>

#include <initializer_list>

namespace nac::ui
{

// Width helpers for building rows that adapt to the active interface font.
// Dear ImGui does not wrap a SameLine() row automatically, so fixed rows that
// fit the old font can otherwise run past the right edge with FONT1/a2sharp.
inline float ButtonWidth(const char* label)
{
    return ImGui::CalcTextSize(label, nullptr, true).x +
           ImGui::GetStyle().FramePadding.x * 2.0f;
}

inline float CheckableWidth(const char* label)
{
    return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
           ImGui::CalcTextSize(label, nullptr, true).x;
}

inline float LabeledItemWidth(float itemWidth, const char* label)
{
    const float labelWidth = ImGui::CalcTextSize(label, nullptr, true).x;
    return itemWidth + (labelWidth > 0.0f
        ? ImGui::GetStyle().ItemInnerSpacing.x + labelWidth
        : 0.0f);
}

inline bool SameLineIfFits(float nextItemWidth)
{
    // After an item, the cursor has advanced to the start of the next line.
    // Its screen X plus the remaining content width is therefore the current
    // content area's right edge, including docking/column constraints.
    const float rightEdge = ImGui::GetCursorScreenPos().x +
                            ImGui::GetContentRegionAvail().x;
    const float nextLeft = ImGui::GetItemRectMax().x +
                           ImGui::GetStyle().ItemSpacing.x;
    if (nextLeft + nextItemWidth > rightEdge) return false;
    ImGui::SameLine();
    return true;
}

// Pick a popup width from the active font's actual glyph metrics, capped to
// the current viewport so a long sentence wraps instead of leaving the screen.
inline float PopupWidthForText(std::initializer_list<const char*> lines,
                               float minimumWidth)
{
    float widest = 0.0f;
    for (const char* line : lines)
    {
        if (!line) continue;
        const float width = ImGui::CalcTextSize(line).x;
        if (width > widest) widest = width;
    }

    float desired = widest + ImGui::GetStyle().WindowPadding.x * 2.0f + 2.0f;
    if (desired < minimumWidth) desired = minimumWidth;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport)
    {
        constexpr float kViewportMargin = 16.0f;
        const float maximum = viewport->WorkSize.x - kViewportMargin * 2.0f;
        if (maximum > 0.0f && desired > maximum) desired = maximum;
    }
    return desired;
}

inline void CenterNextWindowOnMainViewport()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) return;
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
}

inline float WrappedTextExtraHeight(const char* text, float windowWidth)
{
    float wrapWidth = windowWidth -
                      ImGui::GetStyle().WindowPadding.x * 2.0f;
    if (wrapWidth < 1.0f) wrapWidth = 1.0f;
    const float wrapped = ImGui::CalcTextSize(
        text, nullptr, false, wrapWidth).y;
    const float oneLine = ImGui::GetTextLineHeight();
    return wrapped > oneLine ? wrapped - oneLine : 0.0f;
}

inline void CenteredTextWrapped(const char* text)
{
    const float available = ImGui::GetContentRegionAvail().x;
    const float textWidth = ImGui::CalcTextSize(text).x;
    if (textWidth <= available)
    {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (available - textWidth) * 0.5f);
        ImGui::TextUnformatted(text);
        return;
    }

    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + available);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

inline void TextDisabledWrapped(const char* text)
{
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextDisabled("%s", text);
    ImGui::PopTextWrapPos();
}

inline void BulletTextWrapped(const char* text)
{
    ImGui::Bullet();
    ImGui::SameLine();
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

} // namespace nac::ui
