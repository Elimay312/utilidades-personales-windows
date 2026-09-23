#include <doctest/doctest.h>

#include "system/bt_audio.h"

using namespace panel;

namespace {

// The A2DP audio filter of the headphones on this machine, as Windows names it.
constexpr const wchar_t* kA2dp =
    L"\\\\?\\BTHENUM#{0000110b-0000-1000-8000-00805f9b34fb}_VID&000102b0_PID&0000#9&2f4ad928&0&"
    L"3409C9EC00AC_C00000000#{6994ad04-93ef-11d0-a3cc-00a0c9223196}\\wavesink";

}  // namespace

TEST_CASE("an address becomes the key a device path carries") {
  CHECK(AddressKey(L"34:09:c9:ec:00:ac") == L"3409c9ec00ac");
  CHECK(AddressKey(L"34-09-C9-EC-00-AC") == L"3409c9ec00ac");
}

TEST_CASE("only the audio filters of that device, from the Bluetooth enumerator") {
  CHECK(AudioFilterOf(kA2dp, AddressKey(L"34:09:c9:ec:00:ac")));
  // Another device's address is not in it.
  CHECK_FALSE(AudioFilterOf(kA2dp, AddressKey(L"d0:2e:bf:83:bd:b3")));
  // The same digits in a USB device's path do not make it a Bluetooth one.
  CHECK_FALSE(AudioFilterOf(L"\\\\?\\USB#VID_3409&PID_C9EC#3409c9ec00ac#{6994ad04}", L"3409c9ec00ac"));
  // A key that is not a whole address matches nothing, not everything.
  CHECK_FALSE(AudioFilterOf(kA2dp, L""));
  CHECK_FALSE(AudioFilterOf(kA2dp, L"3409"));
}
