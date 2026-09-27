#include "core/log.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <mutex>
#include <string>

#include "core/paths.h"

namespace dock {
namespace {

std::ofstream g_file;
std::mutex g_mutex;

std::string ToUtf8(std::wstring_view text) {
  if (text.empty()) return {};
  const int size = static_cast<int>(text.size());
  const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.data(), size, nullptr, 0, nullptr, nullptr);
  std::string out(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.data(), size, out.data(), bytes, nullptr, nullptr);
  return out;
}

}  // namespace

void LogInit() {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  const std::filesystem::path dir = AppDataDir() / L"logs";

  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  if (ec) return;

  const std::wstring name = std::format(L"dock-{:04}{:02}{:02}.log", now.wYear, now.wMonth, now.wDay);
  std::lock_guard lock(g_mutex);
  g_file.open(dir / name, std::ios::app | std::ios::binary);
}

bool Tracing() {
  static const bool on = GetEnvironmentVariableW(L"DOCK_HOVER_LOG", nullptr, 0) > 0;
  return on;
}

void LogWrite(std::wstring_view level, std::wstring_view message) {
  SYSTEMTIME now{};
  GetLocalTime(&now);
  const std::string line = ToUtf8(std::format(L"{:02}:{:02}:{:02}.{:03} [{}] {}\r\n", now.wHour,
                                              now.wMinute, now.wSecond, now.wMilliseconds, level,
                                              message));
  OutputDebugStringA(line.c_str());

  std::lock_guard lock(g_mutex);
  // Con consola (--check, o lanzado desde una terminal) también sale por ahí.
  fputs(line.c_str(), stdout);
  fflush(stdout);
  if (!g_file.is_open()) return;
  // Se vacía en cada línea: el dock de C# perdía la cola del log al matarlo con
  // Stop-Process -Force, justo la parte que explicaba el fallo.
  g_file << line;
  g_file.flush();
}

}  // namespace dock
