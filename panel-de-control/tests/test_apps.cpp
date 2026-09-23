#include <doctest/doctest.h>

#include "system/apps.h"

using namespace panel;

namespace {

std::wstring FakeExpand(std::wstring_view text) {
  std::wstring out(text);
  const std::wstring variable = L"%LOCALAPPDATA%";
  if (const size_t at = out.find(variable); at != std::wstring::npos) {
    out.replace(at, variable.size(), L"C:\\Users\\eli\\AppData\\Local");
  }
  return out;
}

}  // namespace

TEST_CASE("only absolute paths to an .exe are something the panel would start") {
  CHECK(LooksLikeExe(L"C:\\Users\\eli\\AppData\\Local\\Dock\\app\\Dock.exe"));
  CHECK(LooksLikeExe(L"D:\\apps\\Rayo.EXE"));
  CHECK(LooksLikeExe(L"\\\\server\\share\\tool.exe"));
  CHECK_FALSE(LooksLikeExe(L"Dock.exe"));                    // relative: whose folder?
  CHECK_FALSE(LooksLikeExe(L"C:\\apps\\script.bat"));        // not an exe
  CHECK_FALSE(LooksLikeExe(L"C:\\apps\\Dock.exe -x"));       // a command line, not a path
  CHECK_FALSE(LooksLikeExe(L"cmd /c C:\\apps\\Dock.exe"));
  CHECK_FALSE(LooksLikeExe(L""));
}

TEST_CASE("paths compare the way Windows does") {
  CHECK(SamePath(L"C:\\Users\\Eli\\Dock.exe", L"c:/users/eli/DOCK.EXE"));
  CHECK_FALSE(SamePath(L"C:\\a\\Dock.exe", L"C:\\b\\Dock.exe"));
}

TEST_CASE("icons are written as the hex of their code point") {
  CHECK(GlyphFromHex("E8A9") == 0xE8A9);
  CHECK(GlyphFromHex("e721") == 0xE721);
  CHECK(GlyphFromHex("") == 0);
  CHECK(GlyphFromHex("XYZ") == 0);
  CHECK(GlyphFromHex("12345") == 0);
}

TEST_CASE("the row comes from panel.json, and falls back to the defaults") {
  const auto defaults = ReadUtilities(nlohmann::json::object(), FakeExpand);
  REQUIRE(defaults.size() == 6);
  CHECK(defaults[0].name == L"Dock");
  CHECK(defaults[0].exe == L"C:\\Users\\eli\\AppData\\Local\\Dock\\app\\Dock.exe");
  // Every default can be closed, and Agenda through its app window, not its popup.
  for (const Utility& utility : defaults) CHECK_FALSE(utility.window.empty());
  CHECK(defaults[5].window == L"AgendaApp");

  const nlohmann::json config = nlohmann::json::parse(R"({"utilidades": [
      {"nombre": "Rayo", "exe": "%LOCALAPPDATA%\\Programs\\Rayo\\rayo.exe", "icono": "E8B7", "ventana": "RayoMain"},
      {"nombre": "Sin exe"},
      {"exe": "C:\\sin\\nombre.exe"},
      {"nombre": "Mía", "exe": "D:\\mia.exe"}
  ]})");
  const auto mine = ReadUtilities(config, FakeExpand);
  REQUIRE(mine.size() == 2);  // the two incomplete ones are skipped
  CHECK(mine[0].name == L"Rayo");
  CHECK(mine[0].exe == L"C:\\Users\\eli\\AppData\\Local\\Programs\\Rayo\\rayo.exe");
  CHECK(mine[0].glyph == 0xE8B7);
  CHECK(mine[0].window == L"RayoMain");
  CHECK(mine[1].window.empty());  // no window class: startable, not closable
  CHECK(mine[1].name == L"Mía");
  CHECK(mine[1].glyph != 0);  // no icon given: a generic one, not nothing

  // Not a list: the defaults, not an empty row.
  CHECK(ReadUtilities(nlohmann::json::parse(R"({"utilidades": "Dock"})"), FakeExpand).size() == 6);
}
