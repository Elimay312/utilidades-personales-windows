#pragma once

#include <windows.h>

#include <chrono>
#include <format>
#include <string>

#include "core/i18n.h"
#include "data/model.h"

namespace agenda {

// A click on a reminder while Agenda is running: wParam is the day it is about, as days since
// 1970, and the popup opens on it.
inline constexpr UINT kReminderOpenMessage = WM_APP + 6;

// Shows `reminder` as a Windows notification in the reminder scenario, so it stays on screen
// until it is answered, with Windows' own snooze and dismiss. `now` is the WallMinute it is
// being shown at, which is what "en 10 min" is counted from.
//
// False when Windows will not take it: an unpackaged app can only raise a toast under an
// AppUserModelID that a Start Menu shortcut declares, and a build run straight from its folder
// has none. The caller then says it with the tray balloon, which Windows shows as a
// notification anyway.
bool ShowReminderToast(const Reminder& reminder, long long now, HWND notify);

// "17:00 – 18:00 · Café Pergamino" and "En 10 min": the two lines under the title. Here and
// inline so the balloon that stands in for a toast says exactly the same, and the tests reach
// them without WinRT.
inline std::wstring ReminderWhen(const Reminder& reminder, long long now) {
  std::wstring out;
  const Date today = DayOfWall(now);
  if (!reminder.startMin) {
    out = T(L"Todo el día", L"All day");
  } else {
    out = std::format(L"{:02}:{:02}", *reminder.startMin / 60, *reminder.startMin % 60);
    if (reminder.endMin && *reminder.endMin != *reminder.startMin) {
      out += std::format(L" – {:02}:{:02}", *reminder.endMin / 60, *reminder.endMin % 60);
    }
  }
  if (reminder.day != today) {
    const int days = static_cast<int>(
        (std::chrono::sys_days{reminder.day} - std::chrono::sys_days{today}).count());
    const std::wstring day =
        days == 1 ? std::wstring(T(L"mañana", L"tomorrow"))
                  : std::format(L"{} {}", static_cast<unsigned>(reminder.day.day()),
                                MonthName(reminder.day.month()).substr(0, 3));
    out += L" · " + day;
  }
  if (!reminder.location.empty()) out += L" · " + reminder.location;
  return out;
}

inline std::wstring ReminderSoon(const Reminder& reminder, long long now) {
  const long long left = WallMinute(reminder.day, reminder.startMin.value_or(0)) - now;
  if (left <= 0) return std::wstring(T(L"Empieza ahora", L"Starting now"));
  if (left < 60) {
    const long long minutes = left;
    return std::vformat(T(L"En {} min", L"In {} min"), std::make_wformat_args(minutes));
  }
  if (left < 1440) {
    const long long hours = left / 60;
    const long long minutes = left % 60;
    if (minutes == 0) return std::vformat(T(L"En {} h", L"In {} h"), std::make_wformat_args(hours));
    return std::vformat(T(L"En {} h {} min", L"In {} h {} min"),
                        std::make_wformat_args(hours, minutes));
  }
  const long long days = (left + 1439) / 1440;
  return std::vformat(T(L"En {} días", L"In {} days"), std::make_wformat_args(days));
}


}  // namespace agenda
