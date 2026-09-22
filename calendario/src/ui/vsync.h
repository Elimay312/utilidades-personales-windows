#pragma once

// The clock the spring runs on: one message per frame that DWM composes.
//
// A 16 ms SetTimer is what the hover fades use, and it is fine for them, but it is not the
// screen's clock: it drifts against the refresh and every so often two frames land in one or
// none in another, which on a window that is growing reads as a stutter. DwmFlush returns when
// DWM has composed a frame, so a thread that loops on it and posts a message is a vsync timer
// with no swap chain tricks and no Windows 11-only API.

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace agenda {

class FrameClock {
 public:
  FrameClock() = default;
  ~FrameClock();

  FrameClock(const FrameClock&) = delete;
  FrameClock& operator=(const FrameClock&) = delete;

  // Posts `message` to `hwnd` once per composed frame while running. The thread is born on the
  // first Run and then only sleeps between animations, so stopping never waits for a join.
  void Run(HWND hwnd, UINT message);
  void Pause();
  // The window took the frame. Until it does no other one is posted: a slow frame costs one
  // frame, never a queue of them to catch up on.
  void Taken() { pending_.store(false); }

 private:
  void Loop();

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable wake_;
  HWND hwnd_ = nullptr;
  UINT message_ = 0;
  bool running_ = false;
  bool quitting_ = false;
  std::atomic<bool> pending_{false};
};

}  // namespace agenda
