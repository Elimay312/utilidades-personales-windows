#include <doctest/doctest.h>

#include "system/wifi.h"

using namespace panel;

TEST_CASE("link quality becomes the bars Windows draws") {
  CHECK(BarsFromQuality(0) == 0);
  CHECK(BarsFromQuality(10) == 1);
  CHECK(BarsFromQuality(30) == 2);
  CHECK(BarsFromQuality(60) == 3);
  CHECK(BarsFromQuality(100) == 4);
}

TEST_CASE("one row per network, with the best of what Windows listed for it") {
  // Windows lists a saved network twice: once with its profile, once without.
  const std::vector<FoundNetwork> merged = MergeNetworks({
      {L"Oficina", L"", 2, true, false},
      {L"Casa", L"", 3, true, false},
      {L"Casa", L"Casa", 1, true, true},
      {L"", L"", 4, true, false},  // a hidden network: no name, no row
      {L"Cafe", L"", 4, false, false},
  });
  REQUIRE(merged.size() == 3);
  // The one in use first, saved, with the strongest signal of its two entries.
  CHECK(merged[0].ssid == L"Casa");
  CHECK(merged[0].connected);
  CHECK(merged[0].profile == L"Casa");
  CHECK(merged[0].bars == 3);
  // Then the rest by signal: the open cafe beats the office.
  CHECK(merged[1].ssid == L"Cafe");
  CHECK_FALSE(merged[1].secured);
  CHECK(merged[2].ssid == L"Oficina");
}

TEST_CASE("saved networks go before stronger ones Windows has no profile for") {
  const std::vector<FoundNetwork> merged = MergeNetworks({
      {L"Vecino", L"", 4, true, false},
      {L"Trabajo", L"Trabajo", 1, true, false},
  });
  REQUIRE(merged.size() == 2);
  CHECK(merged[0].ssid == L"Trabajo");
}
