#pragma once

// UI Automation for windows that draw everything themselves.
//
// Agenda paints with Direct2D, so to Narrator the popup is one blank rectangle: there is no
// HWND per control for UIA to find. This is the bridge. A window describes what is on screen
// as a flat list of nodes -- the capsule, the month grid and its days, the list of the day --
// and the providers here answer UIA from that list, asking for it again on every call. There is
// no tree of objects to keep in step with the model: the model IS the tree, read when asked.

#include <windows.h>

#include <ole2.h>  // UIAutomation.h needs COM, and WIN32_LEAN_AND_MEAN leaves it out
#include <UIAutomation.h>
#include <d2d1.h>
#include <wrl/client.h>

#include <string>
#include <vector>

namespace agenda {

struct A11yNode {
  int id = 0;      // stable for as long as the thing it names is on screen; above zero
  int parent = 0;  // 0 is the window itself; parents come before their children in the list
  CONTROLTYPEID type = UIA_CustomControlTypeId;
  std::wstring name;
  std::wstring value;  // an Edit's text; `hasValue` says whether there is a Value pattern
  bool hasValue = false;
  bool readOnly = true;
  D2D1_RECT_F rect{};  // in DIP, in the window's client area
  bool focusable = false;
  bool focused = false;
  bool enabled = true;
  bool invokable = false;
  int toggle = -1;    // -1 no Toggle pattern, 0 off, 1 on
  int selected = -1;  // -1 no SelectionItem pattern, 0 no, 1 yes
  int row = -1;       // a GridItem when both are set
  int column = -1;
  int rows = 0;  // a Grid when both are set
  int columns = 0;
};

// What a window implements to be read. Every action lands back in the window, which does
// exactly what the mouse or the keyboard would have done.
class A11ySource {
 public:
  virtual std::vector<A11yNode> A11yNodes() = 0;
  virtual void A11yInvoke(int id) = 0;
  virtual void A11yToggle(int id) { A11yInvoke(id); }
  virtual void A11ySelect(int id) { A11yInvoke(id); }
  virtual void A11ySetValue(int id, const std::wstring& value) = 0;
  virtual void A11yFocus(int id) = 0;

 protected:
  ~A11ySource() = default;
};

class Accessibility {
 public:
  Accessibility();
  ~Accessibility();
  Accessibility(const Accessibility&) = delete;
  Accessibility& operator=(const Accessibility&) = delete;

  void Attach(HWND hwnd, A11ySource* source);
  // WM_DESTROY. After this every provider UIA still holds answers "not available".
  void Detach();

  // WM_GETOBJECT. True when it answered, with the result in `result`.
  bool OnGetObject(WPARAM wparam, LPARAM lparam, LRESULT& result);

  // Said out loud to whoever listens; nothing at all when nobody does. `Changed` compares the
  // focus and the selection with the last time it was called and raises only what moved, so a
  // window can call it after every change without flooding a screen reader.
  void Changed();

  // Something happened that is not a move of the focus -- "Creado · Deshacer", "¿Borrar...?" --
  // and a screen reader should say it once, now.
  void Announce(const std::wstring& text);

  // The provider behind the window. Public only so accessibility.cpp can name it.
  class Root;

 private:
  Microsoft::WRL::ComPtr<Root> root_;
  int lastFocus_ = 0;
  std::wstring lastFocusName_;
  int lastSelected_ = 0;
  size_t lastCount_ = 0;
};

}  // namespace agenda
