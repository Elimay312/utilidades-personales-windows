#include "ui/vsync.h"

#include <dwmapi.h>

namespace agenda {

FrameClock::~FrameClock() {
  if (!thread_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    quitting_ = true;
  }
  wake_.notify_all();
  thread_.join();
}

void FrameClock::Run(HWND hwnd, UINT message) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    hwnd_ = hwnd;
    message_ = message;
    running_ = true;
  }
  pending_.store(false);
  if (!thread_.joinable()) thread_ = std::thread([this] { Loop(); });
  wake_.notify_all();
}

void FrameClock::Pause() {
  std::lock_guard<std::mutex> lock(mutex_);
  running_ = false;
}

void FrameClock::Loop() {
  for (;;) {
    HWND hwnd = nullptr;
    UINT message = 0;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      wake_.wait(lock, [this] { return running_ || quitting_; });
      if (quitting_) return;
      hwnd = hwnd_;
      message = message_;
    }
    // With composition always on (Windows 8 and later) this only fails if DWM is restarting;
    // a sleep of one frame keeps the animation walking through that instead of spinning.
    if (FAILED(DwmFlush())) Sleep(16);
    if (!pending_.exchange(true)) PostMessageW(hwnd, message, 0, 0);
  }
}

}  // namespace agenda
