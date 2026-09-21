cmake_minimum_required(VERSION 3.25)

# C además de CXX: la amalgamación de SQLite es C, y sin declararlo CMake no sabe con qué
# enlazar ese objetivo.
project(brujula LANGUAGES C CXX)

if(NOT MSVC)
  message(FATAL_ERROR "Brújula solo se compila con MSVC x64. Ejecuta preparar.ps1.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# CRT estático: el .exe no depende de redistribuibles. C++/WinRT lo admite sin nada
# especial, porque sus proyecciones son cabeceras y la única DLL que hay detrás
# (combase) es del sistema.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
# /GL + /LTCG en Release.
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# Los mismos flags en los cuatro objetivos. Se aplican PRIVATE y por objetivo, nunca
# globales, para poder darle a SQLite un juego más flojo sin tocar el nuestro.
#
# /external:env:INCLUDE marca como ajenas las cabeceras del CRT y del SDK —incluidas las
# proyecciones de C++/WinRT, que VsDevCmd ya pone en %INCLUDE%— y /external:W0 las calla.
# Sin esto, /W4 sobre winrt/base.h llena la salida de ruido que no podemos arreglar.
set(BRUJULA_FLAGS /W4 /permissive- /utf-8 /EHsc /await:strict /external:env:INCLUDE /external:W0)
set(BRUJULA_DEFS  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX WINVER=0x0A00 _WIN32_WINNT=0x0A00)

# --- Dependencias ------------------------------------------------------------------

include(FetchContent)

# SYSTEM en las de solo cabecera: es el equivalente de /external para lo que llega por
# -I, y evita tener que silenciar warnings uno a uno en código que no es nuestro.
FetchContent_Declare(doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG        v2.5.3
  GIT_SHALLOW    TRUE
  SYSTEM)

FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.12.0
  GIT_SHALLOW    TRUE
  SYSTEM)
set(JSON_BuildTests OFF CACHE INTERNAL "")

# La amalgamación, no el repositorio: el árbol completo de SQLite son cientos de megas y
# un sistema de compilación propio para construir exactamente este archivo.
#
# Fijada a la estable que listaba sqlite.org el 2026-09-21, con su hash. Al subir de
# versión se cambian las dos cosas juntas, y el año de la URL también: sqlite.org archiva
# las descargas por año y una versión de 2026 no vive en /2025/.
FetchContent_Declare(sqlite3
  URL      https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
  URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032)

FetchContent_MakeAvailable(doctest nlohmann_json sqlite3)

# SQLite aparte y con su propio nivel de warnings: /W4 sobre la amalgamación no sale
# limpio y no vamos a parchearla ni a silenciar warnings en nuestro código. Es la misma
# maniobra que Rayo hace con ImGui.
add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(sqlite3 SYSTEM PUBLIC ${sqlite3_SOURCE_DIR})
target_compile_options(sqlite3 PRIVATE /utf-8)
target_compile_definitions(sqlite3 PRIVATE
  # Sin carga de extensiones: Brújula guarda notas del trabajo y no tiene ningún motivo
  # para ejecutar código de un .dll que llegue dentro de un archivo de base de datos.
  SQLITE_OMIT_LOAD_EXTENSION=1
  SQLITE_DQS=0                      # "así" es una cadena, nunca un nombre de columna
  SQLITE_THREADSAFE=1               # el hilo de sincronización y el de UI la comparten
  SQLITE_DEFAULT_FOREIGN_KEYS=1
  SQLITE_ENABLE_FTS5=1)             # buscar entre novedades, a partir de la fase 4

# --- El núcleo puro ----------------------------------------------------------------

# Aquí solo entra lo que no necesita ni Win32 ni WinRT para decidir: geometría de la
# barra de título, tabla de colores, muelles y conversión de DPI. Que sea una biblioteca
# aparte es la regla 2 de arquitectura hecha estructura — y es lo que permite que
# brujula_tests.exe no enlace una sola línea de interfaz.
add_library(brujula_core STATIC
  src/shell/Caption.cpp
  src/shell/Theme.cpp
  src/compositor/MotionSpec.cpp)
target_include_directories(brujula_core PUBLIC src)
target_compile_options(brujula_core PRIVATE ${BRUJULA_FLAGS})
target_compile_definitions(brujula_core PRIVATE ${BRUJULA_DEFS})

# --- La aplicación -----------------------------------------------------------------

add_executable(brujula WIN32
  src/main.cpp
  src/app/App.cpp
  src/shell/Window.cpp
  src/shell/Backdrop.cpp
  src/shell/ThemeWatcher.cpp
  src/compositor/Scene.cpp
  src/compositor/Device.cpp
  src/compositor/Layer.cpp
  src/compositor/Surface.cpp
  src/compositor/Motion.cpp
  src/compositor/Morph.cpp
  src/ui/Text.cpp
  src/views/Demo.cpp
  src/brujula.rc
  # El manifiesto va suelto, no dentro del .rc, para que link.exe lo funda con el
  # trustInfo que genera él. Con los dos habría dos RT_MANIFEST y haría falta
  # /MANIFEST:NO. Se comprueba con: mt.exe -inputresource:brujula.exe;#1 -out:...
  src/brujula.manifest)

target_compile_options(brujula PRIVATE ${BRUJULA_FLAGS})
target_compile_definitions(brujula PRIVATE ${BRUJULA_DEFS})
target_link_libraries(brujula PRIVATE
  brujula_core
  # dwmapi: Mica, esquinas redondeadas, modo oscuro del marco y DwmExtendFrameIntoClientArea.
  dwmapi
  # windowsapp: el paraguas de WinRT. De aquí salen el Compositor, UISettings y
  # CreateDispatcherQueueController.
  windowsapp
  # d3d11 + dxgi: el dispositivo que comparten Direct2D y Composition. Tiene que ser uno
  # solo, o DWM no puede muestrear nuestras superficies.
  d3d11 dxgi
  # d2d1 + dwrite: todo lo que se dibuja dentro de la ventana.
  d2d1 dwrite)
# wWinMain sin WinMain: el CRT necesita que se lo digan.
target_link_options(brujula PRIVATE /ENTRY:wWinMainCRTStartup)

# --- Las pruebas -------------------------------------------------------------------

# Consola y con asserts, aunque sea Release: comprueban la geometría de la barra de
# título, la tabla de colores y los muelles, que son justo lo que no se ve mal en
# pantalla hasta que ya ha decidido mal. Enlazan nlohmann y SQLite aunque todavía no se
# usen: así la cadena entera queda probada en la fase que puede permitirse que falle.
add_executable(brujula_tests
  tests/main.cpp
  tests/caption_test.cpp
  tests/theme_test.cpp
  tests/motion_test.cpp
  tests/deps_test.cpp)
target_compile_options(brujula_tests PRIVATE ${BRUJULA_FLAGS})
target_compile_definitions(brujula_tests PRIVATE ${BRUJULA_DEFS})
target_link_libraries(brujula_tests PRIVATE
  brujula_core doctest::doctest nlohmann_json::nlohmann_json sqlite3)
