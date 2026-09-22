cmake_minimum_required(VERSION 3.28)

# vcpkg in manifest mode when VCPKG_ROOT exists; FetchContent covers the machines where it
# does not, so the presets in CMakePresets.json work out of the box either way.
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  cmake_path(SET agenda_vcpkg NORMALIZE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
  set(CMAKE_TOOLCHAIN_FILE "${agenda_vcpkg}" CACHE FILEPATH "vcpkg toolchain")
endif()

# C ademas de CXX: la amalgamacion de SQLite es C, y sin declararlo CMake no sabe con que
# enlazar ese objetivo.
project(Agenda VERSION 0.1.0 LANGUAGES C CXX)

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
find_package(SQLite3 QUIET)

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

# La amalgamacion, no el repositorio: el arbol completo de SQLite son cientos de megas y un
# sistema de compilacion propio para construir exactamente este archivo. Fijada con su hash;
# al subir de version se cambian las dos cosas juntas, y el ano de la URL tambien, porque
# sqlite.org archiva las descargas por ano.
if(NOT SQLite3_FOUND)
  # sqlite3.c llega por HTTPS y no por un clon de git, y el cmake que hay en el PATH de esta
  # maquina (el de WinLibs) no trae almacen de certificados: la descarga muere con "SSL
  # certificate verification failed" mientras los clones de git funcionan, asi que parece un
  # problema de red y no lo es. FetchContent reenvia CMAKE_TLS_CAINFO al sub-build, que es
  # donde ocurre la descarga; el de Git para Windows sirve y esta en todas estas maquinas.
  if(NOT DEFINED CMAKE_TLS_CAINFO AND NOT DEFINED ENV{CURL_CA_BUNDLE})
    foreach(agenda_ca_raw "$ENV{ProgramFiles}/Git/usr/ssl/certs/ca-bundle.crt"
                          "$ENV{ProgramW6432}/Git/usr/ssl/certs/ca-bundle.crt")
      # A barras hacia delante: %ProgramFiles% trae "C:\Program Files" y la ruta acaba dentro
      # de un script generado, donde "\P" es un escape invalido y aborta la descarga.
      file(TO_CMAKE_PATH "${agenda_ca_raw}" agenda_ca)
      if(EXISTS "${agenda_ca}")
        set(CMAKE_TLS_CAINFO "${agenda_ca}")
        message(STATUS "Agenda: certificados para la descarga de SQLite en ${agenda_ca}")
        break()
      endif()
    endforeach()
  endif()

  FetchContent_Declare(sqlite3
    URL      https://www.sqlite.org/2025/sqlite-amalgamation-3500400.zip
    URL_HASH SHA256=1d3049dd0f830a025a53105fc79fd2ab9431aea99e137809d064d8ee8356b032)
  FetchContent_MakeAvailable(sqlite3)

  # Con su propio nivel de warnings: /W4 sobre la amalgamacion no sale limpio y no vamos a
  # parchearla ni a silenciar warnings en codigo nuestro para taparla.
  add_library(sqlite3 STATIC "${sqlite3_SOURCE_DIR}/sqlite3.c")
  target_include_directories(sqlite3 SYSTEM PUBLIC "${sqlite3_SOURCE_DIR}")
  target_compile_options(sqlite3 PRIVATE /utf-8)
  target_compile_definitions(sqlite3 PRIVATE
    # Sin carga de extensiones: Agenda guarda la agenda de alguien y no tiene ningun motivo
    # para ejecutar codigo de un .dll que llegue dentro de un archivo de base de datos.
    SQLITE_OMIT_LOAD_EXTENSION=1
    SQLITE_DQS=0                  # "asi" es una cadena, nunca un nombre de columna
    SQLITE_THREADSAFE=1           # el hilo de escritura y el de interfaz la comparten
    SQLITE_DEFAULT_FOREIGN_KEYS=1)
  add_library(SQLite::SQLite3 ALIAS sqlite3)
endif()

set(AGENDA_WARNINGS /W4 /permissive- /utf-8)

# El parser vive aparte porque no depende de la interfaz: ni Direct2D, ni Win32, ni ventanas.
# Eso es lo que deja que los tests lo prueben directo, sin abrir nada.
add_library(agenda_nlp STATIC src/nlp/parser.cpp)
target_include_directories(agenda_nlp PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(agenda_nlp PRIVATE ${AGENDA_WARNINGS})
target_compile_definitions(agenda_nlp PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# La capa de datos, aparte por el mismo motivo que el parser: aqui dentro no entra ni Direct2D
# ni una ventana, asi que los tests la prueban directa contra una base en memoria.
add_library(agenda_data STATIC
  src/data/db.cpp
  src/data/schema.cpp
  src/data/store.cpp)
target_include_directories(agenda_data PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(agenda_data PRIVATE ${AGENDA_WARNINGS})
target_compile_definitions(agenda_data PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(agenda_data PUBLIC SQLite::SQLite3)

add_executable(Agenda WIN32
  src/app/main.cpp
  src/app/monitors.cpp
  src/app/tray.cpp
  src/core/config.cpp
  src/core/log.cpp
  src/ui/components.cpp
  src/ui/paint.cpp
  src/ui/popup_view.cpp
  src/ui/popup_window.cpp
  src/ui/snapshot.cpp
  src/ui/text_input.cpp
  src/ui/theme.cpp
  assets/agenda.rc)
target_include_directories(Agenda PRIVATE src)
target_compile_options(Agenda PRIVATE ${AGENDA_WARNINGS})
target_compile_definitions(Agenda PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
# user32, shell32, ole32 y uuid ya vienen en CMAKE_C_STANDARD_LIBRARIES de MSVC. dwrite entra
# con el texto de la fase 2 e imm32 con el IME del campo de entrada.
target_link_libraries(Agenda PRIVATE agenda_nlp agenda_data nlohmann_json::nlohmann_json
  d2d1 d3d11 dxgi dcomp dwmapi dwrite imm32 windowscodecs)
# El manifiesto (DPI Per-Monitor v2) entra por agenda.rc, no por el generado por el linker.
target_link_options(Agenda PRIVATE /MANIFEST:NO)

enable_testing()
add_executable(agenda_tests
  tests/test_args.cpp
  tests/test_dates.cpp
  tests/test_nlp.cpp
  tests/test_popup.cpp
  tests/test_store.cpp
  tests/test_text_input.cpp
  src/app/monitors.cpp
  # agenda_data registra sus errores, y el log es del ejecutable: se recompila aqui igual que
  # monitors.cpp y text_input.cpp. Sin LogInit no abre archivo y solo sale por el depurador,
  # que es lo que se quiere en una prueba.
  src/core/log.cpp
  src/ui/text_input.cpp)
target_include_directories(agenda_tests PRIVATE src)
target_compile_options(agenda_tests PRIVATE ${AGENDA_WARNINGS})
target_compile_definitions(agenda_tests PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(agenda_tests PRIVATE agenda_nlp agenda_data Catch2::Catch2WithMain)
add_test(NAME unit COMMAND agenda_tests)
