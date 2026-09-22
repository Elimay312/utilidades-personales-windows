#include "app/monitors.h"

#include <format>

namespace agenda {
namespace {

std::optional<int> ParseMonitorNumber(std::wstring_view text) {
  if (text.empty() || text.size() > 2) return std::nullopt;
  int value = 0;
  for (const wchar_t c : text) {
    if (c < L'0' || c > L'9') return std::nullopt;
    value = value * 10 + (c - L'0');
  }
  return value;
}

// True when arg is "--flag=something", leaving that something in value.
bool Flag(std::wstring_view arg, std::wstring_view flag, std::wstring_view& value) {
  if (!arg.starts_with(flag)) return false;
  value = arg.substr(flag.size());
  return true;
}

struct Search {
  std::wstring device;
  HMONITOR found = nullptr;
};

BOOL CALLBACK VisitMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
  auto& search = *reinterpret_cast<Search*>(param);
  MONITORINFOEXW info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor, &info) && search.device == info.szDevice) {
    search.found = monitor;
    return FALSE;  // stop enumerating
  }
  return TRUE;
}

}  // namespace

Options ParseOptions(int argc, const wchar_t* const* argv, std::wstring_view envMonitor) {
  Options options;
  bool fromCommandLine = false;

  for (int i = 1; i < argc; ++i) {
    const std::wstring_view arg = argv[i];
    std::wstring_view value;

    if (Flag(arg, L"--monitor=", value)) {
      const std::optional<int> parsed = ParseMonitorNumber(value);
      if (!parsed) {
        options.error = std::format(L"--monitor expects a display number, got '{}'", value);
        return options;
      }
      options.monitor = *parsed;
      fromCommandLine = true;
    } else if (Flag(arg, L"--render-snapshot=", value)) {
      if (value != L"popup") {
        options.error = std::format(L"--render-snapshot only knows 'popup', got '{}'", value);
        return options;
      }
      options.snapshotView = value;
    } else if (Flag(arg, L"--theme=", value)) {
      if (value != L"dark" && value != L"light") {
        options.error = std::format(L"--theme expects dark or light, got '{}'", value);
        return options;
      }
      options.theme = value;
    } else if (Flag(arg, L"--out=", value)) {
      if (value.empty()) {
        options.error = L"--out expects a file path";
        return options;
      }
      options.snapshotOut = value;
    }
  }

  if (!fromCommandLine && !envMonitor.empty()) {
    const std::optional<int> parsed = ParseMonitorNumber(envMonitor);
    if (!parsed) {
      options.error =
          std::format(L"AGENDA_DEV_MONITOR expects a display number, got '{}'", envMonitor);
      return options;
    }
    options.monitor = *parsed;
  }

  if (!options.snapshotView.empty() && options.snapshotOut.empty()) options.snapshotOut = L"shot.png";
  return options;
}

std::optional<HMONITOR> FindMonitorByDisplayNumber(int number) {
  Search search{std::format(L"\\\\.\\DISPLAY{}", number), nullptr};
  EnumDisplayMonitors(nullptr, nullptr, VisitMonitor, reinterpret_cast<LPARAM>(&search));
  if (search.found == nullptr) return std::nullopt;
  return search.found;
}

}  // namespace agenda
