#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <vector>

#include "app/monitors.h"

namespace {

agenda::Options Parse(std::initializer_list<const wchar_t*> args, std::wstring_view env = {}) {
  std::vector<const wchar_t*> argv{L"Agenda.exe"};
  argv.insert(argv.end(), args);
  return agenda::ParseOptions(static_cast<int>(argv.size()), argv.data(), env);
}

}  // namespace

TEST_CASE("no arguments and no environment: the default monitor") {
  const agenda::Options options = Parse({});
  CHECK(options.error.empty());
  CHECK(options.monitor == agenda::kDefaultMonitor);
}

TEST_CASE("--monitor=N sets the monitor") {
  CHECK(Parse({L"--monitor=2"}).monitor == 2);
  CHECK(Parse({L"--monitor=0"}).monitor == 0);
}

TEST_CASE("AGENDA_DEV_MONITOR applies when --monitor is absent") {
  CHECK(Parse({}, L"3").monitor == 3);
}

TEST_CASE("--monitor wins over AGENDA_DEV_MONITOR") {
  CHECK(Parse({L"--monitor=1"}, L"3").monitor == 1);
}

TEST_CASE("unknown arguments are ignored") {
  const agenda::Options options = Parse({L"--quiet", L"--monitor=2"});
  CHECK(options.error.empty());
  CHECK(options.monitor == 2);
}

TEST_CASE("a non-numeric value is an error") {
  CHECK_FALSE(Parse({L"--monitor=abc"}).error.empty());
  CHECK_FALSE(Parse({L"--monitor="}).error.empty());
  CHECK_FALSE(Parse({}, L"tres").error.empty());
}

TEST_CASE("--render-snapshot picks the view and names the file itself") {
  const agenda::Options options = Parse({L"--render-snapshot=popup"});
  CHECK(options.error.empty());
  CHECK(options.snapshotView == L"popup");
  CHECK(options.snapshotOut == L"shot.png");
}

TEST_CASE("--out says where the snapshot goes") {
  const agenda::Options options = Parse({L"--render-snapshot=popup", L"--out=docs/img/popup.png"});
  CHECK(options.error.empty());
  CHECK(options.snapshotOut == L"docs/img/popup.png");
}

TEST_CASE("an unknown view or an empty path is an error") {
  CHECK_FALSE(Parse({L"--render-snapshot=mes"}).error.empty());
  CHECK_FALSE(Parse({L"--out="}).error.empty());
}

TEST_CASE("without --render-snapshot the app just runs") {
  const agenda::Options options = Parse({L"--monitor=3"});
  CHECK(options.snapshotView.empty());
  CHECK(options.snapshotOut.empty());
}
