#include <windows.h>

#include <shellapi.h>  // CommandLineToArgvW

#include <format>

#include "app/monitors.h"
#include "core/config.h"
#include "core/log.h"
#include "core/paths.h"

using namespace agenda;

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
  LogInit();

  const HANDLE instance = CreateMutexW(nullptr, TRUE, L"Local\\AgendaSingleInstance");
  const bool alreadyRunning = instance == nullptr || GetLastError() == ERROR_ALREADY_EXISTS;
  if (alreadyRunning) {
    LogInfo(L"another instance is already running, exiting");
    return 1;
  }

  int argc = 0;
  wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  const Options options = ParseOptions(argc, argv, EnvVar(L"AGENDA_DEV_MONITOR"));
  LocalFree(argv);
  if (!options.error.empty()) {
    LogError(L"{}", options.error);
    return 2;
  }

  LoadConfig();

  HMONITOR monitor = nullptr;
  if (options.monitor == 0) {
    monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
  } else if (const std::optional<HMONITOR> found = FindMonitorByDisplayNumber(options.monitor)) {
    monitor = *found;
  } else {
    // No silent fallback to another monitor: CLAUDE.md forbids it.
    LogError(L"\\\\.\\DISPLAY{} is not connected, refusing to start", options.monitor);
    return 2;
  }

  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info)) {
    LogError(L"GetMonitorInfoW failed with error {}", GetLastError());
    return 2;
  }

  const RECT& work = info.rcWork;
  LogInfo(L"monitor {}: work area {},{} {}x{}", std::wstring_view(info.szDevice), work.left,
          work.top, work.right - work.left, work.bottom - work.top);
  return 0;
}
