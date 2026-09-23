#include <doctest/doctest.h>

#include <vector>

#include "core/options.h"

using namespace panel;

namespace {

Options Parse(std::initializer_list<const wchar_t*> args, std::wstring_view env = L"") {
  std::vector<const wchar_t*> argv{L"Panel.exe"};
  argv.insert(argv.end(), args.begin(), args.end());
  return ParseOptions(static_cast<int>(argv.size()), argv.data(), env);
}

}  // namespace

TEST_CASE("no arguments and no environment: the default monitor") {
  const Options options = Parse({});
  CHECK(options.error.empty());
  CHECK(options.monitor == kDefaultMonitor);
  CHECK(options.snapshotView.empty());
}

TEST_CASE("--monitor wins over PANEL_DEV_MONITOR, which wins over the default") {
  CHECK(Parse({L"--monitor=2"}).monitor == 2);
  CHECK(Parse({}, L"1").monitor == 1);
  CHECK(Parse({L"--monitor=2"}, L"1").monitor == 2);
}

TEST_CASE("a monitor that is not a number is an error, not a guess") {
  CHECK_FALSE(Parse({L"--monitor=tres"}).error.empty());
  CHECK_FALSE(Parse({}, L"x").error.empty());
}

TEST_CASE("--render-snapshot knows its views and names the file itself") {
  const Options options = Parse({L"--render-snapshot=panel-volumen"});
  CHECK(options.error.empty());
  CHECK(options.snapshotView == L"panel-volumen");
  CHECK(options.snapshotOut == L"shot.png");
  CHECK(Parse({L"--render-snapshot=panel", L"--out=docs\\img\\panel.png"}).snapshotOut ==
        L"docs\\img\\panel.png");
  CHECK_FALSE(Parse({L"--render-snapshot=popup"}).error.empty());
  CHECK_FALSE(Parse({L"--out="}).error.empty());
}

TEST_CASE("--theme takes the three themes and nothing else") {
  CHECK(Parse({L"--theme=light"}).theme == L"light");
  CHECK(Parse({L"--theme=contrast"}).theme == L"contrast");
  CHECK_FALSE(Parse({L"--theme=blue"}).error.empty());
}
