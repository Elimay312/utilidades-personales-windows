#include "data/store.h"

#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <utility>

#include "core/log.h"
#include "core/recurrence.h"
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
          "WHERE e.deleted_at IS NULL AND c.visible = 1 AND c.hidden = 0 "
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

  // The repetitions: series that began on an earlier day and ended it that same day, asked one
  // by one whether their rule lands here. The ones that span days are left to the query above,
  // which already finds them on every day they cover.
  // ponytail: every series older than the day is read and tested, fine for a personal agenda;
  // an index on recurrence != '' if thousands of them ever show up.
  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT e.uid, e.title, e.start_day, e.start_min, e.end_min, e.recurrence, c.color "
          "FROM events e JOIN calendars c ON c.id = e.calendar_id "
          "WHERE e.deleted_at IS NULL AND c.visible = 1 AND c.hidden = 0 "
          "  AND e.recurrence != '' AND e.start_day < ?1 AND e.end_day < ?1")) {
    stmt->Bind(1, key);
    while (stmt->Step()) {
      const std::optional<Date> start = ParseDayKey(stmt->Text(2));
      if (!start || !OccursOn(stmt->Text(5), *start, day)) continue;
      DayItem item;
      item.uid = stmt->Wide(0);
      item.title = stmt->Wide(1);
      item.startMin = stmt->OptInt(3);
      item.endMin = stmt->OptInt(4);
      item.repeats = true;
      item.color = static_cast<std::uint32_t>(stmt->Int(6));
      items.push_back(std::move(item));
    }
  }

  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT t.uid, t.title, t.due_min, t.done_at, c.color "
          "FROM tasks t JOIN calendars c ON c.id = t.list_id "
          "WHERE t.deleted_at IS NULL AND c.visible = 1 AND c.hidden = 0 "
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

std::vector<Reminder> Store::DueReminders(long long from, long long to) {
  std::vector<Reminder> out;
  if (!db_.IsOpen() || to <= from) return out;
  // The longest lead Google allows is four weeks, so nothing starting later than that past `to`
  // can be due yet.
  constexpr int kMaxLeadMinutes = 40320;
  const Date first = DayOfWall(from);
  const Date last = DayOfWall(to + kMaxLeadMinutes);

  std::optional<Stmt> stmt = db_.Prepare(
      "SELECT e.uid, e.title, e.location, e.start_day, e.start_min, e.end_min, e.recurrence, "
      "       COALESCE(e.reminders, c.reminders), c.color "
      "FROM events e JOIN calendars c ON c.id = e.calendar_id "
      "WHERE e.deleted_at IS NULL AND c.visible = 1 AND c.hidden = 0 "
      "  AND COALESCE(e.reminders, c.reminders) != '' "
      "  AND e.start_day <= ?2 AND (e.start_day >= ?1 OR e.recurrence != '')");
  if (!stmt) return out;
  stmt->Bind(1, DayKey(first));
  stmt->Bind(2, DayKey(last));
  while (stmt->Step()) {
    const std::optional<Date> start = ParseDayKey(stmt->Text(3));
    if (!start) continue;
    std::vector<int> leads;
    const std::string list = stmt->Text(7);
    for (size_t at = 0; at <= list.size();) {
      size_t comma = list.find(',', at);
      if (comma == std::string::npos) comma = list.size();
      int minutes = 0;
      bool digits = comma > at;
      for (size_t i = at; i < comma && digits; ++i) {
        digits = list[i] >= '0' && list[i] <= '9';
        minutes = minutes * 10 + (list[i] - '0');
      }
      if (digits && minutes <= kMaxLeadMinutes) leads.push_back(minutes);
      at = comma + 1;
    }
    if (leads.empty()) continue;
    const int longest = *std::max_element(leads.begin(), leads.end());

    const std::optional<int> startMin = stmt->OptInt(4);
    const std::string rule = stmt->Text(6);
    // Every day this event happens on whose reminders could land inside the window: the day it
    // starts, or, for a repetition, the days its rule lands on between the two ends.
    const Date lastDay = DayOfWall(to + longest);
    for (Date day = rule.empty() ? *start : (std::max)(*start, first); day <= lastDay;
         day = AddDays(day, 1)) {
      if (day != *start && (rule.empty() || !OccursOn(rule, *start, day))) {
        if (rule.empty()) break;
        continue;
      }
      for (const int lead : leads) {
        const long long due = WallMinute(day, startMin.value_or(0)) - lead;
        if (due <= from || due > to) continue;
        Reminder reminder;
        reminder.uid = stmt->Wide(0);
        reminder.title = stmt->Wide(1);
        reminder.location = stmt->Wide(2);
        reminder.day = day;
        reminder.startMin = startMin;
        reminder.endMin = stmt->OptInt(5);
        reminder.minutesBefore = lead;
        reminder.at = due;
        reminder.color = static_cast<std::uint32_t>(stmt->Int(8));
        out.push_back(std::move(reminder));
      }
      if (rule.empty()) break;
    }
  }
  std::sort(out.begin(), out.end(),
            [](const Reminder& a, const Reminder& b) { return a.at < b.at; });
  return out;
}

std::vector<DayDot> Store::DotsForRange(Date from, Date to) {
  std::vector<DayDot> dots;
  if (!db_.IsOpen()) return dots;
  const std::string first = DayKey(from);
  const std::string last = DayKey(to);

  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT e.start_day, e.end_day, c.color, e.recurrence "
          "FROM events e JOIN calendars c ON c.id = e.calendar_id "
          "WHERE e.deleted_at IS NULL AND c.visible = 1 AND c.hidden = 0 "
          "  AND e.start_day <= ?2 AND (e.end_day >= ?1 OR e.recurrence != '') "
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
      // A repetition puts a dot wherever its rule lands, the same days ItemsForDay finds it on.
      if (const std::string rule = stmt->Text(3); !rule.empty() && *end == *start) {
        for (Date date = (std::max)(from, AddDays(*start, 1)); date <= to;
             date = AddDays(date, 1)) {
          if (OccursOn(rule, *start, date)) AddDot(dots, date, color);
        }
      }
    }
  }

  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT t.due_day, c.color "
          "FROM tasks t JOIN calendars c ON c.id = t.list_id "
          "WHERE t.deleted_at IS NULL AND c.visible = 1 AND c.hidden = 0 "
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

std::vector<DayItem> Store::UndatedTasks() {
  std::vector<DayItem> items;
  if (!db_.IsOpen()) return items;
  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT t.uid, t.title, t.done_at, c.color "
          "FROM tasks t JOIN calendars c ON c.id = t.list_id "
          "WHERE t.deleted_at IS NULL AND c.visible = 1 AND c.hidden = 0 "
          "  AND t.due_day IS NULL "
          "ORDER BY t.done_at IS NOT NULL, t.position, t.title")) {
    while (stmt->Step()) {
      DayItem item;
      item.isTask = true;
      item.uid = stmt->Wide(0);
      item.title = stmt->Wide(1);
      item.done = !stmt->IsNull(2);
      item.color = static_cast<std::uint32_t>(stmt->Int(3));
      items.push_back(std::move(item));
    }
  }
  return items;
}

std::optional<EventDetail> Store::Event(const std::wstring& uid) {
  if (!db_.IsOpen()) return std::nullopt;
  std::optional<Stmt> stmt = db_.Prepare(
      "SELECT calendar_id, title, location, notes, recurrence, start_day, start_min, end_day, "
      "       end_min FROM events WHERE uid = ? AND deleted_at IS NULL");
  if (!stmt) return std::nullopt;
  stmt->Bind(1, uid);
  if (!stmt->Step()) return std::nullopt;
  const std::optional<Date> start = ParseDayKey(stmt->Text(5));
  const std::optional<Date> end = ParseDayKey(stmt->Text(7));
  if (!start || !end) return std::nullopt;

  EventDetail out;
  out.uid = uid;
  out.calendarId = stmt->Text(0);
  out.title = stmt->Wide(1);
  out.location = stmt->Wide(2);
  out.notes = stmt->Wide(3);
  out.recurrence = stmt->Wide(4);
  out.startDay = *start;
  out.startMin = stmt->OptInt(6);
  out.endDay = *end;
  out.endMin = stmt->OptInt(8);
  return out;
}

int Store::PendingOpCount() {
  if (!db_.IsOpen()) return 0;
  std::optional<Stmt> stmt = db_.Prepare("SELECT COUNT(*) FROM pending_ops");
  if (!stmt || !stmt->Step()) return 0;
  return static_cast<int>(stmt->Int(0));
}

// --- Writes -------------------------------------------------------------------------------

bool Store::DropOthers(const std::wstring& uid, const char* opLike, std::int64_t keep) {
  std::optional<Stmt> stmt =
      db_.Prepare("DELETE FROM pending_ops WHERE uid = ? AND op LIKE ? AND id != ?");
  if (!stmt) return false;
  stmt->Bind(1, uid);
  stmt->Bind(2, opLike);
  stmt->Bind(3, keep);
  bool ok = false;
  stmt->Step(&ok);
  return ok;
}

bool Store::QueueOp(const char* entity, const std::wstring& uid, const char* op,
                    std::int64_t* id) {
  std::optional<Stmt> stmt =
      db_.Prepare("INSERT INTO pending_ops (entity, uid, op, queued_at) VALUES (?, ?, ?, ?)");
  if (!stmt) return false;
  stmt->Bind(1, entity);
  stmt->Bind(2, uid);
  stmt->Bind(3, op);
  stmt->Bind(4, NowSeconds());
  bool ok = false;
  stmt->Step(&ok);
  if (ok && id != nullptr) *id = db_.LastInsertId();
  return ok;
}

std::string Store::DefaultCalendar(bool isTask) {
  const char* kind = isTask ? "tasklist" : "calendar";
  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT id FROM calendars WHERE kind = ? AND visible = 1 AND hidden = 0 "
          "ORDER BY is_primary DESC, sort, id LIMIT 1")) {
    stmt->Bind(1, kind);
    if (stmt->Step()) return stmt->Text(0);
  }
  // The flagged one when it can be seen; with it switched off in the sidebar, the next one that
  // can, because something created into a hidden calendar is something created and not seen.
  // Nothing at all means every calendar is hidden, and then the local one is the last resort.
  return isTask ? kLocalTaskListId : kLocalCalendarId;
}

std::vector<CalendarInfo> Store::Calendars(bool tasklists) {
  std::vector<CalendarInfo> out;
  if (!db_.IsOpen()) return out;
  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT id, title, is_primary FROM calendars "
          "WHERE kind = ? AND visible = 1 AND hidden = 0 ORDER BY sort, title")) {
    stmt->Bind(1, tasklists ? "tasklist" : "calendar");
    while (stmt->Step()) {
      CalendarInfo info;
      info.id = stmt->Text(0);
      info.title = stmt->Wide(1);
      info.isDefault = stmt->Int(2) != 0;
      out.push_back(std::move(info));
    }
  }
  return out;
}

void Store::SetDefaultCalendar(const std::string& id, bool isTask) {
  Enqueue([this, id, isTask] {
    Transaction tx(db_);
    if (!tx.Begin()) return;
    const char* kind = isTask ? "tasklist" : "calendar";
    bool ok = false;
    if (std::optional<Stmt> stmt =
            db_.Prepare("UPDATE calendars SET is_primary = 0 WHERE kind = ?")) {
      stmt->Bind(1, kind);
      stmt->Step(&ok);
    }
    if (ok) {
      if (std::optional<Stmt> stmt = db_.Prepare(
              "UPDATE calendars SET is_primary = 1 WHERE id = ? AND kind = ?")) {
        stmt->Bind(1, id);
        stmt->Bind(2, kind);
        stmt->Step(&ok);
      }
    }
    if (!ok || !tx.Commit()) {
      LogError(L"store: no se pudo cambiar el calendario por defecto");
      return;
    }
    LogInfo(L"store: lo nuevo va ahora a {}", ToWide(id));
    Notify();
  });
}

std::vector<CalendarInfo> Store::AllCalendars() {
  std::vector<CalendarInfo> out;
  if (!db_.IsOpen()) return out;
  if (std::optional<Stmt> stmt = db_.Prepare(
          "SELECT id, title, is_primary, kind, color, hidden FROM calendars "
          "WHERE visible = 1 ORDER BY kind = 'tasklist', sort, title")) {
    while (stmt->Step()) {
      CalendarInfo info;
      info.id = stmt->Text(0);
      info.title = stmt->Wide(1);
      info.isDefault = stmt->Int(2) != 0;
      info.isTaskList = stmt->Text(3) == "tasklist";
      info.color = static_cast<std::uint32_t>(stmt->Int(4));
      info.hidden = stmt->Int(5) != 0;
      out.push_back(std::move(info));
    }
  }
  return out;
}

void Store::SetCalendarHidden(const std::string& id, bool hidden) {
  Enqueue([this, id, hidden] {
    bool ok = false;
    if (std::optional<Stmt> stmt = db_.Prepare("UPDATE calendars SET hidden = ? WHERE id = ?")) {
      stmt->Bind(1, hidden ? 1 : 0);
      stmt->Bind(2, id);
      stmt->Step(&ok);
    }
    if (!ok) {
      LogError(L"store: no se pudo {} el calendario {}", hidden ? L"ocultar" : L"mostrar",
               ToWide(id));
      return;
    }
    Notify();
  });
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
  const std::string list =
      draft.calendar.empty() ? DefaultCalendar(draft.isTask) : draft.calendar;
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
      Report(uid, T(L"no se pudo empezar a guardar", L"could not start saving"));
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
      Report(uid, T(L"no se pudo guardar", L"could not save"));
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
      Report(uid, T(L"no se pudo marcar la tarea", L"could not tick the task"));
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
    // is sent, so ticking and unticking five times still has one thing to say. New one in
    // first, old ones out after (see QueueOp).
    std::int64_t queued = 0;
    if (ok) ok = QueueOp("task", uid, "update", &queued);
    if (ok) ok = DropOthers(uid, "update", queued);
    if (!ok || !tx.Commit()) {
      Report(uid, T(L"no se pudo marcar la tarea", L"could not tick the task"));
      return;
    }
    Notify();
  });
}

void Store::UpdateEvent(const EventDetail& edit, unsigned edits) {
  Enqueue([this, edit, edits] {
    Transaction tx(db_);
    if (!tx.Begin()) {
      Report(edit.uid, T(L"no se pudo guardar el cambio", L"could not save the change"));
      return;
    }

    std::string calendar;
    bool sent = false;
    std::string movedFrom;
    if (std::optional<Stmt> stmt = db_.Prepare(
            "SELECT calendar_id, remote_id IS NOT NULL, moved_from FROM events WHERE uid = ?")) {
      stmt->Bind(1, edit.uid);
      if (stmt->Step()) {
        calendar = stmt->Text(0);
        sent = stmt->Int(1) != 0;
        movedFrom = stmt->IsNull(2) ? std::string() : stmt->Text(2);
      }
    }
    if (calendar.empty()) {
      Report(edit.uid, T(L"ese evento ya no existe", L"that event no longer exists"));
      return;
    }
    // Only the first calendar counts: two changes in a row still move it out of the one Google
    // has it in. And moving it back home means there is nothing to move at all.
    if (sent && edit.calendarId != calendar && movedFrom.empty()) movedFrom = calendar;
    if (movedFrom == edit.calendarId) movedFrom.clear();

    bool ok = false;
    if (std::optional<Stmt> stmt = db_.Prepare(
            "UPDATE events SET calendar_id = ?, title = ?, notes = ?, location = ?, "
            "  start_day = ?, start_min = ?, end_day = ?, end_min = ?, recurrence = ?, "
            "  moved_from = ?, updated_at = ? WHERE uid = ?")) {
      stmt->Bind(1, edit.calendarId);
      stmt->Bind(2, edit.title);
      stmt->Bind(3, edit.notes);
      stmt->Bind(4, edit.location);
      stmt->Bind(5, DayKey(edit.startDay));
      stmt->Bind(6, edit.startMin);
      stmt->Bind(7, DayKey(edit.endDay));
      stmt->Bind(8, edit.endMin);
      stmt->Bind(9, edit.recurrence);
      if (movedFrom.empty()) {
        stmt->BindNull(10);
      } else {
        stmt->Bind(10, movedFrom);
      }
      stmt->Bind(11, NowSeconds());
      stmt->Bind(12, edit.uid);
      stmt->Step(&ok);
    }

    // One update per event, carrying everything the ones before it said had changed. New one
    // in first, old ones out after (see QueueOp).
    unsigned merged = edits;
    if (ok) {
      if (std::optional<Stmt> stmt = db_.Prepare(
              "SELECT op FROM pending_ops WHERE uid = ? AND op LIKE 'update%'")) {
        stmt->Bind(1, edit.uid);
        while (stmt->Step()) merged |= UpdateEdits(stmt->Text(0));
      }
    }
    std::int64_t queued = 0;
    if (ok) ok = QueueOp("event", edit.uid, UpdateOp(merged).c_str(), &queued);
    if (ok) ok = DropOthers(edit.uid, "update%", queued);
    if (!ok || !tx.Commit()) {
      Report(edit.uid, T(L"no se pudo guardar el cambio", L"could not save the change"));
      return;
    }
    Notify();
  });
}

void Store::Remove(const std::wstring& uid, bool isTask) {
  Enqueue([this, uid, isTask] {
    Transaction tx(db_);
    if (!tx.Begin()) {
      Report(uid, T(L"no se pudo deshacer", L"could not undo"));
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
    // nothing left to create, and an edit has nothing left to edit. The deletion goes in before
    // they go out (see QueueOp).
    std::int64_t queued = 0;
    if (ok && sent) ok = QueueOp(isTask ? "task" : "event", uid, "delete", &queued);
    if (ok) ok = DropOthers(uid, "%", queued);

    if (!ok || !tx.Commit()) {
      Report(uid, T(L"no se pudo deshacer", L"could not undo"));
      return;
    }
    Notify();
  });
}

}  // namespace agenda
