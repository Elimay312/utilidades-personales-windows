#pragma once

// Night Light on and off, and its schedule for the tile's subtitle (SEGURIDAD.md 2.5). There is
// no API: Windows keeps it in two CloudStore values and watches them, so writing the state value
// is what the switch in Windows' quick settings does. The blobs are read and built by
// system/nightlight_blob.cpp; this file is the only one that touches the registry for them.
//
// Cuts, all at once: only the state value is ever written, never the settings one; a blob that
// does not round-trip is not written; the first write keeps the original in
// %LOCALAPPDATA%\Panel\luz-nocturna.bak. Registry reads and writes run on the worker; changes
// made elsewhere (Windows' own switch, the schedule) arrive through RegNotifyChangeKeyValue.

#include <windows.h>

#include <mutex>
#include <string>

#include "model/state.h"
#include "system/worker.h"

namespace panel {

inline constexpr UINT kNightMessage = WM_APP + 15;

class NightLight {
 public:
  struct Snapshot {
    bool known = false;
    NightLightState state;
    std::wstring problem;
  };

  NightLight() = default;
  ~NightLight() = default;
  NightLight(const NightLight&) = delete;
  NightLight& operator=(const NightLight&) = delete;

  // Queues the first read and the watch on `worker`; answers come as kNightMessage.
  void Start(HWND window, Worker& worker);
  // Queues ending the watch and closing the keys, before the worker stops.
  void Stop();
  void Set(bool on);

  Snapshot Current();

 private:
  struct Watch {
    NightLight* self = nullptr;
    HKEY key = nullptr;
    HANDLE event = nullptr;
    HANDLE wait = nullptr;
  };
  static void CALLBACK Changed(PVOID context, BOOLEAN timedOut);
  // On the worker only.
  void Open(Watch& watch, const wchar_t* path);
  void Arm(Watch& watch);
  void Close(Watch& watch);
  void Read();
  void Write(bool on);

  HWND window_ = nullptr;
  Worker* worker_ = nullptr;
  Watch state_;
  Watch settings_;

  std::mutex mutex_;
  Snapshot snapshot_;
};

}  // namespace panel
