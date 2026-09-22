#pragma once

// WinHTTP: one session, a connection per host, and one request at a time.
//
// Synchronous, on the synchronisation thread. There is exactly one of those and it has nothing
// else to do while it waits, so asynchronous WinHTTP would trade a function that reads top to
// bottom for a state machine whose completions land on WinHTTP's own thread pool -- the same
// waiting, with more ways to get a handle's lifetime wrong.
//
// Two things in here are not decoration:
//
//   * Cancelling closes the handle from the other thread. A flag would be looked at after the
//     wait, and the wait is what has to end: quitting Agenda in the middle of a sync would
//     otherwise take up to thirty seconds with the window already gone.
//   * Retrying lives here and not in the caller. A 429 or a 5xx is the transport's problem,
//     and google.cpp having to remember to retry every one of its eight calls is how one of
//     them ends up not doing it.

#include <windows.h>

#include <winhttp.h>

#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace agenda::sync {

struct HttpResponse {
  int status = 0;
  std::string body;  // UTF-8 as it arrived, uncompressed by WinHTTP
  // 0 means the header was absent, which is not the same as being told to wait no time.
  int retryAfterSeconds = 0;
};

class Http {
 public:
  Http() = default;
  ~Http();

  Http(const Http&) = delete;
  Http& operator=(const Http&) = delete;

  bool Open();
  void Close();

  // Closes whatever is in flight and wakes whoever is sleeping between retries. Called from the
  // interface thread when Agenda is shutting down.
  void Cancel();
  bool Cancelled() const;

  // One request, retried on a 429, a 5xx and a network failure.
  //
  // Returns true when a reply arrived, whatever it says: a 404 and a 410 are answers, and the
  // caller is the one who knows what they mean. False means nobody was reached, and that is
  // what the popup's offline dot is made of.
  //
  // `headers` is the whole block, CRLF separated, Content-Type included: what goes up as JSON
  // and what goes up as a form differ, and this is not the place that knows which.
  bool Send(const wchar_t* host, const wchar_t* verb, const std::wstring& path,
            const std::wstring& headers, std::string_view body, HttpResponse& out);

  // Whether the last attempt reached anybody at all. This is the whole of "offline" -- not
  // INetworkListManager, which would answer a question nobody asked: what the user notices is
  // that the last thing tried did not go through.
  bool reachable() const;
  std::wstring error() const;

 private:
  bool Once(const wchar_t* host, const wchar_t* verb, const std::wstring& path,
            const std::wstring& headers, std::string_view body, HttpResponse& out,
            bool& reached);
  HINTERNET Connect(const wchar_t* host);
  bool Pause(int milliseconds);  // false when cancelled while waiting
  bool Fail(std::wstring_view what);

  std::uint64_t Register(HINTERNET request);
  bool Unregister(std::uint64_t ticket);

  HINTERNET session_ = nullptr;
  // One entry per host, and there are two: the API and the token endpoint. WinHttpConnect does
  // not connect -- the pool lives on the session -- so these are cheap to hold and cheaper than
  // opening one per request.
  std::map<std::wstring, HINTERNET> connections_;

  mutable std::mutex mutex_;
  std::condition_variable wake_;
  HINTERNET inflight_ = nullptr;
  std::uint64_t ticket_ = 0;
  std::uint64_t nextTicket_ = 1;
  bool cancelled_ = false;
  bool reachable_ = true;
  std::wstring error_;
};

}  // namespace agenda::sync
