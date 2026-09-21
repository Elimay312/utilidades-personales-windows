cmake_minimum_required(VERSION 3.25)
project(rayo LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "Rayo solo se compila con MSVC x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CRT estatico: el .exe no depende de redistribuibles.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# /GL + /LTCG en Release.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

include(FetchContent)
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        v1.92.9b
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(imgui)

# ImGui aparte, con su propio nivel de warnings: /W4 sobre imgui no seria limpio.
add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(imgui PUBLIC
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends)
target_compile_definitions(imgui PUBLIC
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  WINVER=0x0A00 _WIN32_WINNT=0x0A00
  IMGUI_DISABLE_OBSOLETE_FUNCTIONS
  # Sin esto ImWchar es de 16 bits y todo lo que pase de U+FFFF (los emoji) se
  # sustituye por U+FFFD antes de llegar a la fuente.
  IMGUI_USE_WCHAR32)
target_compile_options(imgui PRIVATE /utf-8 /EHsc)
target_link_libraries(imgui PUBLIC d3d11 dxgi)

add_executable(rayo WIN32
  src/main.cpp
  src/app/App.cpp
  src/core/TaskPool.cpp
  src/fs/DirectoryReader.cpp
  src/platform/Window.cpp
  src/platform/GraphicsDevice.cpp
  src/ui/Keymap.cpp
  src/ui/MillerView.cpp
  src/ui/Theme.cpp
  src/rayo.manifest)
target_include_directories(rayo PRIVATE src)
target_compile_options(rayo PRIVATE /W4 /permissive- /utf-8 /EHsc)
# shlwapi: StrCmpLogicalW (orden natural) y StrFormatByteSizeW.
target_link_libraries(rayo PRIVATE imgui dwmapi shlwapi)
target_link_options(rayo PRIVATE /ENTRY:wWinMainCRTStartup)
