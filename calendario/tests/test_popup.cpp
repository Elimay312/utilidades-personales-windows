#include <catch2/catch_test_macros.hpp>

#include "app/hotkey.h"
#include "ui/layout.h"

using agenda::ParseHotkey;
using agenda::PopupRect;

TEST_CASE("the default shortcut parses") {
  const auto hotkey = ParseHotkey(agenda::kDefaultHotkey);
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
