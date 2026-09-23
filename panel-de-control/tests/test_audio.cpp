#include <doctest/doctest.h>

#include "system/audio.h"

using namespace panel;

TEST_CASE("each output says its short name when that tells it apart") {
  const std::vector<std::wstring> names = ChooseOutputNames({
      {L"Auriculares", L"Auriculares (Realtek(R) Audio)"},
      {L"Altavoces", L"Altavoces (Realtek(R) Audio)"},
  });
  CHECK(names == std::vector<std::wstring>{L"Auriculares", L"Altavoces"});
}

TEST_CASE("two outputs with the same short name both say the long one") {
  const std::vector<std::wstring> names = ChooseOutputNames({
      {L"Altavoces", L"Altavoces (Realtek(R) Audio)"},
      {L"Altavoces", L"Altavoces (LG ULTRAWIDE)"},
      {L"Auriculares", L"Auriculares (Realtek(R) Audio)"},
  });
  CHECK(names == std::vector<std::wstring>{L"Altavoces (Realtek(R) Audio)", L"Altavoces (LG ULTRAWIDE)",
                                           L"Auriculares"});
}

TEST_CASE("a driver that leaves a name empty gets the other one") {
  const std::vector<std::wstring> names = ChooseOutputNames({
      {L"", L"USB Audio Device"},
      {L"Altavoces", L""},
  });
  CHECK(names == std::vector<std::wstring>{L"USB Audio Device", L"Altavoces"});
}
