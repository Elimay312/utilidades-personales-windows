#pragma once

// The schema and its migrations.
//
// The version lives in PRAGMA user_version, an integer inside the file header that is --
// and this is the point -- transactional: if a migration dies halfway, the number does not
// go up. With a hand-written versions table somebody has to remember to put it inside the
// same transaction; with user_version there is nothing to remember.
//
// Migrations only go forward. A database written by a NEWER build is left alone and reports
// an error: that happens when an old build is run after a new one, and converting it
// backwards blind would mean losing whatever the new one wrote.

#include "data/db.h"

namespace agenda {

inline constexpr int kSchemaVersion = 3;

// Ids of the rows v1 seeds, so an event has somewhere to hang before there is a Google
// account.
inline constexpr const char* kLocalCalendarId = "local";
inline constexpr const char* kLocalTaskListId = "local-tasks";

// Brings the database up to kSchemaVersion. Idempotent: calling it on a database that is
// already current does nothing and is not an error.
bool Migrate(Db& db);

}  // namespace agenda
