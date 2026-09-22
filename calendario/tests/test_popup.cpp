#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "app/hotkey.h"
#include "ui/layout.h"

using namespace agenda;

TEST_CASE("the default shortcut parses") {
  const auto hotkey = ParseHotkey(kDefaultHotkey);
  REQUIRE(hotkey);
  CHECK(hotkey->vk == 'C');
  CHECK((hotkey->mods & MOD_ALT) != 0);
  CHECK((hotkey->mods & MOD_SHIFT) != 0);
  CHECK((hotkey->mods & MOD_CONTROL) == 0);
  CHECK((hotkey->mods & MOD_NOREPEAT) != 0);  // holding the keys must not repeat
}

TEST_CASE("case and spacing do not matter") {
  const auto hotkey = ParseHotkey(" ctrl + Alt +space ");
  REQUIRE(hotkey);
  CHECK(hotkey->vk == VK_SPACE);
  CHECK((hotkey->mods & (MOD_CONTROL | MOD_ALT)) == (MOD_CONTROL | MOD_ALT));
}

TEST_CASE("function keys and digits") {
  REQUIRE(ParseHotkey("Win+F12"));
  CHECK(ParseHotkey("Win+F12")->vk == VK_F12);
  CHECK(ParseHotkey("F1")->vk == VK_F1);
  CHECK(ParseHotkey("Ctrl+7")->vk == '7');
  CHECK_FALSE(ParseHotkey("F25"));
  CHECK_FALSE(ParseHotkey("F0"));
}

TEST_CASE("nonsense is rejected instead of half understood") {
  CHECK_FALSE(ParseHotkey(""));
  CHECK_FALSE(ParseHotkey("Alt+Shift"));  // modifiers but no key
  CHECK_FALSE(ParseHotkey("Alt+C+D"));    // two keys
  CHECK_FALSE(ParseHotkey("Alt+Banana"));
}

TEST_CASE("the popup hugs the bottom right corner of the work area") {
  const RECT rect = PopupRect(RECT{0, 0, 1920, 1040}, USER_DEFAULT_SCREEN_DPI);
  CHECK(rect.right == 1920 - 12);
  CHECK(rect.bottom == 1040 - 12);
  // 42 % of a 1040 DIP work area, and the width follows the 340:420 of the design.
  CHECK(rect.bottom - rect.top == 437);
  CHECK(rect.right - rect.left == 354);
}

TEST_CASE("the popup scales with the monitor DPI") {
  // A monitor sitting to the left of the primary one, at 150%: 1280x720 DIP of work area.
  const RECT rect = PopupRect(RECT{-1920, 100, 0, 1180}, 144);
  CHECK(rect.right == -18);              // the 12 dip margin, scaled
  CHECK(rect.bottom - rect.top == 570);  // 380 dip, the floor, at 150%
  CHECK(rect.right - rect.left == 462);  // 308 dip
}

TEST_CASE("the panel never grows past its ceiling or shrinks past its floor") {
  const D2D1_SIZE_F tall = PanelSize(RECT{0, 0, 3440, 1440}, USER_DEFAULT_SCREEN_DPI);
  CHECK(tall.height == 560.0f);  // 42 % of 1440 is 605, over the ceiling
  const D2D1_SIZE_F narrow = PanelSize(RECT{0, 0, 1280, 720}, USER_DEFAULT_SCREEN_DPI);
  CHECK(narrow.height == 380.0f);  // 42 % of 720 is 302, under the floor
  CHECK(narrow.width < tall.width);
}

TEST_CASE("the panel never spills out of a tiny work area") {
  const D2D1_SIZE_F size = PanelSize(RECT{0, 0, 800, 400}, USER_DEFAULT_SCREEN_DPI);
  CHECK(size.height <= 400.0f - 24.0f);
  CHECK(size.width <= 800.0f - 24.0f);
}

TEST_CASE("at its base size the panel is exactly what the design system says") {
  const PanelLayout layout = BaseLayout();
  CHECK(layout.width == 340.0f);
  CHECK(layout.height == 420.0f);
  CHECK(layout.type == 1.0f);
  CHECK(layout.padding == 16.0f);
  CHECK(layout.contentWidth == 308.0f);
  CHECK(layout.cellWidth == 44.0f);
  CHECK(layout.cellHeight == 36.0f);
  CHECK(layout.gridTop == 68.0f);
  CHECK(layout.dayCircle == 24.0f);
  CHECK(layout.dotCenterY == 30.0f);
  CHECK(layout.listTop == 292.0f);
  CHECK(layout.listHeight == 68.0f);
  CHECK(layout.cardHeight == 32.0f);
  CHECK(layout.visibleCards == 2);
  CHECK(layout.inputTop == 368.0f);
  CHECK(layout.inputHeight == 36.0f);
  CHECK(layout.panelRadius == 14.0f);
  CHECK(layout.cardRadius == 8.0f);
  CHECK(layout.barWidth == 3.0f);
  CHECK(layout.fontTitle == 15.0f);
}

TEST_CASE("the month grid fills the panel width in seven exact columns") {
  const PanelLayout layout = BaseLayout();
  CHECK(layout.contentWidth == layout.cellWidth * kGridCols);
  CHECK(layout.contentRight == layout.width - layout.padding);
  CHECK(layout.grid().bottom == layout.gridTop + layout.gridHeight);
}

TEST_CASE("a point in a cell hits the day drawn in it") {
  const PanelLayout layout = BaseLayout();
  const D2D1_RECT_F first = layout.cell(0);
  CHECK(Inside(first, first.left + 1.0f, first.top + 1.0f));
  CHECK_FALSE(Inside(first, first.right, first.top + 1.0f));

  // Cell 8 is the second column of the second row, and nothing else claims its centre.
  const D2D1_RECT_F eighth = layout.cell(8);
  CHECK(eighth.left == layout.contentLeft + layout.cellWidth);
  CHECK(eighth.top == layout.gridTop + layout.cellHeight);
  CHECK_FALSE(Inside(first, eighth.left + 1.0f, eighth.top + 1.0f));
}

TEST_CASE("the panel regions never overlap, whatever size it gets") {
  for (const D2D1_SIZE_F size :
       {D2D1_SIZE_F{308.0f, 380.0f}, D2D1_SIZE_F{340.0f, 420.0f}, D2D1_SIZE_F{453.0f, 560.0f}}) {
    const PanelLayout layout = MakeLayout(size);
    CAPTURE(size.width, size.height);
    CHECK(layout.prevArrow().right < layout.nextArrow().left);
    CHECK(layout.nextArrow().right == layout.contentRight);
    CHECK(layout.headerTop + layout.headerHeight <= layout.weekdayTop);
    CHECK(layout.grid().bottom < layout.list().top);
    CHECK(layout.list().bottom <= layout.input().top);
    CHECK(layout.input().bottom == layout.height - layout.padding);
    CHECK(layout.visibleCards >= 2);
    // The cards the layout promises really do fit in the room it left them.
    const float needed = layout.visibleCards * layout.cardHeight +
                         (layout.visibleCards - 1) * layout.gap;
    CHECK(needed <= layout.listHeight);
  }
}

TEST_CASE("a bigger panel means bigger content, not more empty panel") {
  const PanelLayout base = BaseLayout();
  const PanelLayout tall = MakeLayout(D2D1_SIZE_F{453.0f, 560.0f});
  const float grown = tall.height / base.height;

  // Everything the eye lands on grows by the same amount the panel did.
  CHECK(tall.fontEvent == Catch::Approx(base.fontEvent * grown).epsilon(0.02));
  CHECK(tall.fontTitle == Catch::Approx(base.fontTitle * grown).epsilon(0.02));
  CHECK(tall.cellHeight == Catch::Approx(base.cellHeight * grown).epsilon(0.02));
  CHECK(tall.cardHeight == Catch::Approx(base.cardHeight * grown).epsilon(0.02));
  CHECK(tall.dayCircle == Catch::Approx(base.dayCircle * grown).epsilon(0.02));

  // And the proportions hold: the panel does not fill up with air.
  CHECK(tall.listHeight / tall.height == Catch::Approx(base.listHeight / base.height).epsilon(0.02));
  CHECK(tall.gridHeight / tall.height == Catch::Approx(base.gridHeight / base.height).epsilon(0.02));
}
