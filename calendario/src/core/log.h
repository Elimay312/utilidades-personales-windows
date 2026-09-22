#pragma once

#include <format>
#include <string_view>
#include <utility>

namespace agenda {

// Opens %LOCALAPPDATA%\Agenda\logs\agenda-YYYYMMDD.log in append mode. Safe to skip:
// logging without it still reaches the debugger output.
void LogInit();

void LogWrite(std::wstring_view level, std::wstring_view message);

template <class... Args>
void LogInfo(std::wformat_string<Args...> fmt, Args&&... args) {
  LogWrite(L"INFO", std::format(fmt, std::forward<Args>(args)...));
}

template <class... Args>
void LogError(std::wformat_string<Args...> fmt, Args&&... args) {
  LogWrite(L"ERROR", std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace agenda
