#pragma once

// The one thread for everything that can block: WMI now, DDC/CI and WinRT's .get() later
// (CLAUDE.md, architecture rule 2). Jobs run one after another in the order they came, with COM
// initialised multithreaded on the thread, and they tell the window what happened with
// PostMessageW. The interface thread never waits for one.

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace panel {

class Worker {
 public:
  Worker() = default;
  ~Worker() { Stop(); }
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;

  void Start();
  // Runs what is queued, then ends the thread. Whatever a job created on the thread (a WMI
  // connection) has to be let go by a job queued before this, or it outlives COM there.
  void Stop();

  // Queues `job`. A job with the same non-empty `key` still waiting to run is replaced instead:
  // while a slider is dragged only the latest value is worth writing, and the ones in between
  // would only make the screen lag behind the pointer.
  void Post(std::string key, std::function<void()> job);

 private:
  void Loop();

  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::pair<std::string, std::function<void()>>> jobs_;
  bool stopping_ = false;
};

}  // namespace panel
