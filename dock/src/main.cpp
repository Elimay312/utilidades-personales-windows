#include <windows.h>

#include <ole2.h>
#include <shellapi.h>  // CommandLineToArgvW

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "app.h"
#include "check.h"
#include "core/log.h"
#include "core/paths.h"
#include "system/icons.h"

using namespace dock;

namespace {

// Compilado como WIN32 no hay consola propia. Si la salida ya va redirigida (una tubería o un
// fichero) se usa tal cual; si no y nos lanzó una terminal, se engancha la suya. Engancharse
// con la salida redirigida rompería la redirección.
void AttachParentConsole() {
  const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) return;
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
  FILE* ignored = nullptr;
  freopen_s(&ignored, "CONOUT$", "w", stdout);
  SetConsoleOutputCP(CP_UTF8);
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  AttachParentConsole();

  // OleInitialize lo primero, antes de tocar nada de COM: si el hilo se inicializó con
  // CoInitialize(Ex), RegisterDragDrop "will always return an E_OUTOFMEMORY error".
  if (FAILED(OleInitialize(nullptr))) {
    std::fputs("OleInitialize falló\n", stdout);
    return 2;
  }

  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  bool check = false;
  // El proceso hijo que extrae iconos (ver ExtractIconsOutOfProcess): ni log ni mutex.
  if (argc > 1 && std::wstring(argv[1]) == L"--extraer") {
    LocalFree(argv);
    const int code = RunExtractor();
    OleUninitialize();
    return code;
  }
  // --config=<ruta>: otro dock.json, para probar sin tocar el del usuario.
  std::filesystem::path configPath = AppDataDir() / L"dock.json";
  for (int i = 1; i < argc; i++) {
    const std::wstring arg = argv[i];
    if (arg == L"--check") check = true;
    else if (arg.rfind(L"--config=", 0) == 0) configPath = arg.substr(9);
  }
  LocalFree(argv);

  if (check) {
    const int failures = RunChecks(configPath);
    OleUninitialize();
    return failures;
  }

  LogInit();
  const HANDLE onlyInstance = CreateMutexW(nullptr, TRUE, L"Local\\DockSingleInstance");
  if (!onlyInstance || GetLastError() == ERROR_ALREADY_EXISTS) {
    LogInfo(L"[dock] ya hay otro dock en marcha, salgo");
    return 1;
  }

  // La primera vez no hay dock.json: se escribe el de ejemplo que lleva el exe, como hacía el de
  // C# con el que iba a su lado. Solo el de siempre (no uno de --config) y nunca encima de otro.
  std::error_code missing;
  if (configPath == AppDataDir() / L"dock.json" && !std::filesystem::exists(configPath, missing)) {
    std::filesystem::create_directories(configPath.parent_path(), missing);
    const std::string_view seed = SeedConfig();
    std::ofstream(configPath, std::ios::binary).write(seed.data(), static_cast<std::streamsize>(seed.size()));
    LogInfo(L"[config] primera vez: dock.json de ejemplo ({} bytes)", seed.size());
  }

  LogInfo(L"[dock] arranca, config en {}", configPath.wstring());
  App app(configPath);
  const int code = app.Run();
  OleUninitialize();
  return code;
}
