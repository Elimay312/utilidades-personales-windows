#include "ui/Theme.h"

#include <Windows.h>

#include <limits>
#include <string>
#include <vector>

namespace Theme {
namespace {

// Las fuentes CJK pesan decenas de MB. Leerlas con AddFontFromFileTTF serian ~35 MB de
// heap y se cargaria el presupuesto de <50 MB. Mapeadas en memoria solo entran en el
// working set las paginas de los glifos que se usan de verdad, y son paginas limpias.
struct MappedFont {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    void* data = nullptr;
    size_t size = 0;
};

// Viven lo que viva el proceso: ImGui rasteriza glifos nuevos en cualquier momento.
std::vector<MappedFont> g_mapped;

std::wstring FontPath(const wchar_t* file) {
    wchar_t dir[MAX_PATH];
    const UINT length = GetWindowsDirectoryW(dir, MAX_PATH);
    // Barras normales: Win32 las acepta y evitan un muro de escapes.
    std::wstring path = (length > 0 && length < MAX_PATH) ? std::wstring(dir, length)
                                                          : std::wstring(L"C:/Windows");
    path += L"/Fonts/";
    path += file;
    return path;
}

bool MapFont(const wchar_t* path, MappedFont& out) {
    out.file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out.file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (GetFileSizeEx(out.file, &size) && size.QuadPart > 0 &&
        size.QuadPart <= std::numeric_limits<int>::max()) {
        out.mapping = CreateFileMappingW(out.file, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (out.mapping) {
            out.data = MapViewOfFile(out.mapping, FILE_MAP_READ, 0, 0, 0);
            out.size = static_cast<size_t>(size.QuadPart);
        }
    }

    if (!out.data) {
        if (out.mapping) CloseHandle(out.mapping);
        CloseHandle(out.file);
        out = MappedFont{};
        return false;
    }
    return true;
}

// Una fuente que no esta se salta en silencio (Malgun Gothic es opcional en algunas SKU):
// sus glifos saldran como caja, pero la app arranca igual.
bool AddFont(const wchar_t* file, float sizePixels, bool merge) {
    MappedFont mapped;
    if (!MapFont(FontPath(file).c_str(), mapped)) return false;

    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;  // el atlas no debe liberar una vista mapeada
    config.MergeMode = merge;
    if (!ImGui::GetIO().Fonts->AddFontFromMemoryTTF(mapped.data, static_cast<int>(mapped.size),
                                                    sizePixels, &config)) {
        UnmapViewOfFile(mapped.data);
        CloseHandle(mapped.mapping);
        CloseHandle(mapped.file);
        return false;
    }
    g_mapped.push_back(mapped);
    return true;
}

}  // namespace

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
    // 1.92: el atlas es dinamico, los glifos se rasterizan al tamano que toque.
    style.FontScaleDpi = dpiScale;
}

void LoadFont() {
    constexpr float kBaseSize = 16.0f;  // el DPI lo aplica style.FontScaleDpi

    // El orden es la prioridad: gana la primera fuente que tiene el glifo.
    if (!AddFont(L"segoeui.ttf", kBaseSize, false)) {
        ImGui::GetIO().Fonts->AddFontDefault();
        return;
    }
    AddFont(L"seguisym.ttf", kBaseSize, true);  // simbolos
    AddFont(L"seguiemj.ttf", kBaseSize, true);  // emoji
    AddFont(L"msyh.ttc", kBaseSize, true);      // CJK
    AddFont(L"malgun.ttf", kBaseSize, true);    // hangul
}

}  // namespace Theme
