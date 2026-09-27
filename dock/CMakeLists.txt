cmake_minimum_required(VERSION 3.28)

project(Dock VERSION 2.0.0 LANGUAGES CXX)

if(NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "El dock solo se compila con MSVC x64.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# CRT estático, como Panel, Brújula y Rayo: un exe que copiar y ningún redistribuible.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

foreach(dock_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${dock_cfg} "${CMAKE_BINARY_DIR}")
endforeach()

# El cmake del PATH en esta máquina (WinLibs) no trae almacén de certificados y una descarga
# HTTPS muere con "SSL certificate verification failed". El de Git for Windows sí funciona y
# está en todos los equipos donde se compila esto (ver calendario/CMakeLists.txt).
if(NOT DEFINED CMAKE_TLS_CAINFO AND NOT DEFINED ENV{CURL_CA_BUNDLE})
  foreach(dock_ca_raw "$ENV{ProgramFiles}/Git/usr/ssl/certs/ca-bundle.crt"
                      "$ENV{ProgramW6432}/Git/usr/ssl/certs/ca-bundle.crt")
    file(TO_CMAKE_PATH "${dock_ca_raw}" dock_ca)
    if(EXISTS "${dock_ca}")
      set(CMAKE_TLS_CAINFO "${dock_ca}")
      break()
    endif()
  endforeach()
endif()

# Fijada por SHA-256, como en Panel: un tag se puede mover, un hash no. Subir de versión es
# cambiar la URL y el hash a la vez.
include(FetchContent)
FetchContent_Declare(nlohmann_json
  URL      https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
FetchContent_MakeAvailable(nlohmann_json)

# /external: las cabeceras de C++/WinRT del SDK no son nuestras, y /W4 sobre ellas es ruido.
set(DOCK_WARNINGS /W4 /permissive- /utf-8 /EHsc /external:anglebrackets /external:W0)
set(DOCK_DEFINES UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(Dock WIN32
  src/main.cpp
  src/app.cpp
  src/check.cpp
  src/core/jsonc.cpp
  src/core/log.cpp
  src/model/config.cpp
  src/system/appbar.cpp
  src/ui/dock_window.cpp
  assets/dock.rc)
target_include_directories(Dock PRIVATE src)
target_compile_options(Dock PRIVATE ${DOCK_WARNINGS})
target_compile_definitions(Dock PRIVATE ${DOCK_DEFINES})
target_link_libraries(Dock PRIVATE nlohmann_json::nlohmann_json shcore dwmapi)
# El manifiesto (PerMonitorV2, asInvoker) entra por dock.rc.
target_link_options(Dock PRIVATE /MANIFEST:NO)
