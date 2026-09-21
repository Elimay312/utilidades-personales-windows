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
  src/shell/Input.cpp
  src/compositor/MotionSpec.cpp
  src/ui/Edit.cpp
  src/ui/Virtual.cpp
  # Fase 3. La regla para decidir que algo entra aquí no ha cambiado: ¿se equivocaría en
  # silencio? Una eñe mal convertida, una fecha leída en horario local y un repositorio
  # clasificado en el grupo equivocado son exactamente eso — la aplicación sigue andando y
  # solo el dato está mal.
  src/model/Result.cpp
  src/model/Utf.cpp
  src/model/Time.cpp
  src/model/Types.cpp
  src/model/Rules.cpp
  # Fase 4. Misma regla: qué repositorios entran en cada vista, en qué orden salen y qué
  # significa que dos textos coincidan son tres decisiones que, cuando se equivocan, dejan
  # una lista más corta —y una lista más corta se parece muchísimo a una lista correcta—.
  src/app/State.cpp
  src/store/Db.cpp
  src/store/Schema.cpp
  src/store/Repos.cpp
  # De github/ entra lo que decide y no lo que habla: construir la consulta, entender la
  # respuesta y saber qué se reintenta. WinHTTP, la credencial y el hilo se quedan fuera.
  src/github/Query.cpp
  src/github/Parse.cpp
  src/github/Retry.cpp)
target_include_directories(brujula_core PUBLIC src)
# SQLite entra en el núcleo, y no rompe la regla de la biblioteca: lo que el README dice que
# no entra aquí es Win32 y WinRT "para decidir", y SQLite es C portable. Lo que se gana es
# que el esquema y sus migraciones se puedan probar — y lo que vigilan esas pruebas es que
# una sincronización no se lleve por delante las notas del usuario, que es la prioridad 1.
#
# PUBLIC y no PRIVATE: brujula lo recibe por herencia y no hay que repetirlo abajo.
target_link_libraries(brujula_core PUBLIC sqlite3 nlohmann_json::nlohmann_json)
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
  src/compositor/Material.cpp
  src/compositor/Scroller.cpp
  src/compositor/Shadow.cpp
  src/ui/Text.cpp
  src/ui/Element.cpp
  src/ui/Host.cpp
  src/ui/Controls.cpp
  src/ui/Field.cpp
  src/ui/List.cpp
  src/ui/Sidebar.cpp
  src/ui/Overlays.cpp
  # Fase 3, lo que habla con el sistema: WinHTTP, el Administrador de credenciales y el
  # hilo de sincronización. Lo que decide —consulta, parser, reintentos, esquema— está en
  # brujula_core y tiene pruebas.
  src/store/Paths.cpp
  src/github/Http.cpp
  src/github/Client.cpp
  src/github/Auth.cpp
  src/github/Gh.cpp
  src/github/Sync.cpp
  # Fase 4. La vista principal, que sustituye a las dos raíces provisionales: Views::Demo de
  # la fase 1 y Views::Status de la fase 3 ya no existen, y las dos lo decían en su cabecera.
  # De la demo sobrevive lo que de verdad hacía falta —los botones de la ventana—, ahora en
  # Views::Chrome y escritos con el kit.
  src/views/Main.cpp
  src/views/Chrome.cpp
  src/views/Sidebar.cpp
  src/views/RepoList.cpp
  src/views/Card.cpp
  # La hoja de la credencial, de la fase 3. Se queda.
  src/views/Welcome.cpp
  # El catálogo solo existe en Debug: es una pantalla de revisión, no una función del
  # producto, y en Release ni se compila ni deja su texto de ejemplo dentro del binario.
  #
  # La ruta va ABSOLUTA a propósito. Una ruta relativa dentro de una expresión de
  # generador se resuelve contra el directorio de compilación y no contra el de fuentes,
  # y CMake no avisa: falla al enlazar y parece otra cosa.
  $<$<CONFIG:Debug>:${CMAKE_CURRENT_SOURCE_DIR}/src/views/Catalog.cpp>
  src/brujula.rc
  # El manifiesto va suelto, no dentro del .rc, para que link.exe lo funda con el
  # trustInfo que genera él. Con los dos habría dos RT_MANIFEST y haría falta
  # /MANIFEST:NO. Se comprueba con: mt.exe -inputresource:brujula.exe;#1 -out:...
  src/brujula.manifest)

target_compile_options(brujula PRIVATE ${BRUJULA_FLAGS})
target_compile_definitions(brujula PRIVATE ${BRUJULA_DEFS}
  # Con esto se compila el catálogo y se enciende la tecla F12. En Release vale 0 y las
  # dos cosas desaparecen del binario.
  BRUJULA_CATALOGO=$<IF:$<CONFIG:Debug>,1,0>)
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
  d2d1 dwrite
  # imm32: colocar la ventana de composición del IME en el cursor del campo de texto. La
  # cadena confirmada la reparte DefWindowProc como WM_CHAR, así que esto es lo único que
  # hace falta enlazar para que escribir con un método de entrada funcione.
  imm32
  # winhttp: el cliente de GitHub. Viene con Windows, que es lo que evita una dependencia.
  winhttp
  # shell32 + ole32: SHGetKnownFolderPath para %LOCALAPPDATA% —y su CoTaskMemFree—, y
  # ShellExecuteW para abrir GitHub en el navegador desde la hoja de bienvenida.
  shell32 ole32
  # advapi32: CredWriteW, CredReadW y CredDeleteW. La credencial no toca el disco en claro
  # por ningún otro camino (SEGURIDAD.md, regla 1).
  advapi32)
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
  tests/deps_test.cpp
  tests/metrics_test.cpp
  tests/input_test.cpp
  tests/edit_test.cpp
  tests/virtual_test.cpp
  tests/utf_test.cpp
  tests/time_test.cpp
  tests/rules_test.cpp
  tests/state_test.cpp
  tests/store_test.cpp
  tests/query_test.cpp
  tests/parse_test.cpp
  tests/retry_test.cpp
  tests/auth_test.cpp
  # Auth.cpp es del ejecutable, pero el tipo de la credencial y la comprobación de forma son
  # decisiones puras y se prueban. La prueba NO toca el Administrador de credenciales:
  # escribir ahí desde un test pisaría la credencial de verdad del usuario, que vive en el
  # mismo sitio y con el mismo nombre.
  src/github/Auth.cpp)
target_compile_options(brujula_tests PRIVATE ${BRUJULA_FLAGS})
target_compile_definitions(brujula_tests PRIVATE ${BRUJULA_DEFS})
target_link_libraries(brujula_tests PRIVATE
  brujula_core doctest::doctest nlohmann_json::nlohmann_json sqlite3
  # Lo que pide Auth.cpp. No se llama en las pruebas, pero hay que enlazarlo.
  advapi32)
