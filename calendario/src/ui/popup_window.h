#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <chrono>

#include "core/config.h"
#include "data/store.h"
#include "ui/accessibility.h"
#include "ui/app_layout.h"
#include "ui/app_view.h"
#include "ui/paint.h"
#include "ui/popup_view.h"
#include "ui/spring.h"
#include "ui/theme.h"
#include "ui/vsync.h"

namespace agenda {

namespace sync {
class GoogleSync;
}

// One frame of the expansion, posted by the FrameClock once per composed frame.
inline constexpr UINT kFrameMessage = WM_APP + 4;

// The popup window: WS_POPUP with no redirection bitmap, its content composed by
// DirectComposition over a premultiplied swap chain so the acrylic DWM paints behind it shows
// through. It is created hidden when the app starts and reused for the life of the process,
// so reacting to the hotkey is a SetWindowPos plus an animation, never device creation.
//
// It is also the expanded app. A click on the month grows this same window into it on a spring
// -- phase 6 -- and the code for that half lives in popup_window_app.cpp, as more members of
// this class: two windows would be a cut between them, which is the one thing the expansion
// must not look like.
class PopupWindow final : public A11ySource {
 public:
  struct Timing {
    UINT openMs = 160;
    UINT closeMs = 120;
  };

  // `monitor` pins the popup to one monitor (--monitor); nullptr opens it on the one the mouse
  // is on. `themeOverride` is "dark" or "light" from --theme; empty follows the system.
  bool Create(HINSTANCE instance, HMONITOR monitor, Timing timing,
              std::wstring_view themeOverride);

  // Forces the panel size in DIP instead of working it out from the monitor. Only --panel
  // sets this, so a size can be judged on a screen that would not produce it.
  void SetPanelOverride(D2D1_SIZE_F panel) { panelOverride_ = panel; }

  // Where the popup reads its day from and sends its writes. Owned by the app, not by the
  // window, because phase 5 hangs the Google sync off the same store.
  void SetStore(Store* store) { store_ = store; }
  // Declared and not included: the popup asks it two questions and never looks inside.
  void SetSync(sync::GoogleSync* sync) { sync_ = sync; }
  // The settings the popup reads -- theme, language through the global, the length of a new
  // event -- owned by the app and changed by the settings window.
  void SetPreferences(const Preferences* prefs) { prefs_ = prefs; }
  // Ctrl+, from the popup or the app; main knows where the settings window lives.
  void SetOpenSettings(std::function<void()> open) { openSettings_ = std::move(open); }
  // The settings window changed something: the theme is resolved again and everything redrawn
  // in the language that is current now.
  void PreferencesChanged();

  void Toggle();
  void Show();
  void Hide();
  // Opens -- or keeps open -- on `day`: where a click on a reminder takes you.
  void ShowDay(Date day);
  // Opens the app on `day` with the event `uid` in the detail panel: "Abrir" in the island.
  void ShowEvent(const std::wstring& uid, Date day);

  // The expansion, and back. Both can be called halfway through the other: the spring simply
  // turns round from wherever it is.
  void Expand(Date day);
  void Contract();

  HWND hwnd() const { return hwnd_; }

  // --- UI Automation (popup_window_access.cpp) ---
  std::vector<A11yNode> A11yNodes() override;
  void A11yInvoke(int id) override;
  void A11yToggle(int id) override;
  void A11ySelect(int id) override;
  void A11ySetValue(int id, const std::wstring& value) override;
  void A11yFocus(int id) override;

 private:
  template <class T>
  using ComPtr = Microsoft::WRL::ComPtr<T>;

  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  LRESULT Handle(UINT message, WPARAM wparam, LPARAM lparam);

  void ApplyDwmAttributes();
  // Puts the system material and DWM's border on, or takes them off for the close fade.
  void Backdrop(bool on);
  bool CreateDevices();
  void Place(const RECT& work);
  // --- Moving between monitors (phase 7) -----------------------------------------------------
  // The app is dragged by the empty part of its top row, like any title bar.
  bool IsCaption(float x, float y) const;
  // Dragged onto a monitor with another scale, or the scale of this one changed: the window
  // keeps its size in DIP and everything is drawn again at the new DPI.
  void OnDpiChanged(UINT dpi, const RECT& suggested);
  // Where the popup folds back to after the app was moved: the corner of the monitor it is on
  // now, not the one it opened on.
  void AdoptPosition();
  // The scale of the monitor changed with the window on it, which Windows does not report to
  // this window: compared by hand whenever a setting or the displays change.
  void FollowMonitorDpi();
  void Resize(SIZE size);
  // Hidden for good: the swap chain shrinks back to the popup, the size it idles at.
  void Shelve();
  void Render();
  void Animate(bool opening);
  void Rest();  // hidden state: fully transparent and slid down, ready to open
  float SlidePx() const;
  bool AnimationsEnabled() const;
  // --theme first, which is how a snapshot or a test pins it; the settings after that.
  std::wstring_view ThemeChoice() const {
    return !themeOverride_.empty() || prefs_ == nullptr ? std::wstring_view(themeOverride_)
                                                        : std::wstring_view(prefs_->theme);
  }
  int DefaultMinutes() const { return prefs_ != nullptr ? prefs_->durationMin : 60; }

  // There is no WM_PAINT on a window with no redirection bitmap, so anything that changes the
  // model redraws by hand.
  void Invalidate();
  // Rereads the day and the month dots. Called when the day or the month changes and when the
  // worker says it finished something; never in the middle of drawing.
  void Reload();
  // Enter: turns whatever the preview understood into a row. Optimistic -- the card is on
  // screen before the worker has written anything.
  bool CreateFromInput();
  // The preview is asking a.m. or p.m. (nlp::ParsedInput::otherMinute), and the answer.
  bool AskingMeridiem() const;
  void FlipMeridiem();
  // Ctrl+Z while the notice is up: takes back a creation, a deletion or a task made into an
  // event, whichever the notice is about.
  void Undo();
  // The deletions waiting out their five seconds of undo, done for real. Called when the
  // notice goes, when another one takes its place, and when the window hides.
  void FlushDeletes();
  void DropPending(std::vector<DayItem>& items) const;
  // Where typed text goes: the detail field with the keyboard, or the capsule.
  TextInput& FocusedText();
  void ShowToast(std::wstring text);
  void HideToast();
  bool ToggleCardAt(float x, float y);
  // Ticks or unticks the day's task at `index`, with the line drawing itself across it.
  void ToggleDone(size_t index);
  // The sidebar's switch and the tray's tick box, shared by the mouse, the keys and UIA.
  void ToggleCalendar(int index);
  void ToggleUndated(int index);
  bool Tick(float ms);  // advances hover, focus and the month slide; true while still moving
  void StartTicking();
  void RestartCaret();

  // --- The expanded app (popup_window_app.cpp) -------------------------------------------
  enum class Mode { Popup, Morphing, App };
  bool InApp() const { return mode_ != Mode::Popup; }
  // The popup's layout with the capsule wherever the expansion has it now: what the input's
  // drawing and hit testing read, in both modes.
  PanelLayout ActiveLayout() const;
  D2D1_SIZE_F SizeDip() const;
  void StepMorph();
  void ApplyMorph();
  void FinishMorph();
  void ReloadApp();
  void Relayout();
  void SetView(AppView view);
  void MovePeriod(int direction);
  void AddToApp(const DayItem& item, std::optional<Date> day);
  void ScheduleNowTick();
  bool OnAppKeyDown(WPARAM key);
  bool OnAppLeftDown(float x, float y);
  bool OnAppMouseMove(float x, float y);
  void OnWheel(int delta);
  bool TickApp(float step);

  // --- Dragging on the timeline, the detail panel, deleting (phase 6b) ----------------------
  bool BeginDrag(float x, float y);
  void UpdateDrag(float x, float y);
  void EndDrag();
  void CancelDrag();
  void CommitMove(const Ghost& ghost, Date origin);
  void ConvertTask(const DayItem& task, int column, int start);
  bool SelectAllDayAt(float x, float y);
  void OpenDetail(const EventDetail& event, int focus);
  void OpenDetailFor(const std::wstring& uid);
  void CloseDetail();
  void FillDetailFields();
  void FocusDetail(int field);
  bool CommitField(int field);
  void SaveDetail(const EventDetail& event, unsigned edits);
  bool OnDetailKeyDown(WPARAM key);
  bool OnDetailLeftDown(float x, float y);
  bool DetailCursor(float x, float y, LPCWSTR& cursor);
  void AskDelete(const std::wstring& uid);
  void ConfirmDelete();

  D2D1_POINT_2F ToDip(LPARAM lparam) const;
  int HitDay(float x, float y) const;
  void OnMouseMove(float x, float y);
  void OnLeftDown(float x, float y);
  bool OnKeyDown(WPARAM key);
  void OnImeComposition(LPARAM flags);
  void PlaceCandidateWindow();
  void FocusInput(bool focused);
  void SelectDay(Date date);
  void ChangeMonth(int direction);
  void CopySelection(bool cut);
  void Paste();

  // --- The keyboard beyond the capsule (popup_window_access.cpp) ---------------------------
  // Where the keyboard is when it is not in the capsule. The capsule keeps its own flag,
  // inputFocused_, because half of the popup already reads it.
  enum class Zone { Grid, List, Events, Detail, Calendars, Tasks };
  bool OnTab(bool back);
  void EnterZone(Zone zone, bool back);
  bool OnZoneKey(WPARAM key);
  bool OnDetailControlKey(WPARAM key);
  void FocusDetailStop(int stop);
  void MoveDetailStop(bool back);
  int DetailStop() const;
  std::vector<const DayItem*> ShownEvents() const;
  void SelectAdjacentEvent(int direction);
  void RevealSelected();
  void NudgeSelected(int minutes, int days, int endMinutes);
  void OpenFocusedCard();
  void ToggleFocusedCard();
  void UpdateRing();
  int TodayColumnOf(const std::wstring& uid) const;

  // Moves the red line on the turn of every minute while the app is open.
  static constexpr UINT_PTR kNowTimer = 5;

  HWND hwnd_ = nullptr;
  HMONITOR monitor_ = nullptr;
  Zone zone_ = Zone::Grid;
  int listFocus_ = 0;           // the card, the calendar or the tray task the keyboard is on
  bool focusVisible_ = false;   // the keyboard has been used since the last click
  bool eatSpace_ = false;       // a Space that was a command, not a character to type
  Accessibility a11y_;
  Timing timing_{};
  std::wstring themeOverride_;
  D2D1_SIZE_F panelOverride_{};
  UINT dpi_ = USER_DEFAULT_SCREEN_DPI;
  SIZE size_{};    // the window, which is what everything is laid out in
  SIZE buffer_{};  // the swap chain: big enough for the app, so growing never resizes it
  bool visible_ = false;
  bool acrylic_ = false;   // the panel is drawn translucent over the material right now
  bool backdrop_ = false;  // this Windows has a system backdrop at all
  bool placing_ = false;  // Place is moving the window and measures the DPI itself

  Fonts fonts_;
  Theme theme_ = DarkTheme();
  PanelLayout layout_ = BaseLayout();
  PopupModel model_;

  // What the pointer is over and where the keyboard is going. The model carries the fades;
  // these carry the targets they are walking to.
  int hoverDay_ = -1;
  bool hoverPrev_ = false;
  bool hoverNext_ = false;
  bool tracking_ = false;
  bool ticking_ = false;
  bool inputFocused_ = true;
  bool caretVisible_ = true;
  bool toastOn_ = false;
  bool strikeOn_ = false;

  // What Ctrl+Z would take back, and the line it would put back in the capsule. Empty once the
  // notice is gone: undo is offered while it is on screen and not a second longer.
  enum class UndoKind { Created, Deleted, Converted };
  struct Undone {
    std::wstring uid;
    bool isTask = false;
    std::wstring typed;
    UndoKind kind = UndoKind::Created;
    std::wstring other;  // Converted: the task that became the event `uid`
  };
  // Deleted, as far as the screen is concerned, and not yet in the store: undo only has to
  // stop hiding them, so nothing Google holds for the event -- guests, reminders -- is lost.
  struct Pending {
    std::wstring uid;
    bool isTask = false;
  };
  std::vector<Pending> pendingDelete_;
  std::wstring confirmUid_;

  enum class DragKind { None, Create, Move, Resize, Task };
  struct DragState {
    DragKind kind = DragKind::None;
    D2D1_POINT_2F down{};
    bool moved = false;
    int column = 0;       // the column the pointer went down in
    int anchor = 0;       // Create: the minute it went down on
    float grab = 0.0f;    // Move: minutes between the pointer and the start of the block
    int length = 0;       // Move: how long the block is
    DayItem item;         // what is being dragged
    Date origin{};        // Move and Resize: the day it was on
  };
  DragState drag_;
  std::optional<Undone> undo_;
  Store* store_ = nullptr;
  sync::GoogleSync* sync_ = nullptr;
  const Preferences* prefs_ = nullptr;
  std::function<void()> openSettings_;
  ULONGLONG lastTick_ = 0;
  std::wstring parsed_;  // the text the preview in the model was built from

  // The expansion. `spring_.x` is how far it has got, 0 the popup and 1 the app; `goal_` is
  // where it is going. The two rectangles are in physical pixels on this monitor.
  Mode mode_ = Mode::Popup;
  Spring spring_;
  float goal_ = 0.0f;
  RECT popupRect_{};
  RECT appRect_{};
  LARGE_INTEGER lastFrame_{};
  FrameClock clock_;
  AppModel app_;
  AppLayout appLayout_;
  int hoverTab_ = -1;
  int hoverCalendar_ = -1;
  bool hoverCollapse_ = false;
  bool hoverPeriodPrev_ = false;
  bool hoverPeriodNext_ = false;

  ComPtr<ID3D11Device> d3d_;
  ComPtr<IDXGISwapChain1> swapChain_;
  ComPtr<ID2D1DeviceContext> dc_;
  ComPtr<IDCompositionDesktopDevice> composition_;
  ComPtr<IDCompositionTarget> target_;
  ComPtr<IDCompositionVisual2> visual_;
  ComPtr<IDCompositionVisual3> visual3_;
};

}  // namespace agenda
