#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace dock {

// Abre %LOCALAPPDATA%\Dock\logs\dock-AAAAMMDD.log en modo añadir. Si falla, el log sigue
// llegando a la salida del depurador.
void LogInit();

void LogWrite(std::wstring_view level, std::wstring_view message);

// Trazas finas (posición del ratón, visible/escondido...): solo con DOCK_HOVER_LOG puesta.
bool Tracing();

template <class... Args>
void LogInfo(std::wformat_string<Args...> fmt, Args&&... args) {
  LogWrite(L"INFO", std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void LogError(std::wformat_string<Args...> fmt, Args&&... args) {
  LogWrite(L"ERROR", std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void LogTrace(std::wformat_string<Args...> fmt, Args&&... args) {
  if (Tracing()) LogWrite(L"TRACE", std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace dock
