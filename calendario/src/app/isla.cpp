#include "app/isla.h"

#include <nlohmann/json.hpp>

#include <format>

#include "app/toast.h"
#include "core/i18n.h"
#include "core/log.h"
#include "data/db.h"

namespace agenda {
namespace {

constexpr wchar_t kPipe[] = LR"(\\.\pipe\IslaDinamica.avisos)";
constexpr DWORD kWriteTimeoutMs = 2000;
constexpr size_t kMaxAnswer = 512;

// Waits for an overlapped operation, or for `cancel`. False when it did not complete.
bool Finish(HANDLE pipe, OVERLAPPED& op, HANDLE cancel, DWORD timeout, DWORD& bytes) {
  const HANDLE waits[2] = {op.hEvent, cancel};
  const DWORD woke = WaitForMultipleObjects(cancel != nullptr ? 2 : 1, waits, FALSE, timeout);
  if (woke != WAIT_OBJECT_0) {
    CancelIoEx(pipe, &op);
    GetOverlappedResult(pipe, &op, &bytes, TRUE);
    return false;
  }
  return GetOverlappedResult(pipe, &op, &bytes, FALSE) != FALSE;
}

}  // namespace

bool Isla::Offer(const Reminder& reminder, long long now) {
  Reap();
  // Identification only: whoever is at the other end of the pipe gets to know who we are, not
  // to act as us. The island does not need more, and a squatter on the name gets nothing.
  const HANDLE pipe = CreateFileW(kPipe, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT |
                                      SECURITY_IDENTIFICATION,
                                  nullptr);
  // Not found is the normal case -- there is no island -- and busy means it is full. Either
  // way the toast says it instead.
  if (pipe == INVALID_HANDLE_VALUE) return false;

  const nlohmann::json notice = {
      {"app", "Agenda"},
      {"titulo", ToUtf8(reminder.title)},
      // The absolute time and not "en 10 min": the bubble can wait a long while to be opened.
      {"linea", ToUtf8(ReminderWhen(reminder, now))},
      {"color", std::format("#{:06X}", reminder.color & 0xFFFFFFu)},
      // The day of the event inside the bubble, like a calendar icon.
      {"insignia", std::to_string(static_cast<unsigned>(reminder.day.day()))},
      {"botones",
       {{{"id", kDone}, {"texto", ToUtf8(T(L"Terminado", L"Done"))}},
        {{"id", kSnooze5}, {"texto", "5 min"}},
        {{"id", kSnooze10}, {"texto", "10 min"}},
        {{"id", kOpen}, {"texto", ToUtf8(T(L"Abrir", L"Open"))}}}}};
  const std::string line = notice.dump() + "\n";

  OVERLAPPED op{};
  op.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  DWORD written = 0;
  bool sent = WriteFile(pipe, line.data(), static_cast<DWORD>(line.size()), nullptr, &op) ||
              GetLastError() == ERROR_IO_PENDING;
  sent = sent && Finish(pipe, op, nullptr, kWriteTimeoutMs, written) && written == line.size();
  CloseHandle(op.hEvent);
  if (!sent) {
    CloseHandle(pipe);
    return false;
  }

  auto pending = std::make_unique<Pending>();
  pending->reminder = reminder;
  pending->pipe = pipe;
  pending->cancel = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  Pending& ref = *pending;
  pending->reader = std::thread([this, &ref] { Read(ref); });
  pending_.push_back(std::move(pending));
  return true;
}

void Isla::Read(Pending& pending) {
  std::string answer;
  bool cancelled = false;
  OVERLAPPED op{};
  op.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  while (answer.size() < kMaxAnswer && answer.find('\n') == std::string::npos) {
    char buffer[128];
    DWORD got = 0;
    ResetEvent(op.hEvent);
    const bool started = ReadFile(pending.pipe, buffer, sizeof(buffer), nullptr, &op) ||
                         GetLastError() == ERROR_IO_PENDING;
    if (!started || !Finish(pending.pipe, op, pending.cancel, INFINITE, got) || got == 0) {
      cancelled = WaitForSingleObject(pending.cancel, 0) == WAIT_OBJECT_0;
      break;
    }
    answer.append(buffer, got);
  }
  CloseHandle(op.hEvent);

  // Taken back on purpose says nothing; everything else -- an answer, or the island gone
  // without one -- goes to the window.
  if (!cancelled) {
    std::string button;
    const nlohmann::json parsed =
        nlohmann::json::parse(answer.substr(0, answer.find('\n')), nullptr, false);
    if (parsed.is_object()) {
      if (const auto found = parsed.find("boton"); found != parsed.end() && found->is_string()) {
        button = found->get<std::string>();
      }
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      answers_.push_back(Answer{pending.reminder, std::move(button)});
    }
    if (hwnd_ != nullptr) PostMessageW(hwnd_, kIslaAnswerMessage, 0, 0);
  }
  pending.done = true;
}

std::vector<Isla::Answer> Isla::TakeAnswers() {
  Reap();
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<Answer> out;
  out.swap(answers_);
  return out;
}

void Isla::Withdraw(long long now) {
  for (const std::unique_ptr<Pending>& pending : pending_) {
    const Reminder& r = pending->reminder;
    const int end = r.endMin && (!r.startMin || *r.endMin > *r.startMin) ? *r.endMin
                    : r.startMin                                         ? *r.startMin + 60
                                                                         : 24 * 60;
    if (WallMinute(r.day, end) <= now) SetEvent(pending->cancel);
  }
  Reap();
}

void Isla::Reap() {
  std::erase_if(pending_, [](const std::unique_ptr<Pending>& pending) {
    if (!pending->done) return false;
    pending->reader.join();
    CloseHandle(pending->pipe);
    CloseHandle(pending->cancel);
    return true;
  });
}

void Isla::Stop() {
  for (const std::unique_ptr<Pending>& pending : pending_) SetEvent(pending->cancel);
  for (const std::unique_ptr<Pending>& pending : pending_) {
    if (pending->reader.joinable()) pending->reader.join();
    CloseHandle(pending->pipe);
    CloseHandle(pending->cancel);
  }
  pending_.clear();
}

}  // namespace agenda
