#include "sync/oauth.h"

#include <bcrypt.h>
#include <shellapi.h>
#include <wincrypt.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <format>
#include <fstream>
#include <vector>

#include "core/log.h"
#include "core/paths.h"
// For ToWide. The UTF-8 crossing lives in db.h and nowhere else (CLAUDE.md), even when what is
// being converted has nothing to do with SQLite.
#include "data/db.h"
#include "sync/map.h"

namespace agenda::sync {
namespace {

constexpr wchar_t kTokenHost[] = L"oauth2.googleapis.com";
constexpr wchar_t kTokenPath[] = L"/token";
constexpr wchar_t kFormHeaders[] =
    L"Content-Type: application/x-www-form-urlencoded\r\nAccept: application/json\r\n";

// The three the phase asks for and not one more. `calendar.readonly` is here only so the list
// of calendars can be read -- names and colours; asking for plain `calendar` would be asking
// for permission to delete somebody's calendars to paint a dot the right colour.
constexpr char kScopes[] =
    "https://www.googleapis.com/auth/calendar.events "
    "https://www.googleapis.com/auth/calendar.readonly "
    "https://www.googleapis.com/auth/tasks";

// Long enough to pick an account, walk past the unverified-app warning and read the consent
// screen. Shorter than this and the wait expires while somebody is still reading.
constexpr int kConsentSeconds = 180;

// A token is swapped when it has less than this left. The clock on this machine and the clock
// at Google are not the same clock, and an access token that expires mid-pass costs a whole
// pass.
constexpr std::int64_t kRenewBeforeSeconds = 300;

std::int64_t NowSeconds() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

// Base64 in the URL alphabet, without padding, which is what PKCE wants.
std::string Base64Url(const unsigned char* bytes, size_t length) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  out.reserve((length + 2) / 3 * 4);
  for (size_t i = 0; i < length; i += 3) {
    const unsigned value = (static_cast<unsigned>(bytes[i]) << 16) |
                           (i + 1 < length ? static_cast<unsigned>(bytes[i + 1]) << 8 : 0u) |
                           (i + 2 < length ? static_cast<unsigned>(bytes[i + 2]) : 0u);
    out.push_back(kAlphabet[(value >> 18) & 0x3F]);
    out.push_back(kAlphabet[(value >> 12) & 0x3F]);
    if (i + 1 < length) out.push_back(kAlphabet[(value >> 6) & 0x3F]);
    if (i + 2 < length) out.push_back(kAlphabet[value & 0x3F]);
  }
  return out;
}

std::string RandomToken(size_t bytes) {
  std::vector<unsigned char> buffer(bytes);
  if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, buffer.data(), static_cast<ULONG>(buffer.size()),
                                      BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
    return {};
  }
  return Base64Url(buffer.data(), buffer.size());
}

std::string Sha256Challenge(std::string_view verifier) {
  unsigned char digest[32]{};
  if (!BCRYPT_SUCCESS(BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                                 reinterpret_cast<PUCHAR>(const_cast<char*>(verifier.data())),
                                 static_cast<ULONG>(verifier.size()), digest, sizeof(digest)))) {
    return {};
  }
  return Base64Url(digest, sizeof(digest));
}

void Scrub(std::string& secret) {
  if (!secret.empty()) SecureZeroMemory(secret.data(), secret.size());
  secret.clear();
}

std::filesystem::path TokenFile() { return AppDataDir() / L"token.bin"; }

// The one GET Google sends back, and the page the browser is left showing.
//
// A socket and not the HTTP Server API: this listens once, for one request, on a port nobody
// chose. http.sys would want a URL reservation, which wants an administrator.
class Loopback {
 public:
  ~Loopback() {
    if (listener_ != INVALID_SOCKET) closesocket(listener_);
    if (started_) WSACleanup();
  }

  bool Open() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    started_ = true;

    listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener_ == INVALID_SOCKET) return false;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = 0;  // whichever one is free; Google allows any on the loopback
    InetPtonW(AF_INET, L"127.0.0.1", &address.sin_addr);
    if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
      return false;
    }
    if (listen(listener_, 1) == SOCKET_ERROR) return false;

    sockaddr_in bound{};
    int size = sizeof(bound);
    if (getsockname(listener_, reinterpret_cast<sockaddr*>(&bound), &size) == SOCKET_ERROR) {
      return false;
    }
    port_ = ntohs(bound.sin_port);
    return port_ != 0;
  }

  int port() const { return port_; }

  // Waits for the browser and hands back the query string it arrived with. Empty means nobody
  // came, which is what a closed tab looks like from here.
  std::string Wait(int seconds) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(listener_, &readable);
    timeval timeout{seconds, 0};
    if (select(0, &readable, nullptr, nullptr, &timeout) <= 0) return {};

    const SOCKET client = accept(listener_, nullptr, nullptr);
    if (client == INVALID_SOCKET) return {};

    std::string request;
    char buffer[2048];
    for (;;) {
      const int read = recv(client, buffer, sizeof(buffer), 0);
      if (read <= 0) break;
      request.append(buffer, static_cast<size_t>(read));
      // The request line is all this needs, and it is the first one. Waiting for the blank line
      // after the headers would hang on a browser that keeps the connection open.
      if (request.find("\r\n") != std::string::npos) break;
      if (request.size() > 8192) break;
    }

    static constexpr char kPage[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<!doctype html><html lang=\"es\"><meta charset=\"utf-8\">"
        "<title>Agenda</title>"
        "<body style=\"font-family:Segoe UI,sans-serif;background:#1E1F24;color:#F2F2F5;"
        "display:flex;align-items:center;justify-content:center;height:100vh;margin:0\">"
        "<p>Agenda ya tiene permiso. Puedes cerrar esta pestaña.</p></body></html>";
    send(client, kPage, static_cast<int>(sizeof(kPage) - 1), 0);
    shutdown(client, SD_BOTH);
    closesocket(client);

    // 'GET /?code=...&state=... HTTP/1.1'
    const size_t start = request.find(' ');
    if (start == std::string::npos) return {};
    const size_t end = request.find(' ', start + 1);
    if (end == std::string::npos) return {};
    const std::string target = request.substr(start + 1, end - start - 1);
    const size_t query = target.find('?');
    return query == std::string::npos ? std::string() : target.substr(query + 1);
  }

 private:
  bool started_ = false;
  SOCKET listener_ = INVALID_SOCKET;
  int port_ = 0;
};

// One field out of a query string, percent-decoded. Only what this file needs: `code`, `state`
// and `error`.
std::string Field(std::string_view query, std::string_view name) {
  size_t at = 0;
  while (at < query.size()) {
    const size_t amp = query.find('&', at);
    const std::string_view pair =
        query.substr(at, amp == std::string_view::npos ? std::string_view::npos : amp - at);
    const size_t equals = pair.find('=');
    if (equals != std::string_view::npos && pair.substr(0, equals) == name) {
      const std::string_view raw = pair.substr(equals + 1);
      std::string out;
      out.reserve(raw.size());
      for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '+') {
          out.push_back(' ');
        } else if (raw[i] == '%' && i + 2 < raw.size()) {
          const auto nibble = [](char letter) -> int {
            if (letter >= '0' && letter <= '9') return letter - '0';
            if (letter >= 'a' && letter <= 'f') return letter - 'a' + 10;
            if (letter >= 'A' && letter <= 'F') return letter - 'A' + 10;
            return -1;
          };
          const int high = nibble(raw[i + 1]);
          const int low = nibble(raw[i + 2]);
          if (high < 0 || low < 0) return {};
          out.push_back(static_cast<char>(high * 16 + low));
          i += 2;
        } else {
          out.push_back(raw[i]);
        }
      }
      return out;
    }
    if (amp == std::string_view::npos) break;
    at = amp + 1;
  }
  return {};
}

}  // namespace

GoogleAuth::GoogleAuth(OAuthConfig config) : config_(std::move(config)) {}

GoogleAuth::~GoogleAuth() {
  Scrub(access_);
  Scrub(refresh_);
  Scrub(config_.clientSecret);
}

bool GoogleAuth::Configured() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !config_.clientId.empty() && !config_.clientSecret.empty();
}

bool GoogleAuth::Connected() const {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!refresh_.empty()) return true;
    if (loaded_) return false;
  }
  return const_cast<GoogleAuth*>(this)->LoadRefresh();
}

std::wstring GoogleAuth::error() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return error_;
}

bool GoogleAuth::Fail(std::wstring_view what) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    error_ = std::wstring(what);
  }
  LogError(L"oauth: {}", what);
  return false;
}

// --- The consent round trip -----------------------------------------------------------------

bool GoogleAuth::Connect() {
  if (!Configured()) {
    return Fail(L"faltan clientId y clientSecret en config.local.json");
  }
  if (!http_.Open()) return Fail(L"no se pudo abrir la conexión con Google");

  // The verifier never leaves this process; only its hash goes out. That is the whole of PKCE,
  // and it is what makes the client_secret of a desktop app not worth stealing.
  const std::string verifier = RandomToken(48);
  const std::string state = RandomToken(16);
  const std::string challenge = Sha256Challenge(verifier);
  if (verifier.empty() || state.empty() || challenge.empty()) {
    return Fail(L"no se pudo preparar el intercambio (PKCE)");
  }

  Loopback loopback;
  if (!loopback.Open()) return Fail(L"no se pudo escuchar en 127.0.0.1");
  const std::string redirect = std::format("http://127.0.0.1:{}", loopback.port());

  const std::string url =
      "https://accounts.google.com/o/oauth2/v2/auth?" +
      FormEncode({{"client_id", config_.clientId},
                  {"redirect_uri", redirect},
                  {"response_type", "code"},
                  {"scope", kScopes},
                  {"code_challenge", challenge},
                  {"code_challenge_method", "S256"},
                  {"state", state},
                  // Without these two there is no refresh token on a second consent, and Agenda
                  // would work for an hour and then quietly stop.
                  {"access_type", "offline"},
                  {"prompt", "consent"}});

  LogInfo(L"oauth: abriendo el navegador, esperando en 127.0.0.1:{}", loopback.port());
  const std::wstring wide = ToWide(url);
  const HINSTANCE launched =
      ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(launched) <= 32) {
    return Fail(L"no se pudo abrir el navegador");
  }

  const std::string query = loopback.Wait(kConsentSeconds);
  if (query.empty()) return Fail(L"no llegó respuesta del navegador");

  if (const std::string denied = Field(query, "error"); !denied.empty()) {
    return Fail(L"Google no dio permiso: " + ToWide(denied));
  }
  // Anybody can point a browser at a port on this machine. The state is what says the answer
  // belongs to the request that was made.
  if (Field(query, "state") != state) return Fail(L"la respuesta no es de esta petición");

  const std::string code = Field(query, "code");
  if (code.empty()) return Fail(L"la respuesta no trae código");

  return Exchange(code, verifier, loopback.port());
}

bool GoogleAuth::Exchange(std::string_view code, std::string_view verifier, int port) {
  const std::string redirect = std::format("http://127.0.0.1:{}", port);
  const std::string body = FormEncode({{"client_id", config_.clientId},
                                       {"client_secret", config_.clientSecret},
                                       {"code", code},
                                       {"code_verifier", verifier},
                                       {"grant_type", "authorization_code"},
                                       {"redirect_uri", redirect}});

  HttpResponse response;
  if (!http_.Send(kTokenHost, L"POST", kTokenPath, kFormHeaders, body, response)) {
    return Fail(L"no se pudo hablar con Google para canjear el código");
  }
  if (response.status != 200) {
    // The body of a token error names the reason -- invalid_client, invalid_grant -- and
    // contains no token, so it is the one response worth logging.
    return Fail(std::format(L"Google rechazó el código ({}): {}", response.status,
                            ToWide(response.body)));
  }
  if (!ReadTokenResponse(response.body)) return false;
  return SaveRefresh();
}

bool GoogleAuth::Refresh() {
  std::string token;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    token = refresh_;
  }
  if (token.empty()) return Fail(L"no hay cuenta conectada");
  if (!http_.Open()) return Fail(L"no se pudo abrir la conexión con Google");

  const std::string body = FormEncode({{"client_id", config_.clientId},
                                       {"client_secret", config_.clientSecret},
                                       {"refresh_token", token},
                                       {"grant_type", "refresh_token"}});
  Scrub(token);

  HttpResponse response;
  if (!http_.Send(kTokenHost, L"POST", kTokenPath, kFormHeaders, body, response)) {
    return Fail(L"no se pudo renovar el permiso");
  }
  if (response.status != 200) {
    // invalid_grant here almost always means the same thing: the Google Cloud project is still
    // in testing mode, where a refresh token dies after seven days. docs/google-setup.md says
    // so, and this line is where somebody finds out.
    Fail(std::format(L"Google rechazó la renovación ({}): {}", response.status,
                     ToWide(response.body)));
    if (response.status == 400 || response.status == 401) Disconnect();
    return false;
  }
  if (!ReadTokenResponse(response.body)) return false;
  // Google only sends a new refresh token when it feels like it; SaveRefresh keeps whatever is
  // current, which may be the one already on disk.
  return SaveRefresh();
}

bool GoogleAuth::ReadTokenResponse(const std::string& body) {
  const nlohmann::json parsed = nlohmann::json::parse(body, nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return Fail(L"la respuesta del token no es JSON");
  }

  const auto access = parsed.find("access_token");
  if (access == parsed.end() || !access->is_string()) {
    return Fail(L"la respuesta del token no trae access_token");
  }

  std::lock_guard<std::mutex> lock(mutex_);
  Scrub(access_);
  access_ = access->get<std::string>();

  std::int64_t lifetime = 3600;
  if (const auto expires = parsed.find("expires_in");
      expires != parsed.end() && expires->is_number_integer()) {
    lifetime = expires->get<std::int64_t>();
  }
  expiresAt_ = NowSeconds() + lifetime;

  if (const auto renewed = parsed.find("refresh_token");
      renewed != parsed.end() && renewed->is_string()) {
    Scrub(refresh_);
    refresh_ = renewed->get<std::string>();
  }
  loaded_ = true;
  return true;
}

// --- Handing it out -------------------------------------------------------------------------

bool GoogleAuth::Header(std::wstring& out) {
  if (!Configured()) return false;
  if (!Connected()) return false;

  bool stale = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stale = access_.empty() || NowSeconds() + kRenewBeforeSeconds >= expiresAt_;
  }
  if (stale && !Refresh()) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  if (access_.empty()) return false;
  out = L"Authorization: Bearer " + ToWide(access_) + L"\r\n";
  return true;
}

// --- At rest ------------------------------------------------------------------------------

bool GoogleAuth::SaveRefresh() {
  std::string token;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    token = refresh_;
  }
  if (token.empty()) return true;  // nothing new to write; what is on disk still stands

  // The entropy is fixed and in the executable, so it is not a second secret. What it buys is
  // that another program running as this same user cannot decrypt the file by handing the blob
  // straight back to DPAPI.
  static constexpr char kEntropy[] = "Agenda:google-refresh";
  DATA_BLOB plain{static_cast<DWORD>(token.size()),
                  reinterpret_cast<BYTE*>(const_cast<char*>(token.data()))};
  DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy) - 1),
                    reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
  DATA_BLOB sealed{};
  if (CryptProtectData(&plain, L"Agenda", &entropy, nullptr, nullptr,
                       CRYPTPROTECT_UI_FORBIDDEN, &sealed) == FALSE) {
    Scrub(token);
    return Fail(L"no se pudo cifrar el token");
  }
  Scrub(token);

  std::error_code ignored;
  std::filesystem::create_directories(AppDataDir(), ignored);
  std::ofstream file(TokenFile(), std::ios::binary | std::ios::trunc);
  const bool written =
      file && file.write(reinterpret_cast<const char*>(sealed.pbData), sealed.cbData).good();
  LocalFree(sealed.pbData);
  if (!written) return Fail(L"no se pudo guardar el token");

  LogInfo(L"oauth: cuenta conectada, token guardado en {}", TokenFile().wstring());
  return true;
}

bool GoogleAuth::LoadRefresh() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    loaded_ = true;  // asked and answered, even when the answer is no
  }

  std::ifstream file(TokenFile(), std::ios::binary);
  if (!file) return false;
  const std::string sealed((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
  if (sealed.empty()) return false;

  static constexpr char kEntropy[] = "Agenda:google-refresh";
  DATA_BLOB blob{static_cast<DWORD>(sealed.size()),
                 reinterpret_cast<BYTE*>(const_cast<char*>(sealed.data()))};
  DATA_BLOB entropy{static_cast<DWORD>(sizeof(kEntropy) - 1),
                    reinterpret_cast<BYTE*>(const_cast<char*>(kEntropy))};
  DATA_BLOB plain{};
  if (CryptUnprotectData(&blob, nullptr, &entropy, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                         &plain) == FALSE) {
    // A token from another Windows account, or a file that got copied here. Not an error worth
    // stopping for: it means "no account", and connecting again fixes it.
    LogError(L"oauth: el token guardado no se puede descifrar en esta cuenta de Windows");
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    Scrub(refresh_);
    refresh_.assign(reinterpret_cast<const char*>(plain.pbData), plain.cbData);
  }
  SecureZeroMemory(plain.pbData, plain.cbData);
  LocalFree(plain.pbData);
  return true;
}

void GoogleAuth::Disconnect() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Scrub(access_);
    Scrub(refresh_);
    expiresAt_ = 0;
    loaded_ = true;
  }
  std::error_code ignored;
  std::filesystem::remove(TokenFile(), ignored);
  LogInfo(L"oauth: cuenta desconectada");
}

}  // namespace agenda::sync
