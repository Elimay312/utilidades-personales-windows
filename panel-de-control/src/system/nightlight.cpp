#include "system/nightlight.h"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>

#include "core/i18n.h"
#include "core/log.h"
#include "core/paths.h"
#include "system/nightlight_blob.h"

namespace panel {
namespace {

constexpr wchar_t kStatePath[] =
    LR"(Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\DefaultAccount\Current\)"
    LR"(default$windows.data.bluelightreduction.bluelightreductionstate\)"
    LR"(windows.data.bluelightreduction.bluelightreductionstate)";
constexpr wchar_t kSettingsPath[] =
    LR"(Software\Microsoft\Windows\CurrentVersion\CloudStore\Store\DefaultAccount\Current\)"
    LR"(default$windows.data.bluelightreduction.settings\)"
    LR"(windows.data.bluelightreduction.settings)";

std::vector<uint8_t> ReadData(const wchar_t* path) {
  DWORD size = 0;
  if (RegGetValueW(HKEY_CURRENT_USER, path, L"Data", RRF_RT_REG_BINARY, nullptr, nullptr, &size) != ERROR_SUCCESS ||
      size == 0 || size > 4096) {
    return {};
  }
  std::vector<uint8_t> data(size);
  if (RegGetValueW(HKEY_CURRENT_USER, path, L"Data", RRF_RT_REG_BINARY, nullptr, data.data(), &size) !=
      ERROR_SUCCESS) {
    return {};
  }
  data.resize(size);
  return data;
}

// The value as it was before the panel ever wrote it, once: a later copy would keep the panel's
// own write, which is not what anybody would want back.
void KeepOriginal(const std::vector<uint8_t>& original) {
  const std::filesystem::path file = AppDataDir() / L"luz-nocturna.bak";
  std::error_code ec;
  if (std::filesystem::exists(file, ec)) return;
  std::filesystem::create_directories(file.parent_path(), ec);
  std::ofstream out(file, std::ios::binary);
  out.write(reinterpret_cast<const char*>(original.data()), static_cast<std::streamsize>(original.size()));
  LogInfo(L"nightlight: the original state is kept in {}", file.wstring());
}

}  // namespace

void NightLight::Start(HWND window, Worker& worker) {
  window_ = window;
  worker_ = &worker;
  worker_->Post({}, [this] {
    Open(state_, kStatePath);
    Open(settings_, kSettingsPath);
    Read();
  });
}

void NightLight::Stop() {
  if (worker_ == nullptr) return;
  worker_->Post({}, [this] {
    Close(state_);
    Close(settings_);
  });
  worker_ = nullptr;
}

void NightLight::Set(bool on) {
  if (worker_ != nullptr) worker_->Post("night-set", [this, on] { Write(on); });
}

NightLight::Snapshot NightLight::Current() {
  std::lock_guard lock(mutex_);
  Snapshot out = snapshot_;
  snapshot_.problem.clear();
  return out;
}

void CALLBACK NightLight::Changed(PVOID context, BOOLEAN) {
  // A pool thread: back to the worker, which reads and watches again.
  auto* watch = static_cast<Watch*>(context);
  Worker* worker = watch->self->worker_;
  if (worker == nullptr) return;
  worker->Post({}, [watch] {
    watch->self->Read();
    watch->self->Arm(*watch);
  });
}

void NightLight::Open(Watch& watch, const wchar_t* path) {
  watch.self = this;
  // Read and notify only: the settings key is never opened to write (SEGURIDAD.md 2.5).
  if (RegOpenKeyExW(HKEY_CURRENT_USER, path, 0, KEY_READ | KEY_NOTIFY, &watch.key) != ERROR_SUCCESS) {
    watch.key = nullptr;
    return;
  }
  watch.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);  // auto-reset: one wait, re-armed
  if (watch.event == nullptr ||
      !RegisterWaitForSingleObject(&watch.wait, watch.event, &NightLight::Changed, &watch, INFINITE,
                                   WT_EXECUTEDEFAULT)) {
    watch.wait = nullptr;
    return;
  }
  Arm(watch);
}

void NightLight::Arm(Watch& watch) {
  // Called on the worker, which lives as long as the watch does: an asynchronous registry
  // notification ends when the thread that asked for it does.
  if (watch.key != nullptr && watch.event != nullptr) {
    RegNotifyChangeKeyValue(watch.key, FALSE, REG_NOTIFY_CHANGE_LAST_SET, watch.event, TRUE);
  }
}

void NightLight::Close(Watch& watch) {
  if (watch.wait != nullptr) UnregisterWaitEx(watch.wait, INVALID_HANDLE_VALUE);  // waits for a callback running
  if (watch.event != nullptr) CloseHandle(watch.event);
  if (watch.key != nullptr) RegCloseKey(watch.key);
  watch = Watch{};
}

void NightLight::Read() {
  Snapshot next;
  next.known = true;
  const std::vector<uint8_t> state = ReadData(kStatePath);
  const std::optional<bool> on = ReadNightLightState(state);
  // Only a state blob that round-trips can be switched; otherwise the tile says "not supported"
  // and nothing is ever written.
  next.state.supported = on.has_value() && RoundTrips(state);
  next.state.on = on.value_or(false);
  NightLightData data;
  if (ReadNightLightSettings(ReadData(kSettingsPath), data)) {
    next.state.scheduled = data.scheduled;
    next.state.fromMinute = data.fromMinute;
    next.state.toMinute = data.toMinute;
  }
  {
    std::lock_guard lock(mutex_);
    next.problem = snapshot_.problem;
    snapshot_ = next;
  }
  PostMessageW(window_, kNightMessage, 0, 0);
}

void NightLight::Write(bool on) {
  // Read again right now: the value to change is the one there is, not the one seen last.
  const std::vector<uint8_t> original = ReadData(kStatePath);
  FILETIME now{};
  GetSystemTimeAsFileTime(&now);
  const uint64_t filetime = (static_cast<uint64_t>(now.dwHighDateTime) << 32) | now.dwLowDateTime;
  const std::optional<std::vector<uint8_t>> changed =
      WithNightLight(original, on, static_cast<uint64_t>(std::time(nullptr)), filetime);
  if (!changed) {
    LogError(L"nightlight: the state value is not one this knows; nothing was written");
    std::lock_guard lock(mutex_);
    snapshot_.problem = T(L"La luz nocturna de este Windows no es compatible. No se tocó nada.",
                          L"This Windows' Night Light is not supported. Nothing was changed.");
  } else {
    KeepOriginal(original);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kStatePath, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
      const LSTATUS written = RegSetValueExW(key, L"Data", 0, REG_BINARY, changed->data(),
                                             static_cast<DWORD>(changed->size()));
      RegCloseKey(key);
      LogInfo(L"nightlight: turned {} ({})", on ? L"on" : L"off", written == ERROR_SUCCESS ? L"written" : L"failed");
    }
  }
  Read();
}

}  // namespace panel
