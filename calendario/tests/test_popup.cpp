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
  CHECK(rect.right - rect.left == 340);
  CHECK(rect.bottom - rect.top == 420);
}

TEST_CASE("the popup scales with the monitor DPI") {
  // A monitor sitting to the left of the primary one, at 150%.
  const RECT rect = PopupRect(RECT{-1920, 100, 0, 1180}, 144);
  CHECK(rect.right == -18);              // the 12 dip margin, scaled
  CHECK(rect.right - rect.left == 510);  // 340 dip
  CHECK(rect.bottom - rect.top == 630);  // 420 dip
}

TEST_CASE("the month grid fills the panel width in seven exact columns") {
  CHECK(kCellWidth == 44.0f);
  CHECK(kContentWidth == kCellWidth * kGridCols);
  CHECK(kContentRight == kPopupWidthDip - kPaddingDip);
  CHECK(GridRect().bottom == kGridTop + kGridHeight);
}

TEST_CASE("a point in a cell hits the day drawn in it") {
  const D2D1_RECT_F first = CellRect(0);
  CHECK(Inside(first, first.left + 1.0f, first.top + 1.0f));
  CHECK_FALSE(Inside(first, first.right, first.top + 1.0f));

  // Cell 8 is the second column of the second row, and nothing else claims its centre.
  const D2D1_RECT_F eighth = CellRect(8);
  CHECK(eighth.left == kContentLeft + kCellWidth);
  CHECK(eighth.top == kGridTop + kCellHeight);
  CHECK_FALSE(Inside(CellRect(0), eighth.left + 1.0f, eighth.top + 1.0f));
}

TEST_CASE("the panel regions never overlap") {
  CHECK(PrevArrowRect().right < NextArrowRect().left);
  CHECK(NextArrowRect().right == kContentRight);
  CHECK(GridRect().bottom < ListRect().top);
  CHECK(ListRect().bottom < InputRect().top);
  CHECK(InputRect().bottom == kPopupHeightDip - kPaddingDip);
  // Two cards and the air between them are exactly the list.
  CHECK(kCardHeight * kVisibleCards + kGapDip == kListHeight);
}
