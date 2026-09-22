#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "ui/app_layout.h"
#include "ui/spring.h"

using namespace agenda;
using Catch::Approx;

namespace {

Date Day(int year, unsigned month, unsigned day) {
  return Date{std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
}

AppLayout BaseApp(AppView view, int allDayRows = 1) {
  return MakeAppLayout(D2D1_SIZE_F{kAppBaseWidthDip, kAppBaseHeightDip}, BaseLayout(), view,
                       allDayRows);
}

}  // namespace

TEST_CASE("the spring arrives, barely overshoots, and comes back the same way") {
  Spring spring;
  float highest = 0.0f;
  int frames = 0;
  while (spring.Step(1.0f / 60.0f, 1.0f)) {
    REQUIRE(std::isfinite(spring.x));
    highest = (std::max)(highest, spring.x);
    REQUIRE(++frames < 120);  // two seconds would be a spring that never settles
  }
  CHECK(spring.x == 1.0f);
  CHECK(spring.v == 0.0f);
  CHECK(highest < 1.01f);  // rigidity 300 and damping 30: under one percent past the end

  frames = 0;
  while (spring.Step(1.0f / 60.0f, 0.0f)) REQUIRE(++frames < 120);
  CHECK(spring.x == 0.0f);
}

TEST_CASE("a long hitch between frames is a step, not a jump to the end") {
  Spring spring;
  spring.Step(5.0f, 1.0f);  // a debugger break, a laptop waking up
  CHECK(spring.x < 0.5f);
}

TEST_CASE("the app takes 80 percent of the work area, centred") {
  const RECT rect = ExpandedRect(RECT{0, 0, 1920, 1032});
  CHECK(rect.right - rect.left == 1536);
  CHECK(rect.bottom - rect.top == 826);
  CHECK(rect.left == 192);
  CHECK(rect.top == 103);
}

TEST_CASE("the sidebar is the popup, and the top row fits beside it") {
  const PanelLayout popup = BaseLayout();
  const AppLayout app = BaseApp(AppView::Week);

  CHECK(app.sidebarRight == popup.width);
  CHECK(app.sectionTop == popup.listTop);  // the calendars take the day list's place
  CHECK(app.main.left > app.sidebarRight);
  CHECK(app.main.right == Approx(app.width - app.padding));

  // Left to right without overlapping: arrows, period, capsule, tabs, collapse.
  CHECK(app.next.right <= app.title.left);
  CHECK(app.title.right <= app.input.left);
  CHECK(app.input.right <= app.tabs[0].left);
  CHECK(app.tabs[2].right <= app.collapse.left);
  CHECK(app.collapse.right == Approx(app.width - app.padding));
  // The capsule keeps the popup's height, so it can travel without changing shape.
  CHECK(app.input.bottom - app.input.top == popup.inputHeight);

  CHECK(app.columns == 7);
  CHECK(app.columnsLeft + 7.0f * app.columnWidth == Approx(app.main.right));
  CHECK(BaseApp(AppView::Day).columns == 1);
}

TEST_CASE("a minute and its height on the timeline are the same thing both ways") {
  const AppLayout app = BaseApp(AppView::Day);
  const float scroll = 7.0f * 60.0f;
  CHECK(YForMinute(app, scroll, scroll) == app.timeline.top);
  CHECK(YForMinute(app, scroll, 8.0f * 60.0f) == Approx(app.timeline.top + app.hourHeight));

  for (int minute = 7 * 60; minute <= 12 * 60; minute += kSnapMinutes) {
    CHECK(MinuteAtY(app, scroll, YForMinute(app, scroll, static_cast<float>(minute))) ==
          minute);
  }
  // Snapped to the quarter hour, and kept inside the day.
  CHECK(MinuteAtY(app, scroll, YForMinute(app, scroll, 9 * 60 + 8.0f)) == 9 * 60 + 15);
  CHECK(MinuteAtY(app, scroll, YForMinute(app, scroll, 9 * 60 + 7.0f)) == 9 * 60);
  CHECK(MinuteAtY(app, 0.0f, app.timeline.top - 500.0f) == 0);
  CHECK(MinuteAtY(app, MaxScroll(app), app.timeline.bottom + 500.0f) == kMinutesPerDay);
}

TEST_CASE("the column under the pointer, and none outside them") {
  const AppLayout app = BaseApp(AppView::Week);
  CHECK(ColumnAt(app, app.columnsLeft + 1.0f) == 0);
  CHECK(ColumnAt(app, app.columnsLeft + 3.5f * app.columnWidth) == 3);
  CHECK(ColumnAt(app, app.columnsLeft - 1.0f) == -1);
  CHECK(ColumnAt(app, app.main.right + 1.0f) == -1);
}

TEST_CASE("the timeline opens with now a third of the way down, or at eight") {
  const AppLayout app = BaseApp(AppView::Day);
  const float visible = VisibleMinutes(app);
  CHECK(InitialScroll(app, true, 10 * 60) == Approx(10.0f * 60.0f - visible / 3.0f));
  CHECK(InitialScroll(app, false, 10 * 60) == 8.0f * 60.0f);
  CHECK(InitialScroll(app, true, 23 * 60 + 50) == Approx(MaxScroll(app)));
  CHECK(InitialScroll(app, true, 10) == 0.0f);
}

TEST_CASE("overlapping events share the width, and a free column is reused") {
  // 9:00-10:00 and 9:30-10:30 overlap; 10:00-11:00 fits under the first.
  const std::vector<Lane> lanes =
      LayoutOverlaps({{9 * 60, 10 * 60}, {9 * 60 + 30, 10 * 60 + 30}, {10 * 60, 11 * 60}});
  REQUIRE(lanes.size() == 3);
  CHECK(lanes[0].column == 0);
  CHECK(lanes[1].column == 1);
  CHECK(lanes[2].column == 0);
  for (const Lane& lane : lanes) CHECK(lane.columns == 2);

  // An event on its own has the whole column, even after a crowded morning.
  const std::vector<Lane> apart =
      LayoutOverlaps({{9 * 60, 10 * 60}, {9 * 60, 10 * 60}, {15 * 60, 16 * 60}});
  CHECK(apart[0].columns == 2);
  CHECK(apart[2].column == 0);
  CHECK(apart[2].columns == 1);
}

TEST_CASE("the week starts on Monday and the month on its grid") {
  const Date tuesday = Day(2026, 9, 22);
  CHECK(FirstShown(AppView::Day, tuesday) == tuesday);
  CHECK(FirstShown(AppView::Week, tuesday) == Day(2026, 9, 21));
  CHECK(FirstShown(AppView::Week, Day(2026, 9, 27)) == Day(2026, 9, 21));
  CHECK(FirstShown(AppView::Month, tuesday) == Day(2026, 8, 31));
}

TEST_CASE("the capsule starts in the popup, ends in the app, and clears the grid on the way") {
  const PanelLayout popup = BaseLayout();
  const AppLayout app = BaseApp(AppView::Day);

  const PanelLayout start = MorphLayout(popup, app, 0.0f);
  CHECK(start.inputTop == popup.inputTop);
  CHECK(start.inputLeft == popup.inputLeft);
  CHECK_FALSE(start.previewBelow);

  const PanelLayout end = MorphLayout(popup, app, 1.0f);
  CHECK(end.inputTop == app.input.top);
  CHECK(end.inputLeft == app.input.left);
  CHECK(end.inputRight == app.input.right);
  CHECK(end.previewBelow);

  // Whenever the capsule is as high as the grid, it is already to the right of it.
  for (float p = 0.0f; p <= 1.0f; p += 0.05f) {
    const PanelLayout mid = MorphLayout(popup, app, p);
    const bool overGrid = mid.inputTop < popup.gridTop + popup.gridHeight;
    if (overGrid) CHECK(mid.inputLeft >= popup.contentRight);
  }
}
