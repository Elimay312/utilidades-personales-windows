#include "sync/http.h"

#include <chrono>
#include <format>

#include "core/log.h"
#include "sync/map.h"

namespace agenda::sync {
namespace {

constexpr wchar_t kAgent[] = L"Agenda/0.1 (Windows)";

// Four tries, so a blip costs half a second and a wall costs eight. More than this and a pass
// stops being background work; fewer and a single 503 loses the pass. The next pass is five
// minutes away either way, which is what makes giving up cheap.
constexpr int kAttempts = 4;

int ReadRetryAfter(HINTERNET request) {
  wchar_t buffer[32]{};
  DWORD size = sizeof(buffer);
  if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RETRY_AFTER, WINHTTP_HEADER_NAME_BY_INDEX,
                          buffer, &size, WINHTTP_NO_HEADER_INDEX) == FALSE) {
    return 0;
  }
  // Only the form that is a number of seconds. The HTTP-date form is legal and Google does not
  // use it; reading it wrong would be worse than not reading it, because the arithmetic in
  // BackoffMs is a perfectly good answer.
  int seconds = 0;
  for (const wchar_t digit : std::wstring_view(buffer)) {
    if (digit < L'0' || digit > L'9') return 0;
    seconds = seconds * 10 + (digit - L'0');
    if (seconds > 86400) return 86400;
  }
  return seconds;
}

// Closes itself unless somebody takes it away, which is what Cancel does.
class RequestHandle {
 public:
  explicit RequestHandle(HINTERNET handle) : handle_(handle) {}
  ~RequestHandle() {
    if (handle_ != nullptr) WinHttpCloseHandle(handle_);
  }

  RequestHandle(const RequestHandle&) = delete;
  RequestHandle& operator=(const RequestHandle&) = delete;

  HINTERNET Get() const { return handle_; }
  void Disown() { handle_ = nullptr; }

 private:
  HINTERNET handle_ = nullptr;
};

}  // namespace

Http::~Http() { Close(); }

bool Http::Open() {
  Close();

  session_ = WinHttpOpen(kAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                         WINHTTP_NO_PROXY_BYPASS, 0);
  if (session_ == nullptr) return Fail(L"no se pudo preparar la conexión");

  // Resolving and connecting go short: eight seconds without an answer from DNS is not a slow
  // day, it is no network. Receiving goes long, because a first full synchronisation of an old
  // account is a big page and timing it out would leave the pass half done.
  WinHttpSetTimeouts(session_, 8000, 8000, 20000, 30000);

  // Not negotiating HTTP/2 costs nothing here -- one request at a time -- but it saves a
  // handshake between the API and the token endpoint. Absent before Windows 10 1607, so the
  // failure is ignored.
  DWORD http2 = WINHTTP_PROTOCOL_FLAG_HTTP2;
  WinHttpSetOption(session_, WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL, &http2, sizeof(http2));

  // A year of somebody's calendar arrives compressed, without a line of gzip written here.
  DWORD decompression = WINHTTP_DECOMPRESSION_FLAG_ALL;
  WinHttpSetOption(session_, WINHTTP_OPTION_DECOMPRESSION, &decompression,
                   sizeof(decompression));

  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled_ = false;
    reachable_ = true;
  }
  return true;
}

void Http::Close() {
  Cancel();
  for (const auto& [host, handle] : connections_) WinHttpCloseHandle(handle);
  connections_.clear();
  if (session_ != nullptr) {
    WinHttpCloseHandle(session_);
    session_ = nullptr;
  }
}

void Http::Cancel() {
  HINTERNET victim = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cancelled_ = true;
    // Taken out of the record inside the lock. From here on the thread that owns it will not
    // find it when it unhooks, and so will not close it a second time.
    victim = inflight_;
    inflight_ = nullptr;
    ticket_ = 0;
  }
  if (victim != nullptr) WinHttpCloseHandle(victim);
  wake_.notify_all();
}

bool Http::Cancelled() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return cancelled_;
}

bool Http::reachable() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return reachable_;
}

std::wstring Http::error() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_;
}

bool Http::Fail(std::wstring_view what) {
  const DWORD code = GetLastError();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = std::format(L"{} (error {})", what, code);
  }
  LogError(L"http: {} (error {})", what, code);
  return false;
}

std::uint64_t Http::Register(HINTERNET request) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (cancelled_) return 0;
  ticket_ = nextTicket_++;
  inflight_ = request;
  return ticket_;
}

bool Http::Unregister(std::uint64_t ticket) {
  std::lock_guard<std::mutex> lock(mutex_);
  // A ticket that is not the current one means Cancel already took the handle and closed it.
  // The number is what keeps this from closing somebody else's handle if WinHTTP hands the same
  // value back out.
  if (ticket_ != ticket) return false;
  inflight_ = nullptr;
  ticket_ = 0;
  return true;
}

bool Http::Pause(int milliseconds) {
  std::unique_lock<std::mutex> lock(mutex_);
  wake_.wait_for(lock, std::chrono::milliseconds(milliseconds), [this] { return cancelled_; });
  return !cancelled_;
}

HINTERNET Http::Connect(const wchar_t* host) {
  const std::wstring key(host);
  if (const auto found = connections_.find(key); found != connections_.end()) {
    return found->second;
  }
  HINTERNET handle = WinHttpConnect(session_, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (handle == nullptr) {
    Fail(std::format(L"no se pudo abrir la conexión con {}", key));
    return nullptr;
  }
  connections_.emplace(key, handle);
  return handle;
}

bool Http::Send(const wchar_t* host, const wchar_t* verb, const std::wstring& path,
                const std::wstring& headers, std::string_view body, HttpResponse& out) {
  for (int attempt = 1; attempt <= kAttempts; ++attempt) {
    if (Cancelled()) return false;

    out = HttpResponse{};
    bool reached = false;
    const bool answered = Once(host, verb, path, headers, body, out, reached);

    {
      std::lock_guard<std::mutex> lock(mutex_);
      reachable_ = reached;
    }

    if (answered && !ShouldRetry(out.status)) return true;
    if (attempt == kAttempts) {
      if (answered) return true;  // the last word was a real reply; the caller decides
      return false;
    }

    const int wait = BackoffMs(attempt, out.retryAfterSeconds);
    LogInfo(L"http: {} {} -> {}, reintento {} en {} ms", std::wstring_view(verb), path,
            answered ? out.status : 0, attempt, wait);
    if (!Pause(wait)) return false;
  }
  return false;
}

bool Http::Once(const wchar_t* host, const wchar_t* verb, const std::wstring& path,
                const std::wstring& headers, std::string_view body, HttpResponse& out,
                bool& reached) {
  reached = false;
  if (session_ == nullptr) return Fail(L"la sesión no está abierta");

  HINTERNET connection = Connect(host);
  if (connection == nullptr) return false;

  RequestHandle request(WinHttpOpenRequest(connection, verb, path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE));
  if (request.Get() == nullptr) return Fail(L"no se pudo preparar la petición");

  const std::uint64_t ticket = Register(request.Get());
  if (ticket == 0) return false;  // cancelled before it even started

  // If Cancel takes the handle while we are inside, this object must not close it again.
  struct Unhook {
    Http& http;
    std::uint64_t ticket;
    RequestHandle& handle;
    ~Unhook() {
      if (!http.Unregister(ticket)) handle.Disown();
    }
  } unhook{*this, ticket, request};

  if (WinHttpSendRequest(request.Get(), headers.c_str(), static_cast<DWORD>(headers.size()),
                         const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
                         static_cast<DWORD>(body.size()), 0) == FALSE) {
    return Fail(L"no se pudo enviar la petición");
  }

  if (WinHttpReceiveResponse(request.Get(), nullptr) == FALSE) {
    Fail(L"no llegó respuesta");
    return false;
  }

  // From here on somebody answered, whatever they said. That is the difference the offline dot
  // is made of, and it is set before the status is even read.
  reached = true;

  DWORD status = 0;
  DWORD statusSize = sizeof(status);
  if (WinHttpQueryHeaders(request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                          WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                          WINHTTP_NO_HEADER_INDEX) == FALSE) {
    return Fail(L"la respuesta no trae código de estado");
  }
  out.status = static_cast<int>(status);
  out.retryAfterSeconds = ReadRetryAfter(request.Get());

  for (;;) {
    // Between chunks, so a big page and a shutdown do not have to wait for one another.
    if (Cancelled()) return false;

    DWORD available = 0;
    if (WinHttpQueryDataAvailable(request.Get(), &available) == FALSE) {
      return Fail(L"la respuesta se cortó");
    }
    if (available == 0) break;

    const size_t offset = out.body.size();
    out.body.resize(offset + available);

    DWORD read = 0;
    if (WinHttpReadData(request.Get(), out.body.data() + offset, available, &read) == FALSE) {
      return Fail(L"la respuesta se cortó");
    }
    // It can read fewer bytes than it announced, and the extra room would keep whatever was in
    // it. Trimming is what stops the JSON from ending in padding.
    out.body.resize(offset + read);
    if (read == 0) break;
  }

  return true;
}

}  // namespace agenda::sync
