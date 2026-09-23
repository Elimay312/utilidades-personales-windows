#include "system/worker.h"

#include <windows.h>

#include <objbase.h>

namespace panel {

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

void Worker::Post(std::string key, std::function<void()> job) {
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    bool replaced = false;
    if (!key.empty()) {
      for (auto& waiting : jobs_) {
        if (waiting.first == key) {
          waiting.second = std::move(job);
          replaced = true;
          break;
        }
      }
    }
    if (!replaced) jobs_.emplace_back(std::move(key), std::move(job));
  }
  wake_.notify_one();
}

void Worker::Loop() {
  const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
      if (jobs_.empty()) break;  // stopping, and nothing left to run
      job = std::move(jobs_.front().second);
      jobs_.pop_front();
    }
    job();
  }
  if (com) CoUninitialize();
}

}  // namespace panel
