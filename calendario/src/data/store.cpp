#include "data/store.h"

#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <utility>

#include "core/log.h"
#include "data/schema.h"

namespace agenda {
namespace {

std::int64_t NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void AddDot(std::vector<DayDot>& dots, Date date, std::uint32_t color) {
  // First one wins, which is what SampleDayColor did: one dot per day, and it belongs to
  // whatever starts earliest.
  for (const DayDot& dot : dots) {
    if (dot.date == date) return;
  }
  dots.push_back(DayDot{date, color});
}

}  // namespace

std::wstring NewUid() {
  GUID guid{};
  if (FAILED(CoCreateGuid(&guid))) {
    // Never seen to fail, but a uid that repeated would silently merge two events into one,
    // so the fallback still has to be unique.
    return std::format(L"t{:016x}", static_cast<unsigned long long>(GetTickCount64()) * 1000ull +
                                        static_cast<unsigned long long>(NowSeconds() % 1000));
  }
  wchar_t text[40]{};
  StringFromGUID2(guid, text, 40);
  // StringFromGUID2 wraps it in braces, and the braces are noise in a database column.
  std::wstring uid(text);
  if (uid.size() >= 2 && uid.front() == L'{') uid = uid.substr(1, uid.size() - 2);
  return uid;
}

Store::~Store() { Close(); }

bool Store::Open(const std::filesystem::path& path) {
  if (!db_.Open(path)) return false;
  if (!Migrate(db_)) {
    db_.Close();
    return false;
  }
  StartWorker();
  return true;
}

bool Store::OpenMemory() {
  if (!db_.OpenMemory()) return false;
  if (!Migrate(db_)) {
    db_.Close();
    return false;
  }
  StartWorker();
  return true;
}

void Store::Close() {
  StopWorker();
  db_.Close();
}

// --- The worker ---------------------------------------------------------------------------

void Store::StartWorker() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    running_ = true;
  }
  worker_ = std::thread([this] {
    for (;;) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return !running_ || !jobs_.empty(); });
        // Whatever is already queued still gets written: quitting with a creation left in the
        // queue would lose something the user already saw appear on screen.
        if (jobs_.empty()) return;
        job = std::move(jobs_.front());
        jobs_.pop_front();
        busy_ = true;
      }
      job();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        busy_ = false;
      }
      idle_.notify_all();
    }
  });
}

void Store::StopWorker() {
  if (!worker_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
  }
  wake_.notify_all();
  worker_.join();
}

void Store::Enqueue(std::function<void()> job) {
  if (!worker_.joinable()) {
    // No worker means the cache never opened. Writing here would be worse than not writing:
    // it would look like it had worked.
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.push_back(std::move(job));
  }
  wake_.notify_one();
}

void Store::Drain() {
  if (!worker_.joinable()) return;
  std::unique_lock<std::mutex> lock(mutex_);
  idle_.wait(lock, [this] { return jobs_.empty() && !busy_; });
}

void Store::Run(std::function<void()> job) {
  if (!worker_.joinable()) return;

  std::mutex done;
  std::condition_variable ready;
  bool finished = false;
  // Captured by reference, which is safe for exactly one reason: this function does not return
  // until the job has run. Stopping the worker drains what is queued before it exits, so the
  // wait below cannot outlive the queue.
  Enqueue([&] {
    job();
    {
      std::lock_guard<std::mutex> lock(done);
      finished = true;
    }
    ready.notify_one();
  });

  std::unique_lock<std::mutex> lock(done);
  ready.wait(lock, [&finished] { return finished; });
}

void Store::Notify() {
  if (hwnd_ != nullptr) PostMessageW(hwnd_, kStoreChangedMessage, 0, 0);
}

void Store::Report(const std::wstring& uid, std::wstring_view message) {
  {
    std::lock_guard<std::mutex> lock(failures_mutex_);
    failures_.push_back(Failure{uid, std::wstring(message)});
  }
  LogError(L"store: {} ({})", message, uid);
  Notify();
}

std::vector<Store::Failure> Store::TakeFailures() {
  std::lock_guard<std::mutex> lock(failures_mutex_);
  std::vector<Failure> taken;
  taken.swap(failures_);
  return taken;
}

// --- Reads --------------------------------------------------------------------------------

std::vector<DayItem> Store::ItemsForDay(Date day, bool includeUndated) {
  std::vector<DayItem> items;
  if (!db_.IsOpen()) return items;
  const std::string key = DayKey(day);

  // An event spanning days shows up on every one of them, but only the day it starts on gets
  // a clock: "17:00" on the second day of a trip would be a lie.
  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT e.uid, e.title, e.start_day, e.start_min, e.end_min, e.recurrence, c.color "
          "FROM events e JOIN calendars c ON c.id = e.calendar_id "
          "WHERE e.deleted_at IS NULL AND c.visible = 1 "
          "  AND e.start_day <= ?1 AND e.end_day >= ?1")) {
    stmt->Bind(1, key);
    while (stmt->Step()) {
      DayItem item;
      item.uid = stmt->Wide(0);
      item.title = stmt->Wide(1);
      const bool startsHere = stmt->Text(2) == key;
      item.startMin = startsHere ? stmt->OptInt(3) : std::nullopt;
      item.endMin = startsHere ? stmt->OptInt(4) : std::nullopt;
      item.repeats = !stmt->Text(5).empty();
      item.color = static_cast<std::uint32_t>(stmt->Int(6));
      items.push_back(std::move(item));
    }
  }

  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT t.uid, t.title, t.due_min, t.done_at, c.color "
          "FROM tasks t JOIN calendars c ON c.id = t.list_id "
          "WHERE t.deleted_at IS NULL AND c.visible = 1 "
          "  AND (t.due_day = ?1 OR (?2 AND t.due_day IS NULL))")) {
    stmt->Bind(1, key);
    stmt->Bind(2, includeUndated ? 1 : 0);
    while (stmt->Step()) {
      DayItem item;
      item.isTask = true;
      item.uid = stmt->Wide(0);
      item.title = stmt->Wide(1);
      item.startMin = stmt->OptInt(2);
      item.done = !stmt->IsNull(3);
      item.color = static_cast<std::uint32_t>(stmt->Int(4));
      items.push_back(std::move(item));
    }
  }

  std::sort(items.begin(), items.end(), EarlierThan);
  return items;
}

std::vector<DayDot> Store::DotsForRange(Date from, Date to) {
  std::vector<DayDot> dots;
  if (!db_.IsOpen()) return dots;
  const std::string first = DayKey(from);
  const std::string last = DayKey(to);

  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT e.start_day, e.end_day, c.color "
          "FROM events e JOIN calendars c ON c.id = e.calendar_id "
          "WHERE e.deleted_at IS NULL AND c.visible = 1 "
          "  AND e.start_day <= ?2 AND e.end_day >= ?1 "
          "ORDER BY e.start_day, e.start_min")) {
    stmt->Bind(1, first);
    stmt->Bind(2, last);
    while (stmt->Step()) {
      const std::optional<Date> start = ParseDayKey(stmt->Text(0));
      const std::optional<Date> end = ParseDayKey(stmt->Text(1));
      if (!start || !end) continue;
      const auto color = static_cast<std::uint32_t>(stmt->Int(2));
      for (Date date = *start; date <= *end; date = AddDays(date, 1)) {
        if (date >= from && date <= to) AddDot(dots, date, color);
      }
    }
  }

  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT t.due_day, c.color "
          "FROM tasks t JOIN calendars c ON c.id = t.list_id "
          "WHERE t.deleted_at IS NULL AND c.visible = 1 "
          "  AND t.due_day IS NOT NULL AND t.due_day BETWEEN ?1 AND ?2 "
          "ORDER BY t.due_day, t.due_min")) {
    stmt->Bind(1, first);
    stmt->Bind(2, last);
    while (stmt->Step()) {
      if (const std::optional<Date> due = ParseDayKey(stmt->Text(0))) {
        AddDot(dots, *due, static_cast<std::uint32_t>(stmt->Int(1)));
      }
    }
  }

  return dots;
}

int Store::PendingOpCount() {
  if (!db_.IsOpen()) return 0;
  std::optional<Stmt> stmt = db_.Prepare("SELECT COUNT(*) FROM pending_ops");
  if (!stmt || !stmt->Step()) return 0;
  return static_cast<int>(stmt->Int(0));
}

// --- Writes -------------------------------------------------------------------------------

bool Store::QueueOp(const char* entity, const std::wstring& uid, const char* op) {
  std::optional<Stmt> stmt =
      db_.Prepare("INSERT INTO pending_ops (entity, uid, op, queued_at) VALUES (?, ?, ?, ?)");
  if (!stmt) return false;
  stmt->Bind(1, entity);
  stmt->Bind(2, uid);
  stmt->Bind(3, op);
  stmt->Bind(4, NowSeconds());
  bool ok = false;
  stmt->Step(&ok);
  return ok;
}

std::string Store::DefaultCalendar(bool isTask) {
  const char* kind = isTask ? "tasklist" : "calendar";
  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT id FROM calendars WHERE kind = ? AND is_primary = 1 AND visible = 1 "
          "ORDER BY sort, id LIMIT 1")) {
    stmt->Bind(1, kind);
    if (stmt->Step()) return stmt->Text(0);
  }
  // Nothing flagged means no account yet, or a calendar that has been hidden since it was
  // chosen. Either way what is created has to land somewhere it can be seen.
  return isTask ? kLocalTaskListId : kLocalCalendarId;
}

DayItem Store::Create(const Draft& draft) {
  DayItem item;
  item.uid = NewUid();
  item.isTask = draft.isTask;
  item.title = draft.title;
  item.startMin = draft.startMin;
  item.endMin = draft.endMin;
  item.repeats = !draft.recurrence.empty();

  // The colour the card paints with has to be right on the first frame, so it is read here
  // instead of waited for. One row, by primary key.
  const std::string list = DefaultCalendar(draft.isTask);
  if (std::optional<Stmt> stmt = db_.Prepare("SELECT color FROM calendars WHERE id = ?")) {
    stmt->Bind(1, list);
    if (stmt->Step()) item.color = static_cast<std::uint32_t>(stmt->Int(0));
  }

  const std::wstring uid = item.uid;
  Enqueue([this, uid, draft, list] {
    // The row and its queued operation go in together. A creation saved without its operation
    // is an event that never reaches Google, with no error anywhere.
    Transaction tx(db_);
    if (!tx.Begin()) {
      Report(uid, L"no se pudo empezar a guardar");
      return;
    }

    const std::int64_t now = NowSeconds();
    bool ok = false;
    if (draft.isTask) {
      if (std::optional<Stmt> stmt = db_.Prepare(
              "INSERT INTO tasks (uid, list_id, title, due_day, due_min, updated_at) "
              "VALUES (?, ?, ?, ?, ?, ?)")) {
        stmt->Bind(1, uid);
        stmt->Bind(2, list);
        stmt->Bind(3, draft.title);
        if (draft.day) {
          stmt->Bind(4, DayKey(*draft.day));
        } else {
          stmt->BindNull(4);
        }
        stmt->Bind(5, draft.startMin);
        stmt->Bind(6, now);
        stmt->Step(&ok);
      }
    } else if (draft.day) {
      if (std::optional<Stmt> stmt = db_.Prepare(
              "INSERT INTO events (uid, calendar_id, title, start_day, start_min, end_day, "
              "                    end_min, recurrence, updated_at) "
              "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)")) {
        const Date endDay = draft.endDay ? *draft.endDay : *draft.day;
        stmt->Bind(1, uid);
        stmt->Bind(2, list);
        stmt->Bind(3, draft.title);
        stmt->Bind(4, DayKey(*draft.day));
        stmt->Bind(5, draft.startMin);
        stmt->Bind(6, DayKey(endDay));
        stmt->Bind(7, draft.endMin);
        stmt->Bind(8, draft.recurrence);
        stmt->Bind(9, now);
        stmt->Step(&ok);
      }
    }

    if (!ok || !QueueOp(draft.isTask ? "task" : "event", uid, "create") || !tx.Commit()) {
      Report(uid, L"no se pudo guardar");
      return;
    }
    Notify();
  });

  return item;
}

void Store::SetDone(const std::wstring& uid, bool done) {
  Enqueue([this, uid, done] {
    Transaction tx(db_);
    if (!tx.Begin()) {
      Report(uid, L"no se pudo marcar la tarea");
      return;
    }
    const std::int64_t now = NowSeconds();
    bool ok = false;
    if (std::optional<Stmt> stmt =
            db_.Prepare("UPDATE tasks SET done_at = ?, updated_at = ? WHERE uid = ?")) {
      if (done) {
        stmt->Bind(1, now);
      } else {
        stmt->BindNull(1);
      }
      stmt->Bind(2, now);
      stmt->Bind(3, uid);
      stmt->Step(&ok);
    }
    // At most one pending update per item: what goes out is built by reading the row when it
    // is sent, so ticking and unticking five times still has one thing to say.
    if (ok) {
      if (std::optional<Stmt> stmt =
              db_.Prepare("DELETE FROM pending_ops WHERE uid = ? AND op = 'update'")) {
        stmt->Bind(1, uid);
        stmt->Step(&ok);
      }
    }
    if (!ok || !QueueOp("task", uid, "update") || !tx.Commit()) {
      Report(uid, L"no se pudo marcar la tarea");
      return;
    }
    Notify();
  });
}

void Store::Remove(const std::wstring& uid, bool isTask) {
  Enqueue([this, uid, isTask] {
    Transaction tx(db_);
    if (!tx.Begin()) {
      Report(uid, L"no se pudo deshacer");
      return;
    }

    // Has this ever been up there? That is the whole question, and it decides between a delete
    // and a tombstone. Five seconds is plenty for a creation to have reached Google.
    bool sent = false;
    if (std::optional<Stmt> stmt =
            db_.Prepare(isTask ? "SELECT remote_id IS NOT NULL FROM tasks WHERE uid = ?"
                               : "SELECT remote_id IS NOT NULL FROM events WHERE uid = ?")) {
      stmt->Bind(1, uid);
      if (stmt->Step()) sent = stmt->Int(0) != 0;
    }

    bool ok = false;
    if (sent) {
      const std::int64_t now = NowSeconds();
      if (std::optional<Stmt> stmt = db_.Prepare(
              isTask ? "UPDATE tasks SET deleted_at = ?, updated_at = ? WHERE uid = ?"
                     : "UPDATE events SET deleted_at = ?, updated_at = ? WHERE uid = ?")) {
        stmt->Bind(1, now);
        stmt->Bind(2, now);
        stmt->Bind(3, uid);
        stmt->Step(&ok);
      }
    } else {
      if (std::optional<Stmt> stmt = db_.Prepare(isTask ? "DELETE FROM tasks WHERE uid = ?"
                                                        : "DELETE FROM events WHERE uid = ?")) {
        stmt->Bind(1, uid);
        stmt->Step(&ok);
      }
    }

    // Whatever was queued for this row is void either way: a creation that is being undone has
    // nothing left to create, and an edit has nothing left to edit.
    if (ok) {
      if (std::optional<Stmt> stmt = db_.Prepare("DELETE FROM pending_ops WHERE uid = ?")) {
        stmt->Bind(1, uid);
        stmt->Step(&ok);
      }
    }
    if (ok && sent) ok = QueueOp(isTask ? "task" : "event", uid, "delete");

    if (!ok || !tx.Commit()) {
      Report(uid, L"no se pudo deshacer");
      return;
    }
    Notify();
  });
}

}  // namespace agenda
