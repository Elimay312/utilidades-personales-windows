#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace agenda {

// The editable text behind the capsule at the bottom of the popup: a string, a caret and a
// selection anchor. There is no Direct2D and no Win32 in here, which is what lets the tests
// drive it directly and keeps the window free to be the only place that talks to the
// clipboard and the IME.
class TextInput {
 public:
  const std::wstring& text() const { return text_; }
  size_t caret() const { return caret_; }
  size_t anchor() const { return anchor_; }
  bool empty() const { return text_.empty(); }
  bool hasSelection() const { return caret_ != anchor_; }
  size_t selectionStart() const { return (std::min)(caret_, anchor_); }
  size_t selectionEnd() const { return (std::max)(caret_, anchor_); }
  std::wstring_view selectedText() const;

  // Replaces the selection, if any. Control characters are dropped: this is one line of text,
  // so a pasted paragraph arrives as a paragraph without its newlines.
  void Insert(std::wstring_view inserted);
  bool Backspace();
  bool DeleteForward();
  bool DeleteSelection();

  void MoveLeft(bool extend);
  void MoveRight(bool extend);
  void MoveHome(bool extend);
  void MoveEnd(bool extend);
  void MoveTo(size_t index, bool extend);
  void SelectAll();
  void Clear();

  // The notes in the detail panel are the one field with more than one line. With this on, a
  // newline survives Insert (a CR LF arrives as one LF) instead of being dropped with the rest
  // of the control characters.
  void AllowNewlines(bool allow) { newlines_ = allow; }

 private:
  void Place(size_t index, bool extend);

  std::wstring text_;
  size_t caret_ = 0;
  size_t anchor_ = 0;
  bool newlines_ = false;
};

}  // namespace agenda
