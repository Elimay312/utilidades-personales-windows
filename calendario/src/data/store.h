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
#include <optional>
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
  // The tasks with no date at all, for the tray in the expanded view's sidebar. The ones still
  // to do first; done ones stay, struck, until somebody unticks or forgets them.
  std::vector<DayItem> UndatedTasks();
  // One event whole, for the detail panel. Empty when it is gone or never was.
  std::optional<EventDetail> Event(const std::wstring& uid);
  // The reminders that fall due after `from` and up to `to`, both WallMinute. An event's own
  // list, or its calendar's when it has none; a calendar switched off in the sidebar says
  // nothing, like it shows nothing.
  std::vector<Reminder> DueReminders(long long from, long long to);
  // How much is waiting to go up to Google. Nobody empties this queue until phase 5, so for
  // now it is what proves a row and its operation were written together.
  int PendingOpCount();

  // --- Writes, queued -----------------------------------------------------------------
  // Returns the row the interface should show RIGHT NOW, uid already filled in. The insert
  // happens on the worker; if it fails the uid comes back through TakeFailures and the
  // interface takes the row out again.
  DayItem Create(const Draft& draft);
  void SetDone(const std::wstring& uid, bool done);
  // Writes the event back as `edit` says, and queues one update for it -- merged with any
  // update still waiting, so dragging an event five times sends it once. `edits` names the
  // fields beyond the times and the title that changed (kEditLocation, kEditRecurrence).
  //
  // Moving it to another calendar of a row Google already has also writes down where it was,
  // in moved_from: Google moves events with POST .../move on the calendar they are in, and by
  // the time the queue sends this the row already says where it is going.
  void UpdateEvent(const EventDetail& edit, unsigned edits);
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

  // The calendars the tray menu offers, in the order Google lists them. Read on the interface
  // thread, like everything the menu needs: it opens under the mouse and cannot wait.
  std::vector<CalendarInfo> Calendars(bool tasklists);
  // Moves the flag. Queued, because it is a write, and the menu is already closed by then.
  void SetDefaultCalendar(const std::string& id, bool isTask);

  // Every calendar and task list Google still lists, hidden ones included, for the sidebar's
  // switches. Calendars first, then the lists.
  std::vector<CalendarInfo> AllCalendars();
  // The sidebar's switch. Local only: it never goes up to Google, and a pull never touches it.
  void SetCalendarHidden(const std::string& id, bool hidden);

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

  // Says out loud that something could not be done. Public because the synchronisation has the
  // one case the popup has to hear about: a change Google refused for good, which would
  // otherwise disappear from the queue with nobody any the wiser.
  void Report(const std::wstring& uid, std::wstring_view message);

 private:
  void StartWorker();
  void StopWorker();
  void Enqueue(std::function<void()> job);
  void Notify();
  // Queues one operation and hands back its id. Replacing the ones before it goes in that
  // order -- the new one in first, the old ones out after -- and the order is the fix for a
  // bug: pending_ops.id is a rowid without AUTOINCREMENT, so deleting the highest one first
  // and then inserting reuses its number. A pass that was sending the old operation would then
  // delete the new one by that id when it finished, and the newer edit never reached Google.
  // With the new row in first, its id is above anything a pass can be holding.
  bool QueueOp(const char* entity, const std::wstring& uid, const char* op,
               std::int64_t* id = nullptr);
  // Takes out the operations for `uid` that `opLike` matches, except the one just queued.
  bool DropOthers(const std::wstring& uid, const char* opLike, std::int64_t keep);

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
