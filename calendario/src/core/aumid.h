#pragma once

namespace agenda {

// The AppUserModelID the Start Menu shortcut carries. An unpackaged app can only raise a toast
// under an id that some shortcut in the Start Menu declares, so the installer writes it on the
// shortcut and the app asks for its notifier with the same string. Two copies would drift.
inline constexpr wchar_t kAppUserModelId[] = L"Agenda.Desktop";

}  // namespace agenda
