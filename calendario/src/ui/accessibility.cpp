#include "ui/accessibility.h"

#include <wrl/implements.h>

#include <optional>

#include "ui/layout.h"

namespace agenda {
namespace {

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

std::optional<A11yNode> FindNode(const std::vector<A11yNode>& nodes, int id) {
  for (const A11yNode& node : nodes) {
    if (node.id == id) return node;
  }
  return std::nullopt;
}

int Depth(const std::vector<A11yNode>& nodes, const A11yNode& node) {
  int depth = 0;
  for (int parent = node.parent; parent != 0 && depth < 8; ++depth) {
    const std::optional<A11yNode> up = FindNode(nodes, parent);
    parent = up ? up->parent : 0;
  }
  return depth;
}

void SetBool(VARIANT* out, bool value) {
  out->vt = VT_BOOL;
  out->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
}

void SetString(VARIANT* out, const std::wstring& value) {
  out->vt = VT_BSTR;
  out->bstrVal = SysAllocString(value.c_str());
}

void SetInt(VARIANT* out, int value) {
  out->vt = VT_I4;
  out->lVal = value;
}

}  // namespace

// The window: the fragment root, whose own properties come from the HWND through the host
// provider. It holds the source and hands out one Element per node when UIA asks.
class Accessibility::Root
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IRawElementProviderSimple,
                          IRawElementProviderFragment, IRawElementProviderFragmentRoot> {
 public:
  Root(HWND hwnd, A11ySource* source) : hwnd_(hwnd), source_(source) {}

  void Disconnect() { source_ = nullptr; }
  bool Live() const { return source_ != nullptr && hwnd_ != nullptr; }
  HWND hwnd() const { return hwnd_; }
  A11ySource* source() const { return source_; }
  std::vector<A11yNode> Nodes() const { return Live() ? source_->A11yNodes() : std::vector<A11yNode>{}; }

  // An Element for `id`, or null when the id is 0 (the window) or no longer on screen.
  ComPtr<IRawElementProviderFragment> Fragment(int id);

  UiaRect ToScreen(const D2D1_RECT_F& rect) const {
    const float scale = static_cast<float>(MonitorDpi(hwnd_)) / USER_DEFAULT_SCREEN_DPI;
    POINT origin{0, 0};
    ClientToScreen(hwnd_, &origin);
    return UiaRect{origin.x + rect.left * scale, origin.y + rect.top * scale,
                   (rect.right - rect.left) * scale, (rect.bottom - rect.top) * scale};
  }

  // --- IRawElementProviderSimple ---
  IFACEMETHODIMP get_ProviderOptions(ProviderOptions* out) override {
    *out = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
  }
  IFACEMETHODIMP GetPatternProvider(PATTERNID, IUnknown** out) override {
    *out = nullptr;
    return S_OK;
  }
  IFACEMETHODIMP GetPropertyValue(PROPERTYID, VARIANT* out) override {
    out->vt = VT_EMPTY;  // the host provider already knows the window's name and type
    return S_OK;
  }
  IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** out) override {
    if (!Live()) {
      *out = nullptr;
      return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return UiaHostProviderFromHwnd(hwnd_, out);
  }

  // --- IRawElementProviderFragment ---
  IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
    *out = nullptr;
    if (!Live()) return UIA_E_ELEMENTNOTAVAILABLE;
    if (direction != NavigateDirection_FirstChild && direction != NavigateDirection_LastChild) {
      return S_OK;  // the window's parent and siblings belong to the host provider
    }
    const std::vector<A11yNode> nodes = Nodes();
    int found = 0;
    for (const A11yNode& node : nodes) {
      if (node.parent != 0) continue;
      found = node.id;
      if (direction == NavigateDirection_FirstChild) break;
    }
    if (found != 0) *out = Fragment(found).Detach();
    return S_OK;
  }
  IFACEMETHODIMP GetRuntimeId(SAFEARRAY** out) override {
    *out = nullptr;
    return S_OK;
  }
  IFACEMETHODIMP get_BoundingRectangle(UiaRect* out) override {
    *out = UiaRect{};
    return S_OK;
  }
  IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
    *out = nullptr;
    return S_OK;
  }
  IFACEMETHODIMP SetFocus() override { return S_OK; }
  IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
    AddRef();
    *out = this;
    return S_OK;
  }

  // --- IRawElementProviderFragmentRoot ---
  IFACEMETHODIMP ElementProviderFromPoint(double x, double y,
                                          IRawElementProviderFragment** out) override {
    *out = nullptr;
    if (!Live()) return UIA_E_ELEMENTNOTAVAILABLE;
    POINT point{static_cast<LONG>(x), static_cast<LONG>(y)};
    ScreenToClient(hwnd_, &point);
    const float scale = USER_DEFAULT_SCREEN_DPI / static_cast<float>(MonitorDpi(hwnd_));
    const float dx = static_cast<float>(point.x) * scale;
    const float dy = static_cast<float>(point.y) * scale;
    // The deepest node under the point: a day before the grid that holds it.
    const std::vector<A11yNode> nodes = Nodes();
    int best = 0;
    int bestDepth = -1;
    for (const A11yNode& node : nodes) {
      if (dx < node.rect.left || dx >= node.rect.right || dy < node.rect.top ||
          dy >= node.rect.bottom) {
        continue;
      }
      const int depth = Depth(nodes, node);
      if (depth >= bestDepth) {
        best = node.id;
        bestDepth = depth;
      }
    }
    if (best != 0) *out = Fragment(best).Detach();
    return S_OK;
  }
  IFACEMETHODIMP GetFocus(IRawElementProviderFragment** out) override {
    *out = nullptr;
    if (!Live()) return UIA_E_ELEMENTNOTAVAILABLE;
    for (const A11yNode& node : Nodes()) {
      if (node.focused) {
        *out = Fragment(node.id).Detach();
        break;
      }
    }
    return S_OK;
  }

 private:
  HWND hwnd_;
  A11ySource* source_;
};

namespace {

// One node. It keeps only its id and asks the root for the rest on every call, so it can never
// describe something that has changed since it was handed out.
class Element
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IRawElementProviderSimple,
                          IRawElementProviderFragment, IValueProvider, IInvokeProvider,
                          IToggleProvider, ISelectionItemProvider, IGridProvider,
                          IGridItemProvider> {
 public:
  Element(Accessibility::Root* root, int id) : root_(root), id_(id) {}

  // --- IRawElementProviderSimple ---
  IFACEMETHODIMP get_ProviderOptions(ProviderOptions* out) override {
    *out = ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading;
    return S_OK;
  }

  IFACEMETHODIMP GetPatternProvider(PATTERNID pattern, IUnknown** out) override {
    *out = nullptr;
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    bool has = false;
    switch (pattern) {
      case UIA_ValuePatternId:
        has = node->hasValue;
        break;
      case UIA_InvokePatternId:
        has = node->invokable;
        break;
      case UIA_TogglePatternId:
        has = node->toggle >= 0;
        break;
      case UIA_SelectionItemPatternId:
        has = node->selected >= 0;
        break;
      case UIA_GridPatternId:
        has = node->rows > 0 && node->columns > 0;
        break;
      case UIA_GridItemPatternId:
        has = node->row >= 0 && node->column >= 0;
        break;
      default:
        break;
    }
    if (has) {
      AddRef();
      *out = static_cast<IRawElementProviderSimple*>(this);
    }
    return S_OK;
  }

  IFACEMETHODIMP GetPropertyValue(PROPERTYID property, VARIANT* out) override {
    out->vt = VT_EMPTY;
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    switch (property) {
      case UIA_ControlTypePropertyId:
        SetInt(out, node->type);
        break;
      case UIA_NamePropertyId:
        SetString(out, node->name);
        break;
      case UIA_AutomationIdPropertyId:
        SetString(out, L"agenda-" + std::to_wstring(node->id));
        break;
      case UIA_IsKeyboardFocusablePropertyId:
        SetBool(out, node->focusable);
        break;
      case UIA_HasKeyboardFocusPropertyId:
        SetBool(out, node->focused);
        break;
      case UIA_IsEnabledPropertyId:
        SetBool(out, node->enabled);
        break;
      case UIA_IsControlElementPropertyId:
      case UIA_IsContentElementPropertyId:
        SetBool(out, true);
        break;
      case UIA_ProviderDescriptionPropertyId:
        SetString(out, L"Agenda");
        break;
      default:
        break;
    }
    return S_OK;
  }

  IFACEMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** out) override {
    *out = nullptr;
    return S_OK;
  }

  // --- IRawElementProviderFragment ---
  IFACEMETHODIMP Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
    *out = nullptr;
    if (!root_->Live()) return UIA_E_ELEMENTNOTAVAILABLE;
    const std::vector<A11yNode> nodes = root_->Nodes();
    const std::optional<A11yNode> self = FindNode(nodes, id_);
    if (!self) return UIA_E_ELEMENTNOTAVAILABLE;

    int found = 0;
    switch (direction) {
      case NavigateDirection_Parent:
        if (self->parent == 0) {
          root_->AddRef();
          *out = root_.Get();
          return S_OK;
        }
        found = self->parent;
        break;
      case NavigateDirection_FirstChild:
      case NavigateDirection_LastChild:
        for (const A11yNode& node : nodes) {
          if (node.parent != id_) continue;
          found = node.id;
          if (direction == NavigateDirection_FirstChild) break;
        }
        break;
      case NavigateDirection_NextSibling:
      case NavigateDirection_PreviousSibling: {
        int previous = 0;
        bool seen = false;
        for (const A11yNode& node : nodes) {
          if (node.parent != self->parent) continue;
          if (seen) {
            found = node.id;
            break;
          }
          if (node.id == id_) {
            if (direction == NavigateDirection_PreviousSibling) {
              found = previous;
              break;
            }
            seen = true;
          }
          previous = node.id;
        }
        break;
      }
    }
    if (found != 0) *out = root_->Fragment(found).Detach();
    return S_OK;
  }

  IFACEMETHODIMP GetRuntimeId(SAFEARRAY** out) override {
    const int ids[2] = {UiaAppendRuntimeId, id_};
    *out = SafeArrayCreateVector(VT_I4, 0, 2);
    if (*out == nullptr) return E_OUTOFMEMORY;
    for (LONG i = 0; i < 2; ++i) SafeArrayPutElement(*out, &i, const_cast<int*>(&ids[i]));
    return S_OK;
  }

  IFACEMETHODIMP get_BoundingRectangle(UiaRect* out) override {
    *out = UiaRect{};
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = root_->ToScreen(node->rect);
    return S_OK;
  }

  IFACEMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
    *out = nullptr;
    return S_OK;
  }

  IFACEMETHODIMP SetFocus() override {
    if (!root_->Live()) return UIA_E_ELEMENTNOTAVAILABLE;
    root_->source()->A11yFocus(id_);
    return S_OK;
  }

  IFACEMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
    root_->AddRef();
    *out = root_.Get();
    return S_OK;
  }

  // --- IValueProvider ---
  IFACEMETHODIMP SetValue(LPCWSTR text) override {
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    if (node->readOnly) return UIA_E_NOTSUPPORTED;
    root_->source()->A11ySetValue(id_, text != nullptr ? text : L"");
    return S_OK;
  }
  IFACEMETHODIMP get_Value(BSTR* out) override {
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = SysAllocString(node->value.c_str());
    return S_OK;
  }
  IFACEMETHODIMP get_IsReadOnly(BOOL* out) override {
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = node->readOnly;
    return S_OK;
  }

  // --- IInvokeProvider ---
  IFACEMETHODIMP Invoke() override {
    if (!Node()) return UIA_E_ELEMENTNOTAVAILABLE;
    root_->source()->A11yInvoke(id_);
    return S_OK;
  }

  // --- IToggleProvider ---
  IFACEMETHODIMP Toggle() override {
    if (!Node()) return UIA_E_ELEMENTNOTAVAILABLE;
    root_->source()->A11yToggle(id_);
    return S_OK;
  }
  IFACEMETHODIMP get_ToggleState(ToggleState* out) override {
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = node->toggle > 0 ? ToggleState_On : ToggleState_Off;
    return S_OK;
  }

  // --- ISelectionItemProvider ---
  IFACEMETHODIMP Select() override {
    if (!Node()) return UIA_E_ELEMENTNOTAVAILABLE;
    root_->source()->A11ySelect(id_);
    return S_OK;
  }
  IFACEMETHODIMP AddToSelection() override { return Select(); }
  IFACEMETHODIMP RemoveFromSelection() override { return UIA_E_INVALIDOPERATION; }
  IFACEMETHODIMP get_IsSelected(BOOL* out) override {
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = node->selected > 0;
    return S_OK;
  }
  IFACEMETHODIMP get_SelectionContainer(IRawElementProviderSimple** out) override {
    return Parent(out);
  }

  // --- IGridProvider ---
  IFACEMETHODIMP GetItem(int row, int column, IRawElementProviderSimple** out) override {
    *out = nullptr;
    if (!root_->Live()) return UIA_E_ELEMENTNOTAVAILABLE;
    for (const A11yNode& node : root_->Nodes()) {
      if (node.parent == id_ && node.row == row && node.column == column) {
        ComPtr<IRawElementProviderFragment> item = root_->Fragment(node.id);
        return item.CopyTo(IID_PPV_ARGS(out));
      }
    }
    return E_INVALIDARG;
  }
  IFACEMETHODIMP get_RowCount(int* out) override {
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = node->rows;
    return S_OK;
  }
  IFACEMETHODIMP get_ColumnCount(int* out) override {
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = node->columns;
    return S_OK;
  }

  // --- IGridItemProvider ---
  IFACEMETHODIMP get_Row(int* out) override {
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = node->row;
    return S_OK;
  }
  IFACEMETHODIMP get_Column(int* out) override {
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    *out = node->column;
    return S_OK;
  }
  IFACEMETHODIMP get_RowSpan(int* out) override {
    *out = 1;
    return S_OK;
  }
  IFACEMETHODIMP get_ColumnSpan(int* out) override {
    *out = 1;
    return S_OK;
  }
  IFACEMETHODIMP get_ContainingGrid(IRawElementProviderSimple** out) override {
    return Parent(out);
  }

 private:
  std::optional<A11yNode> Node() const {
    if (!root_->Live()) return std::nullopt;
    return FindNode(root_->Nodes(), id_);
  }

  HRESULT Parent(IRawElementProviderSimple** out) {
    *out = nullptr;
    const std::optional<A11yNode> node = Node();
    if (!node) return UIA_E_ELEMENTNOTAVAILABLE;
    if (node->parent == 0) return root_.CopyTo(out);
    return root_->Fragment(node->parent).CopyTo(IID_PPV_ARGS(out));
  }

  ComPtr<Accessibility::Root> root_;
  int id_;
};

}  // namespace

ComPtr<IRawElementProviderFragment> Accessibility::Root::Fragment(int id) {
  ComPtr<IRawElementProviderFragment> out;
  if (id == 0 || !Live()) return out;
  if (!FindNode(Nodes(), id)) return out;
  Make<Element>(this, id).As(&out);
  return out;
}

Accessibility::Accessibility() = default;
Accessibility::~Accessibility() { Detach(); }

void Accessibility::Attach(HWND hwnd, A11ySource* source) {
  root_ = Make<Root>(hwnd, source);
}

void Accessibility::Detach() {
  if (!root_) return;
  const HWND hwnd = root_->hwnd();
  root_->Disconnect();
  // Tells UIA the window's provider is gone, so clients holding elements get "not available"
  // instead of calls into a window that no longer exists.
  UiaReturnRawElementProvider(hwnd, 0, 0, nullptr);
  UiaDisconnectProvider(root_.Get());
  root_.Reset();
}

bool Accessibility::OnGetObject(WPARAM wparam, LPARAM lparam, LRESULT& result) {
  if (!root_ || static_cast<long>(lparam) != static_cast<long>(UiaRootObjectId)) return false;
  result = UiaReturnRawElementProvider(root_->hwnd(), wparam, lparam, root_.Get());
  return true;
}

void Accessibility::Announce(const std::wstring& text) {
  if (!root_ || !root_->Live() || text.empty() || !UiaClientsAreListening()) return;
  const BSTR display = SysAllocString(text.c_str());
  const BSTR activity = SysAllocString(L"agenda");
  UiaRaiseNotificationEvent(root_.Get(), NotificationKind_ActionCompleted,
                            NotificationProcessing_ImportantMostRecent, display, activity);
  SysFreeString(display);
  SysFreeString(activity);
}

void Accessibility::Changed() {
  if (!root_ || !root_->Live() || !UiaClientsAreListening()) return;
  const std::vector<A11yNode> nodes = root_->Nodes();

  int focus = 0;
  std::wstring focusName;
  int selected = 0;
  for (const A11yNode& node : nodes) {
    if (node.focused) {
      focus = node.id;
      focusName = node.name;
    }
    // The grid's selected day is the one that has to be said when the arrows move it.
    if (node.selected > 0 && node.row >= 0) selected = node.id;
  }

  if (nodes.size() != lastCount_) {
    lastCount_ = nodes.size();
    int ids[1] = {0};
    UiaRaiseStructureChangedEvent(root_.Get(), StructureChangeType_ChildrenInvalidated, ids, 0);
  }
  // A focus that stays on the same id but now names something else -- the list's second card
  // after the list was reread -- is a move too, as far as somebody listening is concerned.
  if (focus != 0 && (focus != lastFocus_ || focusName != lastFocusName_)) {
    if (ComPtr<IRawElementProviderFragment> element = root_->Fragment(focus)) {
      ComPtr<IRawElementProviderSimple> simple;
      if (SUCCEEDED(element.As(&simple))) {
        UiaRaiseAutomationEvent(simple.Get(), UIA_AutomationFocusChangedEventId);
      }
    }
  }
  if (selected != 0 && selected != lastSelected_) {
    if (ComPtr<IRawElementProviderFragment> element = root_->Fragment(selected)) {
      ComPtr<IRawElementProviderSimple> simple;
      if (SUCCEEDED(element.As(&simple))) {
        UiaRaiseAutomationEvent(simple.Get(), UIA_SelectionItem_ElementSelectedEventId);
      }
    }
  }
  lastFocus_ = focus;
  lastFocusName_ = focusName;
  lastSelected_ = selected;
}

}  // namespace agenda
