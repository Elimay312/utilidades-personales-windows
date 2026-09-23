#include <doctest/doctest.h>

#include "system/radios.h"

using namespace panel;

TEST_CASE("a device's classic and LE faces have the same address") {
  const std::wstring classic =
      BluetoothAddressFromId(L"Bluetooth#Bluetooth3c:58:c2:aa:bb:cc-a4:c1:38:11:22:33");
  const std::wstring le =
      BluetoothAddressFromId(L"BluetoothLE#BluetoothLE3c:58:c2:aa:bb:cc-A4:C1:38:11:22:33");
  CHECK(classic == L"a4:c1:38:11:22:33");
  CHECK(le == classic);  // and upper or lower case does not make it two devices
}

TEST_CASE("two different devices on the same adapter stay two") {
  CHECK(BluetoothAddressFromId(L"BluetoothLE#BluetoothLE3c:58:c2:aa:bb:cc-a4:c1:38:11:22:33") !=
        BluetoothAddressFromId(L"BluetoothLE#BluetoothLE3c:58:c2:aa:bb:cc-f0:99:b6:44:55:66"));
}

TEST_CASE("an id without the usual shape is its own address, so it still counts") {
  CHECK(BluetoothAddressFromId(L"SWD\\SOMETHING") == L"swd\\something");
}
