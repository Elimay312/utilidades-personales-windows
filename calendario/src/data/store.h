#pragma once

// The repository: what the popup reads, and the thread that does the writing.
//
// Reads run on the interface thread on purpose. The popup has to be on screen in under 100 ms
// with no spinner (CLAUDE.md), and the two queries that fill it are an index lookup and a
// handful of rows. Writes are the ones that go to the worker, because that is where phase 5
// hangs an HTTP request off the same queue.

#include <windows.h>

#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "data/db.h"
#include "data/model.h"

namespace agenda {

// The worker says "something changed, come and look" with an empty message and the interface
// rereads. A payload would be a pointer allocated on one thread and read on another, and
// rereading two queries costs less than getting that right.
inline constexpr UINT kStoreChangedMessage = WM_APP + 2;

class Store {
 public:
  Store() = default;
  ~Store();

  Store(const Store&) = delete;
  Store& operator=(const Store&) = delete;

  // Opens, migrates and starts the worker. False means the cache is unusable and the caller
  // has to say so instead of pretending.
  bool Open(const std::filesystem::path& path);
  bool OpenMemory();  // tests
  void Close();
  bool IsOpen() const { return db_.IsOpen(); }
  std::wstring error() const { return db_.error(); }

  // Where to post kStoreChangedMessage. Not set means nobody is listening, which is the case
  // in the tests.
  void SetNotifyWindow(HWND hwnd) { hwnd_ = hwnd; }

  // --- Reads, on the calling thread ---------------------------------------------------
  // `includeUndated` folds the tasks with no date at all into this day's list. The window
  // passes true for today, which is where a "comprar leche" with no date belongs: nothing
  // that was created is allowed to vanish.
  std::vector<DayItem> ItemsForDay(Date day, bool includeUndated);
  std::vector<DayDot> DotsForRange(Date from, Date to);
  // How much is waiting to go up to Google. Nobody empties this queue until phase 5, so for
  // now it is what proves a row and its operation were written together.
  int PendingOpCount();

  // --- Writes, queued -----------------------------------------------------------------
  // Returns the row the interface should show RIGHT NOW, uid already filled in. The insert
  // happens on the worker; if it fails the uid comes back through TakeFailures and the
  // interface takes the row out again.
  DayItem Create(const Draft& draft);
  void SetDone(const std::wstring& uid, bool done);
  // Undo. While nothing had ever been sent, the row and its queued operation could just leave
  // together. With an account connected that is no longer true: the five seconds of the notice
  // are long enough for the creation to already be up at Google, and a row deleted here would
  // leave the event on the phone for good and bring it back on the next pass. So a row that has
  // a remote_id becomes a tombstone with a deletion queued behind it, and only one that was
  // never sent goes for good.
  void Remove(const std::wstring& uid, bool isTask);

  // Where new things land: the calendar flagged is_primary for its kind. Seeded on 'local',
  // moved onto a Google calendar when one is connected, and moved again from the tray menu.
  // One column that already existed instead of a settings store nobody asked for.
  std::string DefaultCalendar(bool isTask);

  struct Failure {
    std::wstring uid;
    std::wstring message;
  };
  // Drains what the worker could not do. Called when kStoreChangedMessage arrives.
  std::vector<Failure> TakeFailures();

  // Waits until the queue is empty. For the tests, and for closing down.
  void Drain();

  // --- What the synchronisation uses --------------------------------------------------
  // Runs `job` on the writing thread and waits for it. This is how src/sync touches SQLite,
  // and the reason is the one this file has always given: one connection, one writer. The
  // network does NOT come through here -- the requests happen on the synchronisation thread
  // and only the short writes are handed over, so a creation never waits behind a download.
  //
  // Never call this from inside a job. It would be waiting for the thread it is running on.
  void Run(std::function<void()> job);

  // The connection, for that same engine, valid only inside a Run.
  //
  // It is a hole, and it is open on purpose: the alternative was fifteen methods on Store that
  // each say "the sync needs this SQL". What must never come through it is a write to
  // pending_ops. Create, SetDone and Remove are the only three functions that queue anything
  // (see QueueOp), the engine calls none of them, and that -- not a rule somebody has to
  // remember -- is the whole of why what comes down from Google never goes back up.
  Db& db() { return db_; }

  // "Something changed, come and look." Once per pass, not once per event.
  void Touch() { Notify(); }

 private:
  void StartWorker();
  void StopWorker();
  void Enqueue(std::function<void()> job);
  void Report(const std::wstring& uid, std::wstring_view message);
  void Notify();
  bool QueueOp(const char* entity, const std::wstring& uid, const char* op);

  Db db_;
  HWND hwnd_ = nullptr;

  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable wake_;
  std::condition_variable idle_;
  std::deque<std::function<void()>> jobs_;
  bool running_ = false;
  bool busy_ = false;

  std::mutex failures_mutex_;
  std::vector<Failure> failures_;
};

// A fresh identifier, ours, born before the row exists so the interface has something to
// point at from the first frame.
std::wstring NewUid();

}  // namespace agenda
