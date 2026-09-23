#include <doctest/doctest.h>

#include <atomic>
#include <future>
#include <vector>

#include "system/worker.h"

using namespace panel;

TEST_CASE("jobs run in order, and a keyed one waiting is replaced by the next with its key") {
  Worker worker;
  worker.Start();

  // Hold the thread on a first job, so the rest pile up behind it.
  std::promise<void> release;
  std::shared_future<void> gate = release.get_future().share();
  worker.Post({}, [gate] { gate.wait(); });

  std::vector<int> ran;  // only the worker writes it, and it is read after Stop joins
  worker.Post({}, [&ran] { ran.push_back(1); });
  worker.Post("brightness", [&ran] { ran.push_back(10); });
  worker.Post("brightness", [&ran] { ran.push_back(20); });
  worker.Post({}, [&ran] { ran.push_back(2); });
  worker.Post("brightness", [&ran] { ran.push_back(30); });  // replaces 20, keeps its place
  release.set_value();
  worker.Stop();  // runs what is queued, then joins

  CHECK(ran == std::vector<int>{1, 30, 2});
}

TEST_CASE("stopping runs what was queued before it, and nothing after") {
  Worker worker;
  worker.Start();
  std::atomic<int> count{0};
  for (int i = 0; i < 5; ++i) worker.Post({}, [&count] { ++count; });
  worker.Stop();
  CHECK(count == 5);
  worker.Post({}, [&count] { ++count; });  // after Stop: dropped, not run and not leaked
  CHECK(count == 5);
}
