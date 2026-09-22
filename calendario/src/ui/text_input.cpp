#include "ui/text_input.h"

namespace agenda {
namespace {

bool IsHighSurrogate(wchar_t c) { return c >= 0xD800 && c <= 0xDBFF; }
bool IsLowSurrogate(wchar_t c) { return c >= 0xDC00 && c <= 0xDFFF; }

// One caret step is one code point, not one code unit, so an emoji is never cut in half.
size_t StepLeft(const std::wstring& text, size_t index) {
  if (index == 0) return 0;
  --index;
  if (index > 0 && IsLowSurrogate(text[index]) && IsHighSurrogate(text[index - 1])) --index;
  return index;
}

size_t StepRight(const std::wstring& text, size_t index) {
  if (index >= text.size()) return text.size();
  ++index;
  if (index < text.size() && IsLowSurrogate(text[index]) && IsHighSurrogate(text[index - 1])) {
    ++index;
  }
  return index;
}

}  // namespace

std::wstring_view TextInput::selectedText() const {
  const size_t start = selectionStart();
  return std::wstring_view{text_}.substr(start, selectionEnd() - start);
}

void TextInput::Place(size_t index, bool extend) {
  caret_ = (std::min)(index, text_.size());
  if (!extend) anchor_ = caret_;
}

void TextInput::Insert(std::wstring_view inserted) {
  DeleteSelection();
  std::wstring clean;
  clean.reserve(inserted.size());
  for (const wchar_t c : inserted) {
    if ((c >= 0x20 && c != 0x7F) || (newlines_ && c == L'\n')) clean.push_back(c);
  }
  if (clean.empty()) return;
  text_.insert(caret_, clean);
  Place(caret_ + clean.size(), false);
}

bool TextInput::DeleteSelection() {
  if (!hasSelection()) return false;
  const size_t start = selectionStart();
  text_.erase(start, selectionEnd() - start);
  Place(start, false);
  return true;
}

bool TextInput::Backspace() {
  if (DeleteSelection()) return true;
  if (caret_ == 0) return false;
  const size_t from = StepLeft(text_, caret_);
  text_.erase(from, caret_ - from);
  Place(from, false);
  return true;
}

bool TextInput::DeleteForward() {
  if (DeleteSelection()) return true;
  if (caret_ >= text_.size()) return false;
  text_.erase(caret_, StepRight(text_, caret_) - caret_);
  Place(caret_, false);
  return true;
}

void TextInput::MoveLeft(bool extend) {
  // Without shift, a selection collapses to its left edge instead of moving one further.
  if (!extend && hasSelection()) {
    Place(selectionStart(), false);
    return;
  }
  Place(StepLeft(text_, caret_), extend);
}

void TextInput::MoveRight(bool extend) {
  if (!extend && hasSelection()) {
    Place(selectionEnd(), false);
    return;
  }
  Place(StepRight(text_, caret_), extend);
}

void TextInput::MoveHome(bool extend) { Place(0, extend); }

void TextInput::MoveEnd(bool extend) { Place(text_.size(), extend); }

void TextInput::MoveTo(size_t index, bool extend) { Place(index, extend); }

void TextInput::SelectAll() {
  anchor_ = 0;
  caret_ = text_.size();
}

void TextInput::Clear() {
  text_.clear();
  caret_ = 0;
  anchor_ = 0;
}

}  // namespace agenda
