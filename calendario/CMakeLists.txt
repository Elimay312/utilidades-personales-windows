cmake_minimum_required(VERSION 3.28)

# vcpkg in manifest mode when VCPKG_ROOT exists; FetchContent covers the machines where it
# does not, so the presets in CMakePresets.json work out of the box either way.
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  cmake_path(SET agenda_vcpkg NORMALIZE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  set(CMAKE_TOOLCHAIN_FILE "${agenda_vcpkg}" CACHE FILEPATH "vcpkg toolchain")
endif()

project(Agenda VERSION 0.1.0 LANGUAGES CXX)

if(NOT MSVC)
  message(FATAL_ERROR "Agenda solo se compila con MSVC x64 (Visual Studio 2022).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CLAUDE.md documenta build\debug\Agenda.exe, asi que el binario va a la raiz del build dir.
foreach(agenda_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${agenda_cfg} "${CMAKE_BINARY_DIR}")
endforeach()

find_package(nlohmann_json 3 CONFIG QUIET)
find_package(Catch2 3 CONFIG QUIET)

include(FetchContent)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(nlohmann_json)
endif()
if(NOT Catch2_FOUND)
  FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(Catch2)
endif()

set(AGENDA_WARNINGS /W4 /permissive- /utf-8)

add_executable(Agenda WIN32
  src/app/main.cpp
  src/app/monitors.cpp
  src/core/config.cpp
  src/core/log.cpp
  assets/agenda.rc)
target_include_directories(Agenda PRIVATE src)
target_compile_options(Agenda PRIVATE ${AGENDA_WARNINGS})
target_compile_definitions(Agenda PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(Agenda PRIVATE nlohmann_json::nlohmann_json)
# El manifiesto (DPI Per-Monitor v2) entra por agenda.rc, no por el generado por el linker.
target_link_options(Agenda PRIVATE /MANIFEST:NO)

enable_testing()
add_executable(agenda_tests tests/test_args.cpp src/app/monitors.cpp)
target_include_directories(agenda_tests PRIVATE src)
target_compile_options(agenda_tests PRIVATE ${AGENDA_WARNINGS})
target_compile_definitions(agenda_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(agenda_tests PRIVATE Catch2::Catch2WithMain)
add_test(NAME args COMMAND agenda_tests)
