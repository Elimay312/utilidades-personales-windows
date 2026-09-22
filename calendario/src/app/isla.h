#pragma once

// The island next door. isla/ is another project of this repository, a dynamic island along the
// top edge of the screen, and when it is running a reminder goes there instead of to a toast:
// it peeks in with the calendar's colour, folds into a small bubble beside the island, and
// hovering the bubble opens it with its buttons. Each project works on its own -- without the
// island this is a toast, exactly as before -- and better together.
//
// The channel is the island's mailbox (isla/SEGURIDAD.md s.3.7): a named pipe it listens on.
// One connection per reminder: Agenda writes one line of JSON -- a title, a line, a colour and
// up to four buttons -- and waits on the same connection for the id of the button pressed. The
// island knows nothing about calendars; what "hecho", "pos5" or "abrir" mean is decided here.

#include <windows.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "data/model.h"

namespace agenda {

// An answer from the island arrived, or a notice came back without one. No payload: the
// window drains them with Isla::TakeAnswers.
inline constexpr UINT kIslaAnswerMessage = WM_APP + 7;

class Isla {
 public:
  // Button ids, as they come back.
  static constexpr const char* kDone = "hecho";
  static constexpr const char* kSnooze5 = "pos5";
  static constexpr const char* kSnooze10 = "pos10";
  static constexpr const char* kOpen = "abrir";

  Isla() = default;
  ~Isla() { Stop(); }
  Isla(const Isla&) = delete;
  Isla& operator=(const Isla&) = delete;

  void SetNotifyWindow(HWND hwnd) { hwnd_ = hwnd; }

  // Hands `reminder` to the island. False when there is no island listening, or it is busy:
  // then the caller raises the toast itself.
  bool Offer(const Reminder& reminder, long long now);

  // `button` empty means the island went away -- closed, or full -- without an answer, and the
  // reminder still has to be said some other way.
  struct Answer {
    Reminder reminder;
    std::string button;
  };
  std::vector<Answer> TakeAnswers();

  // Takes back the notices of events that are already over: a dot for yesterday's meeting is
  // not a reminder, it is litter.
  void Withdraw(long long now);

  // Cancels everything in flight and waits for it. The island just sees the connections close.
  void Stop();

 private:
  struct Pending {
    Reminder reminder;
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE cancel = nullptr;
    std::thread reader;
    std::atomic<bool> done{false};
  };
  void Read(Pending& pending);
  void Reap();

  HWND hwnd_ = nullptr;
  std::mutex mutex_;  // answers_; pending_ is only touched on the interface thread
  std::vector<std::unique_ptr<Pending>> pending_;
  std::vector<Answer> answers_;
};

}  // namespace agenda
