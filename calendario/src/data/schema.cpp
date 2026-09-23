#include "data/schema.h"

#include <string>

#include "core/log.h"

namespace agenda {
namespace {

// --- v1 -----------------------------------------------------------------------------------
//
// Time is stored as a LOCAL WALL CLOCK -- a day and a minute of that day -- and not as a UTC
// instant. That is what the code already carries around (nlp::DateTime is a Date plus a
// minuteOfDay), it is the only question the interface ever asks ("what is there on day D?"),
// and it is what Google Calendar itself stores: a dateTime with a timeZone, not an instant.
// Keeping a UTC column as well would mean two conversions per query and two ideas of what
// day today is inside one cache, which is one of them being wrong.
//
// `updated_at` is the exception and is a real UTC instant, because it is conflict metadata
// and not an hour of anybody's day.
constexpr const char* kV1 = R"SQL(
-- One table for both kinds of list: a Google calendar and a Google Tasks list are used the
-- same way from here -- a name, a colour, a remote id -- and splitting them would be two
-- tables with the same five columns.
CREATE TABLE calendars (
  id          TEXT PRIMARY KEY,
  kind        TEXT NOT NULL,
  title       TEXT NOT NULL,
  color       INTEGER NOT NULL,
  time_zone   TEXT NOT NULL DEFAULT '',
  is_primary  INTEGER NOT NULL DEFAULT 0,
  visible     INTEGER NOT NULL DEFAULT 1,
  sort        INTEGER NOT NULL DEFAULT 0
);

-- No cascade on DELETE and cascade on UPDATE, which is the opposite of what one writes by
-- reflex and is exactly what is needed: deleting a calendar has to fail while events hang
-- from it, and an id that changes has to drag them along.
CREATE TABLE events (
  id          INTEGER PRIMARY KEY,
  uid         TEXT NOT NULL UNIQUE,
  calendar_id TEXT NOT NULL REFERENCES calendars(id) ON UPDATE CASCADE,
  title       TEXT NOT NULL,
  notes       TEXT NOT NULL DEFAULT '',
  start_day   TEXT NOT NULL,
  start_min   INTEGER,
  end_day     TEXT NOT NULL,
  end_min     INTEGER,
  recurrence  TEXT NOT NULL DEFAULT '',
  remote_id   TEXT,
  etag        TEXT,
  updated_at  INTEGER NOT NULL,
  deleted_at  INTEGER
);
CREATE INDEX events_by_day ON events(start_day, start_min);

CREATE TABLE tasks (
  id          INTEGER PRIMARY KEY,
  uid         TEXT NOT NULL UNIQUE,
  list_id     TEXT NOT NULL REFERENCES calendars(id) ON UPDATE CASCADE,
  title       TEXT NOT NULL,
  notes       TEXT NOT NULL DEFAULT '',
  due_day     TEXT,
  due_min     INTEGER,
  done_at     INTEGER,
  position    TEXT NOT NULL DEFAULT '',
  remote_id   TEXT,
  etag        TEXT,
  updated_at  INTEGER NOT NULL,
  deleted_at  INTEGER
);
CREATE INDEX tasks_by_day ON tasks(due_day, due_min);

-- One row per calendar or list: each one carries its own cursor.
CREATE TABLE sync_state (
  id          TEXT PRIMARY KEY REFERENCES calendars(id) ON UPDATE CASCADE,
  sync_token  TEXT NOT NULL DEFAULT '',
  updated_min TEXT NOT NULL DEFAULT '',
  synced_at   INTEGER NOT NULL DEFAULT 0
);

-- The queue. The autoincrement id IS the replay order: a create and the edit that follows it
-- have to leave in that order or the server gets an edit for something that does not exist.
-- It points at 'uid' and not at 'remote_id' because when it is queued there is no remote yet.
--
-- There is no payload column on purpose. The operation says WHAT changed, and the body that
-- goes out is built by reading the row when it is sent. With a JSON copy inside the queue,
-- two edits in a row send the first version and then the second instead of sending the good
-- one once.
CREATE TABLE pending_ops (
  id        INTEGER PRIMARY KEY,
  entity    TEXT NOT NULL,
  uid       TEXT NOT NULL,
  op        TEXT NOT NULL,
  tries     INTEGER NOT NULL DEFAULT 0,
  last_err  TEXT NOT NULL DEFAULT '',
  queued_at INTEGER NOT NULL
);
CREATE INDEX pending_by_uid ON pending_ops(uid);

-- Somewhere to land before a Google account exists. Creating and undoing keep working with
-- no network, which is this phase's acceptance test.
INSERT INTO calendars (id, kind, title, color, is_primary) VALUES
  ('local',       'calendar', 'Local',  0x4A8BF5, 1),
  ('local-tasks', 'tasklist', 'Tareas', 0xF5A623, 1);
INSERT INTO sync_state (id) VALUES ('local'), ('local-tasks');
)SQL";

// --- v2 -----------------------------------------------------------------------------------
//
// Phase 6. Only columns added, nothing renamed or dropped, so a v1 cache migrates by itself.
//
// `hidden` is the user's switch in the sidebar and is NOT `visible`: `visible` means "Google
// still lists this calendar", and every pull writes it back to 1. A switch stored there would
// last until the next sync.
//
// `moved_from` is the calendar an event lived in on Google before it was moved to another one.
// Google does not move an event with a PATCH -- it wants POST .../move on the calendar it is in
// now -- and by the time the queue sends it, the row already says where it is going.
constexpr const char* kV2 = R"SQL(
ALTER TABLE calendars ADD COLUMN hidden INTEGER NOT NULL DEFAULT 0;
ALTER TABLE events ADD COLUMN location TEXT NOT NULL DEFAULT '';
ALTER TABLE events ADD COLUMN moved_from TEXT;
)SQL";

// --- v3 -----------------------------------------------------------------------------------
//
// Phase 7, with the user's permission, and again only columns added: the reminders, which is
// what the Windows notifications are raised from. They are "notification" reminders only --
// Google sends the e-mail ones itself -- as minutes before the start, comma-separated.
//
// `events.reminders` NULL means "whatever the calendar says", which is Google's useDefault and
// what every event created here is born with; an empty string means none at all. A calendar's
// own list is its defaultReminders. The local calendar gets ten minutes, the same Google gives
// a new account, so an agenda with no account still says something before the dentist.
//
// The sync tokens are emptied so the next pass downloads every event once and fills the column.
// An incremental pass only sends what changed, and a reminder nobody touched would never come.
constexpr const char* kV3 = R"SQL(
ALTER TABLE events ADD COLUMN reminders TEXT;
ALTER TABLE calendars ADD COLUMN reminders TEXT NOT NULL DEFAULT '';
UPDATE calendars SET reminders = '10' WHERE id = 'local';
UPDATE sync_state SET sync_token = '';
)SQL";

// --- v4 -----------------------------------------------------------------------------------
//
// Phase 8, with the user's permission, only columns added: the occurrences of a series that
// Google keeps apart. An occurrence moved or cancelled on the web arrives as an item of its own
// with the id of its series and the start it had; without these columns it was stored as one
// more event and the series still drew the original, so it showed up twice.
//
// `series_id` is the series' id AT GOOGLE, not our uid: that is what arrives, and the series may
// come on a later page than its exception. A series created here goes up with EventIdFor(uid),
// so its key is known before it has a remote_id. `original_day` is the day the occurrence had.
// A cancelled occurrence is kept as a tombstone with both, and the store skips that day too.
//
// The sync tokens are emptied, as in v3, so the exceptions already out there come down.
constexpr const char* kV4 = R"SQL(
ALTER TABLE events ADD COLUMN series_id TEXT;
ALTER TABLE events ADD COLUMN original_day TEXT;
CREATE INDEX events_by_series ON events(series_id, original_day);
UPDATE sync_state SET sync_token = '';
)SQL";

// --- v5 -----------------------------------------------------------------------------------
//
// Phase 12, with the user's permission, and again nothing rebuilt: more than one Google account.
//
// `accounts` is one row per account, with the file its refresh token is kept in (next to the
// cache, encrypted with DPAPI). The account that was already connected is number 1 and keeps
// token.bin, so connecting again is not needed; the ones added later get token-<id>.bin. Its
// email is the id of its primary calendar, which the next pass writes down.
//
// `calendars.account_id` says whose a calendar or a task list is; NULL is the local
// placeholders. `calendars.remote_id` is Google's id when it cannot be ours: the same calendar
// shared into two accounts would be one primary key twice, so the second one gets a key of its
// own and remembers the real id here. NULL means the key IS Google's id, which is every row
// that already exists.
constexpr const char* kV5 = R"SQL(
CREATE TABLE accounts (
  id          INTEGER PRIMARY KEY,
  email       TEXT NOT NULL DEFAULT '',
  token_file  TEXT NOT NULL UNIQUE,
  added_at    INTEGER NOT NULL
);
ALTER TABLE calendars ADD COLUMN account_id INTEGER REFERENCES accounts(id);
ALTER TABLE calendars ADD COLUMN remote_id TEXT;
INSERT INTO accounts (id, token_file, added_at)
  SELECT 1, 'token.bin', CAST(strftime('%s', 'now') AS INTEGER)
  WHERE EXISTS (SELECT 1 FROM calendars WHERE id NOT IN ('local', 'local-tasks'));
UPDATE calendars SET account_id = 1 WHERE id NOT IN ('local', 'local-tasks');
)SQL";

struct Migration {
  int version;
  const char* sql;
};

constexpr Migration kMigrations[] = {
    {1, kV1},
    {2, kV2},
    {3, kV3},
    {4, kV4},
    {5, kV5},
};

}  // namespace

bool Migrate(Db& db, int upTo) {
  const std::optional<std::int64_t> current = db.UserVersion();
  if (!current) return false;

  const std::int64_t from = *current;
  if (from > kSchemaVersion) {
    LogError(L"db: la cache la escribio una version mas nueva de Agenda (v{}), no se toca",
             from);
    return db.Fail(L"la cache es de una version mas nueva de Agenda");
  }
  if (from >= upTo) return true;

  // Everything inside one transaction, user_version included. If a CREATE TABLE fails, the
  // Transaction destructor undoes whatever there was and the number stays where it was, so
  // the next start tries again from the same place.
  Transaction tx(db);
  if (!tx.Begin()) return false;

  for (const Migration& step : kMigrations) {
    if (step.version <= from || step.version > upTo) continue;
    if (!db.Exec(step.sql)) return false;
  }

  if (!db.SetUserVersion(upTo)) return false;
  if (!tx.Commit()) return false;

  LogInfo(L"db: esquema en v{} (venia de v{})", upTo, from);
  return true;
}

}  // namespace agenda
