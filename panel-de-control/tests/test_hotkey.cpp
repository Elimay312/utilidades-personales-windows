#include <doctest/doctest.h>

#include "core/hotkey.h"

using namespace panel;

// Ported from calendario/tests/test_popup.cpp; only the default changed.

TEST_CASE("the default shortcut is Ctrl+Alt+A") {
  const auto hotkey = ParseHotkey(kDefaultHotkey);
  REQUIRE(hotkey);
  CHECK(hotkey->vk == 'A');
  CHECK((hotkey->mods & MOD_CONTROL) != 0);
  CHECK((hotkey->mods & MOD_ALT) != 0);
  CHECK((hotkey->mods & MOD_SHIFT) == 0);
  // Win+A belongs to Windows' quick settings and RegisterHotKey cannot take it.
  CHECK((hotkey->mods & MOD_WIN) == 0);
  CHECK((hotkey->mods & MOD_NOREPEAT) != 0);  // holding the keys must not toggle it over and over
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
