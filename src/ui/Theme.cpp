#include "ui/Theme.h"

namespace Theme {

void Apply(float dpiScale) {
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle();  // partir de cero: Apply se repite en cada cambio de DPI

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text]           = kText;
    c[ImGuiCol_TextDisabled]   = kTextDim;
    c[ImGuiCol_WindowBg]       = kPanel;
    c[ImGuiCol_ChildBg]        = kPanel;
    c[ImGuiCol_PopupBg]        = kPanel;
    c[ImGuiCol_Border]         = kBackground;
    c[ImGuiCol_BorderShadow]   = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]        = kBackground;
    c[ImGuiCol_FrameBgHovered] = kSelection;
    c[ImGuiCol_FrameBgActive]  = kSelection;
    c[ImGuiCol_TitleBg]        = kBackground;
    c[ImGuiCol_TitleBgActive]  = kBackground;
    c[ImGuiCol_ScrollbarBg]    = kPanel;
    c[ImGuiCol_ScrollbarGrab]  = kBackground;
    c[ImGuiCol_CheckMark]      = kAccent;
    c[ImGuiCol_SliderGrab]     = kAccent;
    c[ImGuiCol_Button]         = kBackground;
    c[ImGuiCol_ButtonHovered]  = kSelection;
    c[ImGuiCol_ButtonActive]   = kSelection;
    c[ImGuiCol_Header]         = kSelection;
    c[ImGuiCol_HeaderHovered]  = kSelection;
    c[ImGuiCol_HeaderActive]   = kSelection;
    c[ImGuiCol_Separator]      = kBackground;
    c[ImGuiCol_ResizeGrip]     = ImVec4(0, 0, 0, 0);

    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 2.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(6.0f, 3.0f);
    style.ScrollbarSize = 12.0f;
    style.ScaleAllSizes(dpiScale);
}

void LoadFont(float dpiScale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    // Tamano entero: media pixel de diferencia y el texto pequeno se emborrona.
    const float size = static_cast<float>(static_cast<int>(16.0f * dpiScale + 0.5f));
    if (!io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", size))
        io.Fonts->AddFontDefault();
}

}  // namespace Theme
