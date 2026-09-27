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
  src/system/drop.cpp
  src/system/icons.cpp
  src/system/inventory.cpp
  src/system/jumplist.cpp
  src/system/launch.cpp
  src/system/steam.cpp
  src/system/worker.cpp
  src/ui/dock_window.cpp
  src/ui/genie.cpp
  src/ui/stack.cpp
  src/ui/text.cpp
  src/ui/visuals.cpp
  src/ui/vsync.cpp
  assets/dock.rc)
target_include_directories(Dock PRIVATE src)
target_compile_options(Dock PRIVATE ${DOCK_WARNINGS})
target_compile_definitions(Dock PRIVATE ${DOCK_DEFINES})
# windowsapp y CoreMessaging: Windows.UI.Composition vía C++/WinRT y su DispatcherQueue.
target_link_libraries(Dock PRIVATE nlohmann_json::nlohmann_json shcore dwmapi windowsapp CoreMessaging
  # d3d11/d2d1/dxgi: el device WARP que sube los píxeles a superficies. dwrite: las etiquetas.
  # shlwapi: AssocQueryString, la app que abre una URL (su icono).
  d3d11 d2d1 dxgi dwrite shlwapi)
# El manifiesto (PerMonitorV2, asInvoker) entra por dock.rc.
target_link_options(Dock PRIVATE /MANIFEST:NO)

# Instalar-Dock.exe: el instalador de Panel (panel-de-control/CMakeLists.txt), un fichero con
# Dock.exe dentro como RCDATA. Dock.exe sale en la raíz de la carpeta de build en todas las
# configuraciones, así que su ruta se sabe al configurar y llega al .rc por una cabecera
# generada. Instala por usuario en %LOCALAPPDATA%\Programs\Dock y se queda como Desinstalar.exe.
set(dock_exe "${CMAKE_BINARY_DIR}/Dock.exe")
set(dock_assets "${CMAKE_CURRENT_SOURCE_DIR}/assets")
configure_file(src/installer/installer_config.h.in "${CMAKE_BINARY_DIR}/installer/installer_config.h" @ONLY)
add_executable(dock_installer WIN32 src/installer/installer.cpp src/installer/installer.rc)
set_target_properties(dock_installer PROPERTIES OUTPUT_NAME "Instalar-Dock")
add_dependencies(dock_installer Dock)
target_include_directories(dock_installer PRIVATE "${CMAKE_BINARY_DIR}/installer")
target_compile_options(dock_installer PRIVATE ${DOCK_WARNINGS})
target_compile_definitions(dock_installer PRIVATE ${DOCK_DEFINES})
# comctl32: el TaskDialog. El manifiesto (common controls v6) entra por installer.rc.
target_link_libraries(dock_installer PRIVATE comctl32)
target_link_options(dock_installer PRIVATE /MANIFEST:NO)
