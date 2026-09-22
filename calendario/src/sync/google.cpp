#include "sync/google.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <format>
#include <string>
#include <vector>

#include "core/log.h"
#include "data/db.h"
#include "data/schema.h"
#include "data/store.h"
#include "sync/map.h"

namespace agenda::sync {
namespace {

constexpr wchar_t kApiHost[] = L"www.googleapis.com";

// Enough pages for a decade of somebody's calendar at 250 events a page, and a hard stop so a
// server that keeps handing back the same page token cannot spin this thread forever.
constexpr int kMaxPages = 40;

// After this many refusals an operation is thrown away. Without a ceiling, one row Google will
// never accept blocks everything queued behind it, for good.
constexpr int kMaxTries = 5;

// Clocks drift. Asking for everything changed since a minute before the last pass costs a few
// repeated tasks -- which are idempotent here -- and not asking costs the changes that happened
// inside the gap, silently and for ever.
constexpr std::int64_t kUpdatedMinSlackSeconds = 60;

std::int64_t NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::wstring JsonHeaders(const std::wstring& auth) {
  return auth + L"Content-Type: application/json; charset=UTF-8\r\nAccept: application/json\r\n";
}

std::string Str(const nlohmann::json& parent, const char* key) {
  const auto found = parent.find(key);
  if (found == parent.end() || !found->is_string()) return {};
  return found->get<std::string>();
}

bool Flag(const nlohmann::json& parent, const char* key) {
  const auto found = parent.find(key);
  return found != parent.end() && found->is_boolean() && found->get<bool>();
}

// A reply that is supposed to be JSON. A captive portal answers 200 with a login page, and that
// has to be a failure here rather than a calendar with no events in it.
bool ReadJson(const HttpResponse& response, nlohmann::json& out) {
  out = nlohmann::json::parse(response.body, nullptr, /*allow_exceptions=*/false);
  return !out.is_discarded() && out.is_object();
}

// The first 300 characters of an error body. Google says what it did not like -- a scope that
// was not granted, a calendar that is not there -- and that sentence is the difference between
// a bug report and a shrug. Never called on a body that could hold a token.
std::wstring Why(const HttpResponse& response) {
  const std::string body = response.body.substr(0, 300);
  return ToWide(body);
}

void BindDay(Stmt& stmt, int index, const std::optional<std::string>& day) {
  if (day) {
    stmt.Bind(index, *day);
  } else {
    stmt.BindNull(index);
  }
}

void BindStamp(Stmt& stmt, int index, const std::optional<std::int64_t>& value) {
  if (value) {
    stmt.Bind(index, *value);
  } else {
    stmt.BindNull(index);
  }
}

void DropQueued(Db& db, const std::wstring& uid) {
  if (std::optional<Stmt> stmt = db.Prepare("DELETE FROM pending_ops WHERE uid = ?")) {
    stmt->Bind(1, uid);
    stmt->Step();
  }
}

int QueuedFor(Db& db, const std::wstring& uid) {
  if (std::optional<Stmt> stmt = db.Prepare("SELECT COUNT(*) FROM pending_ops WHERE uid = ?")) {
    stmt->Bind(1, uid);
    if (stmt->Step()) return static_cast<int>(stmt->Int(0));
  }
  return 0;
}

// --- Applying what came down ----------------------------------------------------------------

void ApplyEvent(Db& db, const nlohmann::json& item, const std::string& calendarId) {
  const std::optional<EventRow> row = ReadEvent(item);
  if (!row) return;

  // Who is here already. Matched on remote_id and also on our own uid with the dashes taken
  // out, because that is the id handed to Google when Agenda creates an event: it is what saves
  // the day when a creation arrived but its answer never came back.
  std::wstring uid;
  std::int64_t localUpdated = 0;
  if (std::optional<Stmt> stmt =
          db.Prepare("SELECT uid, updated_at FROM events "
                     "WHERE remote_id = ?1 OR replace(lower(uid), '-', '') = ?1 LIMIT 1")) {
    stmt->Bind(1, row->remoteId);
    if (stmt->Step()) {
      uid = stmt->Wide(0);
      localUpdated = stmt->Int(1);
    }
  }

  if (row->cancelled) {
    // Deleted at Google is deleted here, row and queue together. A tombstone is for a deletion
    // of ours that still has to go up; this one has already happened at both ends.
    if (uid.empty()) return;
    if (std::optional<Stmt> stmt = db.Prepare("DELETE FROM events WHERE uid = ?")) {
      stmt->Bind(1, uid);
      stmt->Step();
    }
    DropQueued(db, uid);
    LogInfo(L"sync: evento borrado en Google, quitado de aquí ({})", uid);
    return;
  }

  if (uid.empty()) {
    uid = NewUid();
    std::optional<Stmt> stmt = db.Prepare(
        "INSERT INTO events (uid, calendar_id, title, notes, start_day, start_min, end_day, "
        "                    end_min, recurrence, remote_id, etag, updated_at, location) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt) return;
    stmt->Bind(1, uid);
    stmt->Bind(2, calendarId);
    stmt->Bind(3, row->title);
    stmt->Bind(4, row->notes);
    stmt->Bind(5, row->startDay);
    stmt->Bind(6, row->startMin);
    stmt->Bind(7, row->endDay);
    stmt->Bind(8, row->endMin);
    stmt->Bind(9, row->recurrence);
    stmt->Bind(10, row->remoteId);
    stmt->Bind(11, row->etag);
    stmt->Bind(12, row->updatedAt);
    stmt->Bind(13, row->location);
    stmt->Step();
    return;
  }

  // The etag is Google's bookkeeping and not the user's data, so it is refreshed whoever wins.
  // It is what a retried PATCH after a 412 needs in order to go through.
  //
  // The location rides along when there is none here: rows cached before schema v2 never had
  // the column, and this is how they learn it without losing to a tie on updated_at. A location
  // typed here is never overwritten this way.
  if (std::optional<Stmt> stmt = db.Prepare(
          "UPDATE events SET etag = ?, remote_id = ?, "
          "  location = CASE WHEN location = '' THEN ? ELSE location END WHERE uid = ?")) {
    stmt->Bind(1, row->etag);
    stmt->Bind(2, row->remoteId);
    stmt->Bind(3, row->location);
    stmt->Bind(4, uid);
    stmt->Step();
  }

  if (!RemoteWins(row->updatedAt, localUpdated)) return;

  // Losing a local change is the only case where something of the user's is thrown away, and it
  // does not happen quietly.
  if (QueuedFor(db, uid) > 0) {
    LogInfo(L"sync: conflicto en {} -- gana Google ({} > {}), se pierde un cambio local", uid,
            row->updatedAt, localUpdated);
    DropQueued(db, uid);
  }

  std::optional<Stmt> stmt = db.Prepare(
      "UPDATE events SET calendar_id = ?, title = ?, notes = ?, start_day = ?, start_min = ?, "
      "                  end_day = ?, end_min = ?, recurrence = ?, updated_at = ?, "
      "                  location = ?, moved_from = NULL, deleted_at = NULL "
      "WHERE uid = ?");
  if (!stmt) return;
  stmt->Bind(1, calendarId);
  stmt->Bind(2, row->title);
  stmt->Bind(3, row->notes);
  stmt->Bind(4, row->startDay);
  stmt->Bind(5, row->startMin);
  stmt->Bind(6, row->endDay);
  stmt->Bind(7, row->endMin);
  stmt->Bind(8, row->recurrence);
  stmt->Bind(9, row->updatedAt);
  stmt->Bind(10, row->location);
  stmt->Bind(11, uid);
  stmt->Step();
}

void ApplyTask(Db& db, const nlohmann::json& item, const std::string& listId) {
  const std::optional<TaskRow> row = ReadTask(item);
  if (!row) return;

  std::wstring uid;
  std::int64_t localUpdated = 0;
  std::string localDay;
  bool hadDay = false;
  std::optional<int> localMin;
  if (std::optional<Stmt> stmt = db.Prepare(
          "SELECT uid, updated_at, due_day, due_min FROM tasks WHERE remote_id = ? LIMIT 1")) {
    stmt->Bind(1, row->remoteId);
    if (stmt->Step()) {
      uid = stmt->Wide(0);
      localUpdated = stmt->Int(1);
      hadDay = !stmt->IsNull(2);
      localDay = stmt->Text(2);
      localMin = stmt->OptInt(3);
    }
  }

  if (row->deleted) {
    if (uid.empty()) return;
    if (std::optional<Stmt> stmt = db.Prepare("DELETE FROM tasks WHERE uid = ?")) {
      stmt->Bind(1, uid);
      stmt->Step();
    }
    DropQueued(db, uid);
    LogInfo(L"sync: tarea borrada en Google, quitada de aquí ({})", uid);
    return;
  }

  // Google Tasks has nowhere to put the hour of a task -- it keeps the day and throws the rest
  // away -- so the local hour is kept for as long as the day has not moved. Dropping it on
  // every pass would make "pagar luz a las 9" quietly become "pagar luz" five minutes later.
  const bool sameDay = hadDay && row->dueDay && localDay == *row->dueDay;
  const std::optional<int> keptMin = sameDay ? localMin : std::nullopt;

  if (uid.empty()) {
    uid = NewUid();
    std::optional<Stmt> stmt = db.Prepare(
        "INSERT INTO tasks (uid, list_id, title, notes, due_day, due_min, done_at, position, "
        "                   remote_id, etag, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt) return;
    stmt->Bind(1, uid);
    stmt->Bind(2, listId);
    stmt->Bind(3, row->title);
    stmt->Bind(4, row->notes);
    BindDay(*stmt, 5, row->dueDay);
    stmt->BindNull(6);  // a task born at Google never had an hour to keep
    BindStamp(*stmt, 7, row->doneAt);
    stmt->Bind(8, row->position);
    stmt->Bind(9, row->remoteId);
    stmt->Bind(10, row->etag);
    stmt->Bind(11, row->updatedAt);
    stmt->Step();
    return;
  }

  if (std::optional<Stmt> stmt = db.Prepare("UPDATE tasks SET etag = ? WHERE uid = ?")) {
    stmt->Bind(1, row->etag);
    stmt->Bind(2, uid);
    stmt->Step();
  }

  if (!RemoteWins(row->updatedAt, localUpdated)) return;

  if (QueuedFor(db, uid) > 0) {
    LogInfo(L"sync: conflicto en {} -- gana Google ({} > {}), se pierde un cambio local", uid,
            row->updatedAt, localUpdated);
    DropQueued(db, uid);
  }

  std::optional<Stmt> stmt = db.Prepare(
      "UPDATE tasks SET list_id = ?, title = ?, notes = ?, due_day = ?, due_min = ?, "
      "                 done_at = ?, position = ?, updated_at = ?, deleted_at = NULL "
      "WHERE uid = ?");
  if (!stmt) return;
  stmt->Bind(1, listId);
  stmt->Bind(2, row->title);
  stmt->Bind(3, row->notes);
  BindDay(*stmt, 4, row->dueDay);
  stmt->Bind(5, keptMin);
  BindStamp(*stmt, 6, row->doneAt);
  stmt->Bind(7, row->position);
  stmt->Bind(8, row->updatedAt);
  stmt->Bind(9, uid);
  stmt->Step();
}

// --- Reading a row back out, to send it -----------------------------------------------------

struct Outgoing {
  bool found = false;
  bool deleted = false;
  std::string container;  // calendar_id or list_id
  std::string remoteId;
  std::string etag;
  std::string movedFrom;  // events only: the calendar Google still has it in
  EventRow event;
  TaskRow task;
};

Outgoing LoadEvent(Db& db, const std::wstring& uid) {
  Outgoing out;
  std::optional<Stmt> stmt = db.Prepare(
      "SELECT calendar_id, title, notes, start_day, start_min, end_day, end_min, recurrence, "
      "       remote_id, etag, updated_at, deleted_at, location, moved_from "
      "FROM events WHERE uid = ?");
  if (!stmt) return out;
  stmt->Bind(1, uid);
  if (!stmt->Step()) return out;

  out.found = true;
  out.container = stmt->Text(0);
  out.event.title = stmt->Text(1);
  out.event.notes = stmt->Text(2);
  out.event.startDay = stmt->Text(3);
  out.event.startMin = stmt->OptInt(4);
  out.event.endDay = stmt->Text(5);
  out.event.endMin = stmt->OptInt(6);
  out.event.recurrence = stmt->Text(7);
  out.remoteId = stmt->IsNull(8) ? std::string() : stmt->Text(8);
  out.etag = stmt->IsNull(9) ? std::string() : stmt->Text(9);
  out.event.updatedAt = stmt->Int(10);
  out.deleted = !stmt->IsNull(11);
  out.event.location = stmt->Text(12);
  out.movedFrom = stmt->IsNull(13) ? std::string() : stmt->Text(13);
  return out;
}

Outgoing LoadTask(Db& db, const std::wstring& uid) {
  Outgoing out;
  std::optional<Stmt> stmt = db.Prepare(
      "SELECT list_id, title, notes, due_day, done_at, position, remote_id, etag, updated_at, "
      "       deleted_at FROM tasks WHERE uid = ?");
  if (!stmt) return out;
  stmt->Bind(1, uid);
  if (!stmt->Step()) return out;

  out.found = true;
  out.container = stmt->Text(0);
  out.task.title = stmt->Text(1);
  out.task.notes = stmt->Text(2);
  if (!stmt->IsNull(3)) out.task.dueDay = stmt->Text(3);
  if (!stmt->IsNull(4)) out.task.doneAt = stmt->Int(4);
  out.task.position = stmt->Text(5);
  out.remoteId = stmt->IsNull(6) ? std::string() : stmt->Text(6);
  out.etag = stmt->IsNull(7) ? std::string() : stmt->Text(7);
  out.task.updatedAt = stmt->Int(8);
  out.deleted = !stmt->IsNull(9);
  return out;
}

}  // namespace

// --- The thread -----------------------------------------------------------------------------

GoogleSync::GoogleSync(Store& store, OAuthConfig config)
    : store_(store), auth_(std::move(config)) {}

GoogleSync::~GoogleSync() { Stop(); }

void GoogleSync::Start() {
  if (!auth_.Configured()) {
    LogInfo(L"sync: sin credenciales de Google, Agenda funciona solo en este equipo");
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_) return;
    running_ = true;
  }
  if (!http_.Open()) LogError(L"sync: {}", http_.error());
  worker_ = std::thread([this] { Loop(); });
  if (auth_.Connected()) Nudge();
}

void GoogleSync::Stop() {
  if (!worker_.joinable()) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
  }
  // Closing the handle is what ends a wait that has already started. Without it, quitting in
  // the middle of a pass would take as long as the receive timeout.
  http_.Cancel();
  wake_.notify_all();
  worker_.join();
  http_.Close();
}

void GoogleSync::Nudge(std::chrono::seconds notBefore) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) return;
  if (notBefore.count() > 0 && lastPass_.time_since_epoch().count() != 0 &&
      std::chrono::steady_clock::now() - lastPass_ < notBefore) {
    return;
  }
  wanted_ = true;
  pushOnly_ = false;
  wake_.notify_one();
}

void GoogleSync::Push() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) return;
  // Only upgrades: a full pass already asked for does not become a push.
  if (!wanted_) pushOnly_ = true;
  wanted_ = true;
  wake_.notify_one();
}

void GoogleSync::Connect() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!running_) return;
  connectWanted_ = true;
  wake_.notify_one();
}

void GoogleSync::Disconnect() {
  auth_.Disconnect();
  offline_.store(false);
  store_.Touch();
}

void GoogleSync::Loop() {
  for (;;) {
    bool connect = false;
    bool pushOnly = false;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      // Five minutes is the heartbeat. Everything else -- opening the popup, creating
      // something -- arrives as a nudge and cuts the wait short.
      wake_.wait_for(lock, std::chrono::minutes(5),
                     [this] { return !running_ || wanted_ || connectWanted_; });
      if (!running_) return;
      connect = connectWanted_;
      pushOnly = wanted_ && pushOnly_;
      connectWanted_ = false;
      wanted_ = false;
      pushOnly_ = false;
      if (!connect) lastPass_ = std::chrono::steady_clock::now();
    }

    if (connect) {
      // The asking already happened, in front of a window. This is the browser opening.
      if (auth_.Connect()) {
        store_.Touch();
        RunPass(/*pushOnly=*/false);
      } else {
        store_.Touch();
      }
      continue;
    }
    RunPass(pushOnly);
  }
}

void GoogleSync::RunPass(bool pushOnly) {
  if (!auth_.Configured() || !auth_.Connected()) return;

  std::wstring header;
  if (!auth_.Header(header)) {
    // No usable permission. Either the network is down -- which the dot already says by itself
    // -- or Google refused to renew, which is a different thing and the only one the user has
    // to be told about, because it is the only one they can do something about.
    if (auth_.TakeLostAccount()) {
      offline_.store(false);
      if (hwnd_ != nullptr) PostMessageW(hwnd_, kSyncLostAccountMessage, 0, 0);
    } else {
      offline_.store(!http_.reachable());
    }
    store_.Touch();
    return;
  }

  bool ok = PushPending(header);
  if (ok && !pushOnly) {
    if (PullCalendars(header)) {
      AdoptLocalRows();

      // Even a read goes through the writing thread. One connection means a read taken from
      // here could land inside a transaction the worker has open.
      std::vector<CalendarInfo> calendars;
      std::vector<CalendarInfo> lists;
      store_.Run([&] {
        calendars = store_.Calendars(/*tasklists=*/false);
        lists = store_.Calendars(/*tasklists=*/true);
      });

      for (const CalendarInfo& calendar : calendars) {
        if (calendar.id == kLocalCalendarId) continue;
        if (!PullEvents(header, calendar.id)) {
          ok = false;
          break;
        }
      }
      if (ok) {
        for (const CalendarInfo& list : lists) {
          if (list.id == kLocalTaskListId) continue;
          if (!PullTasks(header, list.id)) {
            ok = false;
            break;
          }
        }
      }
    } else {
      ok = false;
    }
  }

  offline_.store(!http_.reachable());
  store_.Touch();
  if (ok) LogInfo(L"sync: pasada terminada");
}

// --- Down ----------------------------------------------------------------------------------

bool GoogleSync::PullCalendars(const std::wstring& auth) {
  HttpResponse response;
  if (!http_.Send(kApiHost, L"GET", L"/calendar/v3/users/me/calendarList?maxResults=250",
                  JsonHeaders(auth), {}, response)) {
    return false;
  }
  if (response.status != 200) {
    LogError(L"sync: la lista de calendarios devolvió {}: {}", response.status, Why(response));
    return false;
  }
  nlohmann::json parsed;
  if (!ReadJson(response, parsed)) {
    LogError(L"sync: la lista de calendarios no es JSON");
    return false;
  }

  HttpResponse listsResponse;
  const bool gotLists = http_.Send(kApiHost, L"GET", L"/tasks/v1/users/@me/lists?maxResults=100",
                                   JsonHeaders(auth), {}, listsResponse);
  nlohmann::json lists;
  if (gotLists && listsResponse.status == 200) {
    if (!ReadJson(listsResponse, lists)) lists = nlohmann::json::object();
  } else if (gotLists) {
    // Almost always the Tasks API not enabled in the Cloud project. Events keep working, so
    // this is said once and the pass goes on.
    LogError(L"sync: la lista de tareas devolvió {}: {}", listsResponse.status,
             Why(listsResponse));
  }

  store_.Run([&] {
    Transaction tx(store_.db());
    if (!tx.Begin()) return;

    // Everything that is not the local placeholder goes invisible first, and what comes down
    // turns itself back on. That is how a calendar unticked on the web stops painting dots here
    // without a row being deleted -- which the schema will not allow while events hang off it.
    if (std::optional<Stmt> stmt = store_.db().Prepare(
            "UPDATE calendars SET visible = 0 WHERE id NOT IN (?, ?)")) {
      stmt->Bind(1, kLocalCalendarId);
      stmt->Bind(2, kLocalTaskListId);
      stmt->Step();
    }

    const auto upsert = [&](const std::string& id, const char* kind, const std::string& title,
                            std::uint32_t color, const std::string& zone, bool primary,
                            int sort) {
      std::optional<Stmt> stmt = store_.db().Prepare(
          "INSERT INTO calendars (id, kind, title, color, time_zone, is_primary, visible, sort) "
          "VALUES (?1, ?2, ?3, ?4, ?5, ?6, 1, ?7) "
          "ON CONFLICT(id) DO UPDATE SET title = excluded.title, color = excluded.color, "
          "  time_zone = excluded.time_zone, visible = 1, sort = excluded.sort");
      if (!stmt) return;
      stmt->Bind(1, id);
      stmt->Bind(2, kind);
      stmt->Bind(3, title);
      stmt->Bind(4, static_cast<std::int64_t>(color));
      stmt->Bind(5, zone);
      // is_primary is only ever set when the row is born. On a later pass it is left alone,
      // because by then it means what the tray menu last said and not what Google thinks.
      stmt->Bind(6, primary ? 1 : 0);
      stmt->Bind(7, sort);
      stmt->Step();

      if (std::optional<Stmt> state =
              store_.db().Prepare("INSERT OR IGNORE INTO sync_state (id) VALUES (?)")) {
        state->Bind(1, id);
        state->Step();
      }
    };

    int sort = 0;
    std::string fallbackCalendar;
    if (const auto items = parsed.find("items"); items != parsed.end() && items->is_array()) {
      for (const nlohmann::json& item : *items) {
        if (!item.is_object()) continue;
        const std::string id = Str(item, "id");
        if (id.empty()) continue;
        // Only what is ticked on the web. Somebody who hid a calendar there hid it on purpose,
        // and downloading it to not show it would be work nobody asked for.
        if (!Flag(item, "selected") && !Flag(item, "primary")) continue;

        std::string title = Str(item, "summaryOverride");
        if (title.empty()) title = Str(item, "summary");
        if (title.empty()) title = id;
        const std::uint32_t color = ReadColor(Str(item, "backgroundColor")).value_or(0x4A8BF5u);
        if (Flag(item, "primary") || fallbackCalendar.empty()) fallbackCalendar = id;
        upsert(id, "calendar", title, color, Str(item, "timeZone"), Flag(item, "primary"),
               sort++);
      }
    }

    int listSort = 0;
    std::string fallbackList;
    if (const auto items = lists.find("items"); items != lists.end() && items->is_array()) {
      for (const nlohmann::json& item : *items) {
        if (!item.is_object()) continue;
        const std::string id = Str(item, "id");
        if (id.empty()) continue;
        std::string title = Str(item, "title");
        if (title.empty()) title = id;
        if (fallbackList.empty()) fallbackList = id;
        // Google Tasks has no colours, so every list keeps the amber the design system gives
        // tasks. Inventing one per list would be a colour that means nothing.
        upsert(id, "tasklist", title, 0xF5A623u, {}, listSort == 0, listSort);
        ++listSort;
      }
    }

    // Whatever the tray menu chose stays chosen, as long as it is still there. Only when there
    // is nothing flagged -- the first connection -- does Google's primary get the flag.
    const auto ensure = [&](const char* kind, const std::string& fallback) {
      if (fallback.empty()) return;
      bool has = false;
      if (std::optional<Stmt> stmt = store_.db().Prepare(
              "SELECT COUNT(*) FROM calendars WHERE kind = ? AND is_primary = 1 AND visible = 1 "
              "AND id NOT IN (?, ?)")) {
        stmt->Bind(1, kind);
        stmt->Bind(2, kLocalCalendarId);
        stmt->Bind(3, kLocalTaskListId);
        if (stmt->Step()) has = stmt->Int(0) > 0;
      }
      if (has) return;
      if (std::optional<Stmt> stmt =
              store_.db().Prepare("UPDATE calendars SET is_primary = 0 WHERE kind = ?")) {
        stmt->Bind(1, kind);
        stmt->Step();
      }
      if (std::optional<Stmt> stmt =
              store_.db().Prepare("UPDATE calendars SET is_primary = 1 WHERE id = ?")) {
        stmt->Bind(1, fallback);
        stmt->Step();
      }
    };
    ensure("calendar", fallbackCalendar);
    ensure("tasklist", fallbackList);

    tx.Commit();
  });
  return true;
}

bool GoogleSync::PullEvents(const std::wstring& auth, const std::string& calendarId) {
  std::string token;
  store_.Run([&] {
    if (std::optional<Stmt> stmt =
            store_.db().Prepare("SELECT sync_token FROM sync_state WHERE id = ?")) {
      stmt->Bind(1, calendarId);
      if (stmt->Step()) token = stmt->Text(0);
    }
  });

  const std::string base = "/calendar/v3/calendars/" + UrlEscape(calendarId) + "/events";
  std::string pageToken;
  std::string nextToken;
  bool restarted = false;

  for (int page = 0; page < kMaxPages; ++page) {
    // showDeleted is what makes a cancellation arrive at all; singleEvents stays false because
    // a repetition is stored as its rule and not unfolded (CLAUDE.md). timeMin is absent on
    // purpose: Google refuses to hand back a syncToken for a query that carries one, so the
    // first pass downloads the whole calendar and every one after it downloads the difference.
    std::string query = "?maxResults=250&showDeleted=true&singleEvents=false";
    if (!token.empty()) query += "&syncToken=" + UrlEscape(token);
    if (!pageToken.empty()) query += "&pageToken=" + UrlEscape(pageToken);

    HttpResponse response;
    if (!http_.Send(kApiHost, L"GET", ToWide(base + query), JsonHeaders(auth), {}, response)) {
      return false;
    }

    if (response.status == 410) {
      if (restarted) {
        LogError(L"sync: {} sigue devolviendo 410 tras empezar de cero", ToWide(calendarId));
        return false;
      }
      // The token is too old to be useful. Everything has to come down again, and what is here
      // has to go first: an event deleted at Google while Agenda was closed is not mentioned by
      // a full listing, so it would survive as a row nobody ever contradicts. Rows with
      // something queued are left alone -- they are the user's, and they have not gone up yet.
      LogInfo(L"sync: el syncToken de {} caducó, se sincroniza de cero", ToWide(calendarId));
      store_.Run([&] {
        Transaction tx(store_.db());
        if (!tx.Begin()) return;
        if (std::optional<Stmt> stmt = store_.db().Prepare(
                "DELETE FROM events WHERE calendar_id = ? AND remote_id IS NOT NULL "
                "AND uid NOT IN (SELECT uid FROM pending_ops)")) {
          stmt->Bind(1, calendarId);
          stmt->Step();
        }
        if (std::optional<Stmt> stmt = store_.db().Prepare(
                "UPDATE sync_state SET sync_token = '' WHERE id = ?")) {
          stmt->Bind(1, calendarId);
          stmt->Step();
        }
        tx.Commit();
      });
      token.clear();
      pageToken.clear();
      restarted = true;
      continue;
    }

    if (response.status != 200) {
      LogError(L"sync: {} devolvió {}: {}", ToWide(calendarId), response.status, Why(response));
      return false;
    }

    nlohmann::json parsed;
    if (!ReadJson(response, parsed)) {
      LogError(L"sync: la respuesta de {} no es JSON", ToWide(calendarId));
      return false;
    }

    if (const auto items = parsed.find("items"); items != parsed.end() && items->is_array()) {
      // One transaction per page and not per event, and not per pass either: a page is a unit
      // small enough that the writing thread is never held for long.
      store_.Run([&] {
        Transaction tx(store_.db());
        if (!tx.Begin()) return;
        for (const nlohmann::json& item : *items) ApplyEvent(store_.db(), item, calendarId);
        tx.Commit();
      });
    }

    pageToken = Str(parsed, "nextPageToken");
    if (const std::string given = Str(parsed, "nextSyncToken"); !given.empty()) nextToken = given;
    if (pageToken.empty()) break;
  }

  // Written only once the last page is in. Saving it halfway would mean the next pass asks for
  // the difference since a point that was never reached, and the missing middle never arrives.
  if (!nextToken.empty()) {
    store_.Run([&] {
      if (std::optional<Stmt> stmt = store_.db().Prepare(
              "UPDATE sync_state SET sync_token = ?, synced_at = ? WHERE id = ?")) {
        stmt->Bind(1, nextToken);
        stmt->Bind(2, NowSeconds());
        stmt->Bind(3, calendarId);
        stmt->Step();
      }
    });
  }
  return true;
}

bool GoogleSync::PullTasks(const std::wstring& auth, const std::string& listId) {
  std::string updatedMin;
  store_.Run([&] {
    if (std::optional<Stmt> stmt =
            store_.db().Prepare("SELECT updated_min FROM sync_state WHERE id = ?")) {
      stmt->Bind(1, listId);
      if (stmt->Step()) updatedMin = stmt->Text(0);
    }
  });

  const std::string base = "/tasks/v1/lists/" + UrlEscape(listId) + "/tasks";
  const std::int64_t startedAt = NowSeconds();
  std::string pageToken;

  for (int page = 0; page < kMaxPages; ++page) {
    std::string query = "?maxResults=100&showCompleted=true&showHidden=true&showDeleted=true";
    if (!updatedMin.empty()) query += "&updatedMin=" + UrlEscape(updatedMin);
    if (!pageToken.empty()) query += "&pageToken=" + UrlEscape(pageToken);

    HttpResponse response;
    if (!http_.Send(kApiHost, L"GET", ToWide(base + query), JsonHeaders(auth), {}, response)) {
      return false;
    }
    if (response.status != 200) {
      LogError(L"sync: la lista {} devolvió {}: {}", ToWide(listId), response.status,
               Why(response));
      return false;
    }
    nlohmann::json parsed;
    if (!ReadJson(response, parsed)) {
      LogError(L"sync: la respuesta de {} no es JSON", ToWide(listId));
      return false;
    }

    if (const auto items = parsed.find("items"); items != parsed.end() && items->is_array()) {
      store_.Run([&] {
        Transaction tx(store_.db());
        if (!tx.Begin()) return;
        for (const nlohmann::json& item : *items) ApplyTask(store_.db(), item, listId);
        tx.Commit();
      });
    }

    pageToken = Str(parsed, "nextPageToken");
    if (pageToken.empty()) break;
  }

  // The cursor is the moment this pass STARTED, less a minute of slack, and it is written only
  // once the pass finished. A clock ten minutes fast would otherwise skip ten minutes of
  // changes for ever.
  const std::string cursor = FormatInstant(startedAt - kUpdatedMinSlackSeconds);
  store_.Run([&] {
    if (std::optional<Stmt> stmt = store_.db().Prepare(
            "UPDATE sync_state SET updated_min = ?, synced_at = ? WHERE id = ?")) {
      stmt->Bind(1, cursor);
      stmt->Bind(2, NowSeconds());
      stmt->Bind(3, listId);
      stmt->Step();
    }
  });
  return true;
}

// --- Up ------------------------------------------------------------------------------------

void GoogleSync::AdoptLocalRows() {
  store_.Run([&] {
    // Asked for explicitly instead of through Store::DefaultCalendar, because until this
    // function has run there may be TWO rows of a kind flagged as the default -- the seeded
    // placeholder and the one that just came down -- and that question would be answered by
    // whichever identifier happens to sort first.
    const auto realOne = [&](const char* kind) {
      std::string id;
      if (std::optional<Stmt> stmt = store_.db().Prepare(
              "SELECT id FROM calendars WHERE kind = ? AND is_primary = 1 AND visible = 1 "
              "AND id NOT IN (?, ?) ORDER BY sort, id LIMIT 1")) {
        stmt->Bind(1, kind);
        stmt->Bind(2, kLocalCalendarId);
        stmt->Bind(3, kLocalTaskListId);
        if (stmt->Step()) id = stmt->Text(0);
      }
      return id;
    };

    const std::string calendar = realOne("calendar");
    const std::string list = realOne("tasklist");
    if (calendar.empty() && list.empty()) return;  // no account behind them yet

    Transaction tx(store_.db());
    if (!tx.Begin()) return;
    int moved = 0;

    // Moving what was created without an account, and retiring the placeholder, are one thing
    // and not two. Doing the second only when the first moved something is what left this
    // machine with two calendars flagged as the default: there was nothing local to move.
    const auto adopt = [&](const std::string& target, const char* placeholder, const char* sql) {
      if (target.empty()) return;
      if (std::optional<Stmt> stmt = store_.db().Prepare(sql)) {
        stmt->Bind(1, target);
        stmt->Bind(2, placeholder);
        stmt->Step();
        moved += store_.db().Changes();
      }
      // The placeholder stops being offered. It is not deleted: the schema will not let a
      // calendar go while anything still points at it, and there is no reason to find that out
      // the hard way.
      if (std::optional<Stmt> stmt = store_.db().Prepare(
              "UPDATE calendars SET is_primary = 0, visible = 0 WHERE id = ?")) {
        stmt->Bind(1, placeholder);
        stmt->Step();
      }
    };

    adopt(calendar, kLocalCalendarId, "UPDATE events SET calendar_id = ? WHERE calendar_id = ?");
    adopt(list, kLocalTaskListId, "UPDATE tasks SET list_id = ? WHERE list_id = ?");

    if (moved > 0) {
      LogInfo(L"sync: {} cosas creadas sin cuenta pasan al calendario elegido", moved);
    }
    tx.Commit();
  });
}

bool GoogleSync::PushPending(const std::wstring& auth) {
  struct Queued {
    std::int64_t id = 0;
    std::string entity;
    std::wstring uid;
    std::string op;
    int tries = 0;
  };
  std::vector<Queued> queue;
  store_.Run([&] {
    // By id ascending, because the autoincrement IS the order things happened in.
    if (std::optional<Stmt> stmt = store_.db().Prepare(
            "SELECT id, entity, uid, op, tries FROM pending_ops ORDER BY id")) {
      while (stmt->Step()) {
        queue.push_back(Queued{stmt->Int(0), stmt->Text(1), stmt->Wide(2), stmt->Text(3),
                               static_cast<int>(stmt->Int(4))});
      }
    }
  });
  if (queue.empty()) return true;

  const std::wstring headers = JsonHeaders(auth);

  for (const Queued& item : queue) {
    if (http_.Cancelled()) return false;

    const bool isTask = item.entity == "task";
    Outgoing row;
    store_.Run([&] {
      row = isTask ? LoadTask(store_.db(), item.uid) : LoadEvent(store_.db(), item.uid);
    });

    const auto forget = [&] {
      store_.Run([&] {
        if (std::optional<Stmt> stmt =
                store_.db().Prepare("DELETE FROM pending_ops WHERE id = ?")) {
          stmt->Bind(1, item.id);
          stmt->Step();
        }
      });
    };

    // A refusal that repeating will not fix. The count goes up, and after enough of them the
    // operation is thrown away with the reason written down -- one poisoned row must not block
    // everything behind it for ever.
    const auto refuse = [&](const HttpResponse& answer) {
      const int tries = item.tries + 1;
      const std::wstring reason = std::format(L"{}: {}", answer.status, Why(answer));
      LogError(L"sync: Google rechazó {} ({} de {}) -- {}", item.uid, tries, kMaxTries, reason);
      store_.Run([&] {
        if (tries >= kMaxTries) {
          if (std::optional<Stmt> stmt =
                  store_.db().Prepare("DELETE FROM pending_ops WHERE id = ?")) {
            stmt->Bind(1, item.id);
            stmt->Step();
          }
        } else if (std::optional<Stmt> stmt = store_.db().Prepare(
                       "UPDATE pending_ops SET tries = ?, last_err = ? WHERE id = ?")) {
          stmt->Bind(1, tries);
          stmt->Bind(2, reason);
          stmt->Bind(3, item.id);
          stmt->Step();
        }
      });
      if (tries >= kMaxTries) {
        store_.Report(item.uid, L"Google no aceptó este cambio");
      }
    };

    // The row went away underneath its operation -- undone, or overwritten by a pull that won.
    if (!row.found) {
      forget();
      continue;
    }
    // Still sitting in the local placeholder, which means no account has adopted it yet.
    // Sending it would put it in a calendar Google has never heard of.
    if (row.container == kLocalCalendarId || row.container == kLocalTaskListId) continue;

    const bool deleting = item.op == "delete" || row.deleted;
    const unsigned edits = UpdateEdits(item.op);

    // Moved to another calendar here: Google moves it with a POST on the calendar it is still
    // in, and only then will a PATCH on the new one find it. A 404 there means it is not in the
    // old calendar any more -- a move whose answer got lost -- so the PATCH gets its chance.
    if (!isTask && !deleting && !row.remoteId.empty() && !row.movedFrom.empty()) {
      HttpResponse moved;
      const std::wstring movePath =
          ToWide(MovePath(row.movedFrom, row.remoteId, row.container));
      if (!http_.Send(kApiHost, L"POST", movePath, headers, {}, moved)) {
        LogInfo(L"sync: sin conexión, quedan {} operaciones en la cola", queue.size());
        return false;
      }
      if (ShouldRetry(moved.status)) {
        LogError(L"sync: Google pide calma ({}), la cola espera", moved.status);
        return false;
      }
      const bool movedOk = moved.status >= 200 && moved.status < 300;
      if (!movedOk && moved.status != 404) {
        refuse(moved);
        continue;
      }
      if (movedOk) {
        nlohmann::json parsed;
        if (ReadJson(moved, parsed)) row.etag = Str(parsed, "etag");
      }
      LogInfo(L"sync: {} pasa de {} a {}", item.uid, ToWide(row.movedFrom),
              ToWide(row.container));
      store_.Run([&] {
        if (std::optional<Stmt> stmt = store_.db().Prepare(
                "UPDATE events SET moved_from = NULL, etag = ? WHERE uid = ?")) {
          stmt->Bind(1, row.etag);
          stmt->Bind(2, item.uid);
          stmt->Step();
        }
      });
    }

    std::wstring path;
    const wchar_t* verb = L"POST";
    std::string body;
    std::wstring requestHeaders = headers;

    if (isTask) {
      const std::wstring base = L"/tasks/v1/lists/" + ToWide(UrlEscape(row.container)) + L"/tasks";
      if (deleting) {
        if (row.remoteId.empty()) {
          forget();
          continue;
        }
        verb = L"DELETE";
        path = base + L"/" + ToWide(UrlEscape(row.remoteId));
      } else if (row.remoteId.empty()) {
        // Tasks will not take an identifier from the client, so there is no way to make this
        // idempotent. If the answer to a creation is lost, the retry makes a second task.
        // ponytail: aceptado, el arreglo seria un id propio que la API no admite.
        verb = L"POST";
        path = base;
        body = WriteTask(row.task).dump();
      } else {
        verb = L"PATCH";
        path = base + L"/" + ToWide(UrlEscape(row.remoteId));
        body = WriteTask(row.task).dump();
      }
    } else {
      const std::wstring base =
          L"/calendar/v3/calendars/" + ToWide(UrlEscape(row.container)) + L"/events";
      if (deleting) {
        if (row.remoteId.empty()) {
          forget();
          continue;
        }
        verb = L"DELETE";
        path = base + L"/" + ToWide(UrlEscape(row.remoteId));
      } else if (row.remoteId.empty()) {
        verb = L"POST";
        path = base;
        // Our own identifier, which is what makes this safe to send twice.
        body = WriteEvent(row.event, EventIdFor(item.uid), edits).dump();
      } else {
        verb = L"PATCH";
        path = base + L"/" + ToWide(UrlEscape(row.remoteId));
        body = WriteEvent(row.event, {}, edits).dump();
        if (!row.etag.empty()) requestHeaders += L"If-Match: " + ToWide(row.etag) + L"\r\n";
      }
    }

    HttpResponse response;
    if (!http_.Send(kApiHost, verb, path, requestHeaders, body, response)) {
      // Nobody was reached. Everything stays where it is and the next pass tries again: this is
      // the whole of "offline loses nothing".
      LogInfo(L"sync: sin conexión, quedan {} operaciones en la cola", queue.size());
      return false;
    }

    const int status = response.status;
    const bool created = status >= 200 && status < 300;
    // A 409 on a creation is our own identifier coming back: it is already there, which is the
    // answer we wanted. A 404 or 410 on a deletion is the same kind of good news.
    const bool alreadyThere = status == 409 && !deleting;
    const bool alreadyGone = (status == 404 || status == 410) && deleting;

    if (created || alreadyThere || alreadyGone) {
      std::string remoteId = row.remoteId;
      std::string etag;
      if (created && !response.body.empty()) {
        nlohmann::json parsed;
        if (ReadJson(response, parsed)) {
          if (const std::string given = Str(parsed, "id"); !given.empty()) remoteId = given;
          etag = Str(parsed, "etag");
        }
      }
      if (alreadyThere && remoteId.empty()) remoteId = EventIdFor(item.uid);

      store_.Run([&] {
        Transaction tx(store_.db());
        if (!tx.Begin()) return;
        if (deleting) {
          // The tombstone has done its job.
          if (std::optional<Stmt> stmt = store_.db().Prepare(
                  isTask ? "DELETE FROM tasks WHERE uid = ?" : "DELETE FROM events WHERE uid = ?")) {
            stmt->Bind(1, item.uid);
            stmt->Step();
          }
        } else {
          if (std::optional<Stmt> stmt = store_.db().Prepare(
                  isTask ? "UPDATE tasks SET remote_id = ?, etag = ? WHERE uid = ?"
                         : "UPDATE events SET remote_id = ?, etag = ? WHERE uid = ?")) {
            stmt->Bind(1, remoteId);
            stmt->Bind(2, etag);
            stmt->Bind(3, item.uid);
            stmt->Step();
          }
        }
        if (std::optional<Stmt> stmt =
                store_.db().Prepare("DELETE FROM pending_ops WHERE id = ?")) {
          stmt->Bind(1, item.id);
          stmt->Step();
        }
        tx.Commit();
      });
      continue;
    }

    if (status == 412) {
      // Somebody edited it between our reading the etag and our sending it. The rule is the
      // newest wins, so the current version is fetched and compared; ApplyEvent keeps whichever
      // is newer, refreshes the etag either way, and throws our operation away if Google won.
      // If ours won, the operation stays queued and goes up next pass with the new etag.
      LogInfo(L"sync: {} cambió en Google mientras se enviaba, se relee", item.uid);
      HttpResponse fresh;
      if (http_.Send(kApiHost, L"GET", path, headers, {}, fresh) && fresh.status == 200) {
        nlohmann::json parsed;
        if (ReadJson(fresh, parsed)) {
          store_.Run([&] {
            Transaction tx(store_.db());
            if (!tx.Begin()) return;
            if (isTask) {
              ApplyTask(store_.db(), parsed, row.container);
            } else {
              ApplyEvent(store_.db(), parsed, row.container);
            }
            tx.Commit();
          });
        }
      }
      continue;
    }

    if (ShouldRetry(status)) {
      // Http already retried this with backoff and it is still saying no. Stopping the whole
      // queue is deliberate: sending operation seven for a row whose operation five just failed
      // is worse than waiting five minutes.
      LogError(L"sync: Google pide calma ({}), la cola espera", status);
      return false;
    }

    refuse(response);
  }
  return true;
}

}  // namespace agenda::sync
