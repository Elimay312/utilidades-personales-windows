#pragma once

// The synchronisation with Google Calendar and Google Tasks, on its own thread.
//
// It does NOT hang off the Store's queue, which is what store.h assumed in phase 4. That queue
// also carries the writes the popup makes, and a request that takes twenty seconds sitting in
// front of them would leave a creation unwritten for twenty seconds: the card the user watched
// appear is already on screen, so it would come back wrong after closing and reopening. What
// goes through the Store is each SQLite write, with Run, so the cache keeps one connection and
// one writer.
//
// The order inside a pass is not arbitrary. Pushing comes FIRST: with the pulls in front, a
// download would overwrite a local edit that had not gone out yet, and the queue that was
// holding it would be thrown away as stale.

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

#include "sync/http.h"
#include "sync/oauth.h"

namespace agenda {

class Store;

namespace sync {

// Google refused to renew the permission, so there is no account any more. Sent once, without
// payload, to whatever window has the tray icon: it is the only thing in this phase the user
// has to be told about out loud, because it is the only one they have to act on.
inline constexpr UINT kSyncLostAccountMessage = WM_APP + 3;

class GoogleSync {
 public:
  GoogleSync(Store& store, OAuthConfig config);
  ~GoogleSync();

  GoogleSync(const GoogleSync&) = delete;
  GoogleSync& operator=(const GoogleSync&) = delete;

  // Does nothing without credentials: Agenda stays a local calendar and says so once, rather
  // than failing every five minutes.
  void Start();
  // Cuts whatever is in flight and waits for the thread. Cancelling closes the request handle,
  // so this takes a moment and not the thirty seconds of a timeout.
  void Stop();

  // Where kSyncLostAccountMessage goes. Not set means nobody is listening, which is the case
  // in a test.
  void SetNotifyWindow(HWND hwnd) { hwnd_ = hwnd; }

  // Asks for a pass. Does nothing if the last one was less than `notBefore` ago; with zero, now.
  // Never blocks -- this is what the popup calls as it opens and right after Enter.
  void Nudge(std::chrono::seconds notBefore = std::chrono::seconds::zero());
  // Only empties the queue upwards. What is wanted right after creating something: the whole
  // pass would be a download nobody asked for.
  void Push();

  // Starts the consent round trip on the synchronisation thread.
  //
  // WHOEVER CALLS THIS HAS ALREADY ASKED THE USER. Opening the browser is on the list of things
  // CLAUDE.md says to ask about first, and the asking belongs to whatever has a window.
  void Connect();
  void Disconnect();

  bool Configured() const { return auth_.Configured(); }
  bool Connected() const { return auth_.Connected(); }

  // What lights the dot in the popup: there is an account, and the last thing tried did not
  // reach anybody. With no account nothing is shown, because nothing is failing.
  bool Offline() const { return offline_.load(); }

 private:
  void Loop();
  void RunPass(bool pushOnly);
  bool PushPending(const std::wstring& auth);
  bool PullCalendars(const std::wstring& auth);
  bool PullEvents(const std::wstring& auth, const std::string& calendarId);
  bool PullTasks(const std::wstring& auth, const std::string& listId);
  void AdoptLocalRows();

  Store& store_;
  Http http_;
  GoogleAuth auth_;
  HWND hwnd_ = nullptr;

  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool running_ = false;
  bool wanted_ = false;
  bool pushOnly_ = false;
  bool connectWanted_ = false;
  std::chrono::steady_clock::time_point lastPass_{};
  std::atomic<bool> offline_{false};
};

}  // namespace sync
}  // namespace agenda
