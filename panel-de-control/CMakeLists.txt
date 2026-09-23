cmake_minimum_required(VERSION 3.28)

project(Panel VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  # x64 only: an x86 process on x64 Windows gets an empty list from Radio::GetRadiosAsync.
  message(FATAL_ERROR "Panel solo se compila con MSVC x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static CRT, like Brujula and Rayo: one exe to copy, no redistributable to install.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

foreach(panel_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${panel_cfg} "${CMAKE_BINARY_DIR}")
endforeach()

# The cmake on this machine's PATH (WinLibs) ships no certificate store, so an HTTPS download
# dies with "SSL certificate verification failed". Git for Windows' bundle works and is on every
# machine these projects are built on (see calendario/CMakeLists.txt).
if(NOT DEFINED CMAKE_TLS_CAINFO AND NOT DEFINED ENV{CURL_CA_BUNDLE})
  foreach(panel_ca_raw "$ENV{ProgramFiles}/Git/usr/ssl/certs/ca-bundle.crt"
                       "$ENV{ProgramW6432}/Git/usr/ssl/certs/ca-bundle.crt")
    file(TO_CMAKE_PATH "${panel_ca_raw}" panel_ca)
    if(EXISTS "${panel_ca}")
      set(CMAKE_TLS_CAINFO "${panel_ca}")
      break()
    endif()
  endforeach()
endif()

# Both pinned by SHA-256 (SEGURIDAD.md 1.8): a tag can be moved, a hash cannot. Upgrading means
# changing the URL and the hash together.
include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
FetchContent_Declare(doctest
  URL      https://github.com/doctest/doctest/archive/refs/tags/v2.5.3.tar.gz
  URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json doctest)

set(PANEL_WARNINGS /W4 /permissive- /utf-8 /EHsc)
set(PANEL_DEFINES UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(Panel WIN32
  src/main.cpp
  src/core/config.cpp
  src/core/log.cpp
  src/core/options.cpp
  src/model/sample.cpp
  src/ui/paint.cpp
  src/ui/panel_view.cpp
  src/ui/panel_window.cpp
  src/ui/snapshot.cpp
  src/ui/theme.cpp
  src/ui/vsync.cpp
  assets/panel.rc)
target_include_directories(Panel PRIVATE src)
target_compile_options(Panel PRIVATE ${PANEL_WARNINGS})
target_compile_definitions(Panel PRIVATE ${PANEL_DEFINES})
target_link_libraries(Panel PRIVATE nlohmann_json::nlohmann_json
  d2d1 d3d11 dxgi dcomp dwmapi dwrite windowscodecs shcore)
# The manifest (PerMonitorV2, asInvoker) comes in through panel.rc.
target_link_options(Panel PRIVATE /MANIFEST:NO)

enable_testing()
add_executable(panel_tests
  tests/main.cpp
  tests/test_controls.cpp
  tests/test_hotkey.cpp
  tests/test_layout.cpp
  tests/test_options.cpp
  src/core/log.cpp
  src/core/options.cpp)
target_include_directories(panel_tests PRIVATE src)
target_compile_options(panel_tests PRIVATE ${PANEL_WARNINGS})
target_compile_definitions(panel_tests PRIVATE ${PANEL_DEFINES})
target_link_libraries(panel_tests PRIVATE doctest::doctest shcore)
add_test(NAME unit COMMAND panel_tests)
