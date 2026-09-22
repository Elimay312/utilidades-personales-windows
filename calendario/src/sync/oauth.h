#pragma once

// Getting permission from Google, and keeping it.
//
// OAuth 2.0 with PKCE and a loopback redirect, which is the flow Google documents for a desktop
// application and the only one that does not need a secret that is actually secret. The
// client_secret of a desktop app is in the executable of everybody who has it; what protects
// the exchange is the verifier, which never leaves this process.
//
// The listener is a plain socket on 127.0.0.1 with the port left to the system. Not the HTTP
// Server API, which would want a URL reservation and therefore an administrator, for the sake
// of receiving one GET.
//
// The refresh token is encrypted with DPAPI and written to %LOCALAPPDATA%\Agenda\token.bin. It
// is tied to the Windows account: copying that file to another machine, or another user on this
// one, gets nothing. The access token is never written anywhere -- it lasts an hour and asking
// for another costs one request.

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

#include "sync/http.h"

namespace agenda::sync {

struct OAuthConfig {
  std::string clientId;
  std::string clientSecret;
};

class GoogleAuth {
 public:
  explicit GoogleAuth(OAuthConfig config);
  ~GoogleAuth();

  GoogleAuth(const GoogleAuth&) = delete;
  GoogleAuth& operator=(const GoogleAuth&) = delete;

  // There are credentials in config.local.json. Without them Agenda is a local calendar and
  // says so once, instead of failing at every pass.
  bool Configured() const;
  // There is a refresh token on disk, so there is an account.
  bool Connected() const;

  // Opens the browser and waits for the redirect.
  //
  // CALLED ONLY AFTER THE USER HAS SAID YES. Launching the browser is on the list of things
  // CLAUDE.md says to ask about first, and the asking belongs to whoever has a window; this
  // runs on the synchronisation thread, which has none.
  bool Connect();

  // Forgets the account: the file goes, and so does what is in memory.
  void Disconnect();

  // An Authorization header good for the next few minutes, refreshing if it is not. False
  // means there is no usable permission right now and the caller should stop, not retry.
  bool Header(std::wstring& out);

  // True once, the first time somebody asks after Google has refused to renew the permission.
  //
  // It is not the same as being offline and must not be shown as such: the account is gone and
  // the only thing that brings it back is the user connecting again. In a Google Cloud project
  // left in testing mode this happens every seven days, which is exactly the case that must not
  // fail quietly -- Agenda would keep writing to the cache and nothing would ever go up.
  bool TakeLostAccount();

  std::wstring error() const;

 private:
  bool Exchange(std::string_view code, std::string_view verifier, int port);
  bool Refresh();
  bool ReadTokenResponse(const std::string& body);
  bool SaveRefresh();
  bool LoadRefresh();
  bool Fail(std::wstring_view what);

  OAuthConfig config_;
  Http http_;  // its own session: the token endpoint is a different host and a different life

  mutable std::mutex mutex_;
  std::string access_;
  std::string refresh_;
  std::int64_t expiresAt_ = 0;  // epoch seconds UTC
  bool loaded_ = false;
  bool lostAccount_ = false;
  std::wstring error_;
};

}  // namespace agenda::sync
