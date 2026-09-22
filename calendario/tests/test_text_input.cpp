#include <catch2/catch_test_macros.hpp>

#include "ui/text_input.h"

using namespace agenda;

TEST_CASE("typing appends and drags the caret along") {
  TextInput input;
  input.Insert(L"ma");
  input.Insert(L"ñana");
  CHECK(input.text() == L"mañana");
  CHECK(input.caret() == 6);
  CHECK_FALSE(input.hasSelection());
}

TEST_CASE("a single line input drops control characters") {
  TextInput input;
  input.Insert(L"pagar\r\n la luz\t");
  CHECK(input.text() == L"pagar la luz");
}

TEST_CASE("backspace removes one character and then stops at the start") {
  TextInput input;
  input.Insert(L"gym");
  CHECK(input.Backspace());
  CHECK(input.text() == L"gy");
  CHECK(input.Backspace());
  CHECK(input.Backspace());
  CHECK_FALSE(input.Backspace());
  CHECK(input.empty());
}

TEST_CASE("delete removes forward without moving the caret") {
  TextInput input;
  input.Insert(L"hoy");
  input.MoveHome(false);
  CHECK(input.DeleteForward());
  CHECK(input.text() == L"oy");
  CHECK(input.caret() == 0);
}

TEST_CASE("shift and the arrows grow a selection") {
  TextInput input;
  input.Insert(L"dentista");
  input.MoveHome(false);
  input.MoveRight(true);
  input.MoveRight(true);
  CHECK(input.hasSelection());
  CHECK(input.selectedText() == L"de");
  CHECK(input.selectionStart() == 0);
  CHECK(input.selectionEnd() == 2);
}

TEST_CASE("an arrow without shift collapses the selection to its edge") {
  TextInput input;
  input.Insert(L"dentista");
  input.SelectAll();
  input.MoveLeft(false);
  CHECK_FALSE(input.hasSelection());
  CHECK(input.caret() == 0);

  input.SelectAll();
  input.MoveRight(false);
  CHECK(input.caret() == 8);
}

TEST_CASE("typing over a selection replaces it") {
  TextInput input;
  input.Insert(L"dentista");
  input.SelectAll();
  input.Insert(L"gym");
  CHECK(input.text() == L"gym");
  CHECK(input.caret() == 3);
}

TEST_CASE("backspace over a selection removes the selection, not one character") {
  TextInput input;
  input.Insert(L"comprar leche");
  input.MoveHome(false);
  for (int i = 0; i < 8; ++i) input.MoveRight(true);
  CHECK(input.Backspace());
  CHECK(input.text() == L"leche");
  CHECK(input.caret() == 0);
}

TEST_CASE("select all covers the whole line and home and end walk to the edges") {
  TextInput input;
  input.Insert(L"el 25 almuerzo");
  input.SelectAll();
  CHECK(input.selectedText() == L"el 25 almuerzo");
  input.MoveHome(false);
  CHECK(input.caret() == 0);
  input.MoveEnd(false);
  CHECK(input.caret() == 14);
  CHECK_FALSE(input.hasSelection());
}

TEST_CASE("the caret steps over a surrogate pair in one go") {
  TextInput input;
  input.Insert(L"a\U0001F600b");  // an emoji is two code units
  CHECK(input.text().size() == 4);
  input.MoveHome(false);
  input.MoveRight(false);
  input.MoveRight(false);
  CHECK(input.caret() == 3);
  CHECK(input.Backspace());
  CHECK(input.text() == L"ab");
}

TEST_CASE("clicking places the caret and clamps past the end") {
  TextInput input;
  input.Insert(L"hoy 17:00");
  input.MoveTo(4, false);
  CHECK(input.caret() == 4);
  input.MoveTo(99, false);
  CHECK(input.caret() == 9);
}
