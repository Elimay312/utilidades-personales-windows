#include "system/worker.h"

#include <windows.h>

#include <objbase.h>

namespace dock {

void Worker::Start() {
  if (thread_.joinable()) return;
  stopping_ = false;
  thread_ = std::thread([this] { Loop(); });
}

void Worker::Stop() {
  if (!thread_.joinable()) return;
  {
    std::lock_guard lock(mutex_);
    stopping_ = true;
  }
  wake_.notify_all();
  thread_.join();
}

void Worker::Post(std::function<void()> job) {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    jobs_.push_back(std::move(job));
  }
  wake_.notify_one();
}

void Worker::Loop() {
  const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (jobs_.empty()) break;
      job = std::move(jobs_.front());
      jobs_.pop_front();
    }
    job();
  }
  if (com) CoUninitialize();
}

}  // namespace dock
