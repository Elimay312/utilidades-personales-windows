#include <doctest/doctest.h>

#include "system/display_ids.h"

using namespace panel;

// The three screens of the user's desk, as DisplayConfig and WMI named them (phase 4b).
TEST_CASE("a DisplayConfig path becomes the instance WMI names") {
  const std::wstring laptop = InstanceFromDevicePath(
      L"\\\\?\\DISPLAY#AUO7EAD#5&f094e1b&0&UID4355#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}");
  CHECK(laptop == L"DISPLAY\\AUO7EAD\\5&f094e1b&0&UID4355");
  CHECK(SameInstance(laptop, InstanceFromWmi(L"DISPLAY\\AUO7EAD\\5&f094e1b&0&UID4355_0")));
  // Case differs between the two APIs on some drivers.
  CHECK(SameInstance(laptop, L"display\\auo7ead\\5&F094E1B&0&uid4355"));

  const std::wstring lg = InstanceFromDevicePath(
      L"\\\\?\\DISPLAY#GSM5BF7#5&f094e1b&0&UID4354#{e6f07b5f-ee97-4a90-b076-33f57bf4eaa7}");
  CHECK_FALSE(SameInstance(lg, laptop));
  CHECK_FALSE(SameInstance(L"", L""));

  CHECK(InstanceFromDevicePath(L"").empty());
  CHECK(InstanceFromDevicePath(L"\\\\.\\DISPLAY1").empty());  // a GDI name, not a device path
  CHECK(InstanceFromWmi(L"DISPLAY\\GSM5BF7\\x") == L"DISPLAY\\GSM5BF7\\x");
}

TEST_CASE("built-in panels are told by their output technology") {
  CHECK(IsInternalOutput(11));  // the laptop here: DisplayPort embedded
  CHECK(IsInternalOutput(static_cast<long>(0x80000000)));
  CHECK(IsInternalOutput(13));
  CHECK_FALSE(IsInternalOutput(5));   // HDMI: the LG
  CHECK_FALSE(IsInternalOutput(10));  // DisplayPort external: the ARZOPA over USB-C
}
