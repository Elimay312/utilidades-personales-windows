#include "ui/snapshot.h"

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <system_error>

#include "core/dates.h"
#include "core/hr.h"
#include "core/log.h"
#include "core/text.h"
#include "ui/app_layout.h"
#include "ui/app_view.h"
#include "ui/layout.h"
#include "ui/paint.h"
#include "ui/popup_view.h"
#include "ui/fields.h"
#include "ui/sample_data.h"
#include "ui/settings_window.h"
#include "ui/theme.h"

using Microsoft::WRL::ComPtr;

namespace agenda {
namespace {

// Offscreen there is no desktop to blur, so a flat neutral grey stands in for the acrylic.
// Without it the translucent panel would be judged against whatever an image viewer puts
// behind transparency, which is usually white and lies about the design. The light theme gets
// a lighter one, because a pale panel at 85% over a dark grey is not what the user will see.
constexpr D2D1_COLOR_F kDarkBackdrop{0.27f, 0.27f, 0.30f, 1.0f};
constexpr D2D1_COLOR_F kLightBackdrop{0.80f, 0.80f, 0.84f, 1.0f};
constexpr int kMarginDip = 24;  // grey visible around the panel, so the corners can be judged

// A fixed day, so docs/img only changes when the design does and not when the calendar turns.
constexpr Date kSnapshotToday{std::chrono::year{2026}, std::chrono::September,
                              std::chrono::day{22}};
constexpr int kSnapshotMinute = 10 * 60;  // and a fixed hour, for the same reason

// The work area the transition is drawn over: the one the app's design size is 80 % of.
constexpr RECT kSnapshotWork{0, 0, 1920, 1032};

AppView ViewFor(std::wstring_view view) {
  if (view == L"app-semana" || view == L"app-detalle" || view == L"app-arrastre" ||
      view == L"app-borrar" || view == L"app-repeticion" || view == L"app-buscar") {
    return AppView::Week;
  }
  if (view == L"app-mes") return AppView::Month;
  return AppView::Day;
}

// What Store::Search would find in the sample week: the two weeks around the snapshot's day,
// each event once, on its first day from today on.
std::vector<DayItem> SampleSearch(std::wstring_view query) {
  std::vector<DayItem> found;
  const std::wstring needle = Folded(query);
  if (needle.find_first_not_of(L' ') == std::wstring::npos) return found;
  for (int offset = -7; offset <= 7; ++offset) {
    const Date day = AddDays(kSnapshotToday, offset);
    for (DayItem item : detail::SampleAppDay(day)) {
      if (Folded(item.title).find(needle) == std::wstring::npos) continue;
      const bool seen = std::any_of(found.begin(), found.end(),
                                    [&](const DayItem& other) { return other.uid == item.uid; });
      if (seen || (offset < 0 && item.repeats)) continue;
      item.occurrence = day;
      found.push_back(item);
    }
  }
  std::stable_sort(found.begin(), found.end(), [](const DayItem& a, const DayItem& b) {
    const bool aAhead = a.occurrence >= kSnapshotToday;
    const bool bAhead = b.occurrence >= kSnapshotToday;
    if (aAhead != bAhead) return aAhead;
    return aAhead ? a.occurrence < b.occurrence : a.occurrence > b.occurrence;
  });
  return found;
}

}  // namespace

bool RenderSnapshot(std::wstring_view view, std::wstring_view theme, D2D1_SIZE_F panel,
                    std::wstring_view text, const std::filesystem::path& out) {
  // "popup-creado" is the same panel a moment after Enter. A still of something that lasts a
  // hundred and sixty milliseconds is the only way to judge it without filming the screen.
  const bool justCreated = view == L"popup-creado";
  const bool offline = view == L"popup-sin-conexion";
  const bool app = view.starts_with(L"app-");
  const bool transition = view == L"app-transicion";
  const bool settings = view == L"configuracion";
  if (!KnowsSnapshotView(view)) {
    LogError(L"--render-snapshot does not know the view '{}'", view);
    return false;
  }

  const Theme palette = ResolveTheme(theme);

  // The design size unless asked otherwise, never the size this monitor would give: a
  // committed PNG must not depend on the machine that produced it.
  const PanelLayout layout =
      panel.width > 0.0f && panel.height > 0.0f ? MakeLayout(panel) : BaseLayout();

  Fonts fonts;
  if (!fonts.Create(layout)) return false;

  // How the popup looks the instant it opens: the input focused and waiting, nothing typed,
  // unless --text says otherwise. The hour is pinned like the day, so a PNG of the preview
  // does not change with the hour it was taken at.
  PopupModel model = MakeModel(kSnapshotToday);
  model.focus = 1.0f;
  model.caretOn = true;
  // Made-up data and not the user's cache: a committed PNG has to change when the design does
  // and not when somebody writes something down that morning.
  FillSampleData(model);
  model.offline = offline;
  if (justCreated) {
    DayItem fresh;
    fresh.uid = L"sample-nuevo";
    fresh.title = L"Dentista";
    fresh.startMin = 17 * 60;
    fresh.endMin = 18 * 60;
    fresh.color = detail::kSampleEventColor;
    model.day.push_back(fresh);
    std::sort(model.day.begin(), model.day.end(), EarlierThan);
    model.enterUid = fresh.uid;
    model.enterT = 0.30f;  // caught on the way up, where the lift and the fade are visible
    model.toast = T(L"Creado · Deshacer", L"Created · Undo");
    model.toastT = 1.0f;
  }
  const bool search = view == L"popup-buscar" || view == L"app-buscar";
  if (search && text.empty()) text = L"?con";
  if (!text.empty()) {
    model.input.Insert(text);
    if (Searching(model)) {
      model.results = SampleSearch(SearchQuery(model));
    } else {
      model.preview = nlp::ParseInput(text, nlp::Now{kSnapshotToday, kSnapshotMinute});
    }
  }

  // The app: the popup's layout is still the sidebar, and the app is laid out at its design
  // size -- or, for the transition, at the size the window has halfway between the two, placed
  // where it would be on a real work area.
  AppModel appModel;
  appModel.view = ViewFor(view);
  appModel.nowMinute = kSnapshotMinute;
  float progress = 1.0f;
  RECT window{0, 0, static_cast<LONG>(kAppBaseWidthDip), static_cast<LONG>(kAppBaseHeightDip)};
  if (app) {
    // In the app the capsule waits unfocused, so D, S and M are views and not letters.
    model.focus = text.empty() ? 0.0f : 1.0f;
    model.caretOn = !text.empty();
    FillSampleApp(model, appModel);
    const Date tuesday = kSnapshotToday;
    if (view == L"app-detalle") {
      // The coffee with Ana, open, with the keyboard in its location.
      DetailModel& detail = appModel.detail;
      detail.open = true;
      detail.t = 1.0f;
      detail.event = EventDetail{L"week-2", "familia", L"Café con Ana",
                                 L"Café Pergamino, El Poblado",
                                 L"Llevarle las fotos del viaje y el libro que le debo.", L"",
                                 tuesday, 10 * 60, tuesday, 11 * 60};
      const std::wstring texts[kDetailFields] = {
          detail.event.title, DayFieldText(tuesday), TimeFieldText(10 * 60),
          TimeFieldText(11 * 60), detail.event.location, detail.event.notes};
      detail.fields[kFieldNotes].AllowNewlines(true);
      for (int i = 0; i < kDetailFields; ++i) detail.fields[i].Insert(texts[i]);
      detail.focus = kFieldLocation;
      detail.caretOn = true;
      appModel.selected = L"week-2";
    } else if (view == L"app-arrastre") {
      // The interview, picked up on Wednesday at eleven and on its way to Thursday at three.
      appModel.ghost = Ghost{true, false, 3, 15 * 60, 16 * 60, detail::kSampleWorkColor,
                             L"Entrevista", L"week-6", {}};
    } else if (view == L"app-borrar") {
      appModel.selected = L"week-3";
      appModel.confirm = ConfirmDeleteText(L"Revisión de código");
    } else if (view == L"app-repeticion") {
      // Monday's gym dragged somewhere else: which one moves, this Monday's or every Monday's.
      appModel.selected = L"gym";
      appModel.scope.text = ScopeText(L"Gimnasio", false);
    }
    if (transition) {
      progress = 0.5f;
      window = LerpRect(PlaceRect(kSnapshotWork, layout.size(), USER_DEFAULT_SCREEN_DPI),
                        ExpandedRect(kSnapshotWork), progress);
    }
  }
  const D2D1_SIZE_F appSize{static_cast<float>(window.right - window.left),
                            static_cast<float>(window.bottom - window.top)};
  const AppLayout appLayout = MakeAppLayout(appSize, layout, appModel.view, AllDayRows(appModel),
                                            EaseOutCubic(appModel.detail.t));
  if (app) {
    const Date first = appModel.first;
    const bool showsToday = first <= kSnapshotToday &&
                            kSnapshotToday < AddDays(first, ShownDays(appModel.view));
    appModel.scroll = InitialScroll(appLayout, showsToday, kSnapshotMinute);
  }

  UINT width = static_cast<UINT>(layout.width) + 2 * kMarginDip;
  UINT height = static_cast<UINT>(layout.height) + 2 * kMarginDip;
  if (transition) {
    width = static_cast<UINT>(kSnapshotWork.right - kSnapshotWork.left);
    height = static_cast<UINT>(kSnapshotWork.bottom - kSnapshotWork.top);
  } else if (settings) {
    // No margin: it is a window with its own frame, and what is judged is its client area.
    width = static_cast<UINT>(SettingsWindow::SizeDip().width);
    height = static_cast<UINT>(SettingsWindow::SizeDip().height);
  } else if (app) {
    width = static_cast<UINT>(appSize.width) + 2 * kMarginDip;
    height = static_cast<UINT>(appSize.height) + 2 * kMarginDip;
  }

  ComPtr<IWICImagingFactory> wic;
  if (Failed(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(&wic)),
             L"CoCreateInstance(WICImagingFactory)")) {
    return false;
  }

  ComPtr<IWICBitmap> bitmap;
  if (Failed(wic->CreateBitmap(width, height, GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapCacheOnLoad, &bitmap),
             L"IWICImagingFactory::CreateBitmap")) {
    return false;
  }

  ComPtr<ID2D1Factory> factory;
  if (Failed(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.GetAddressOf()),
             L"D2D1CreateFactory")) {
    return false;
  }

  ComPtr<ID2D1RenderTarget> target;
  const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
      96.0f);
  if (Failed(factory->CreateWicBitmapRenderTarget(bitmap.Get(), properties, &target),
             L"CreateWicBitmapRenderTarget")) {
    return false;
  }

  target->BeginDraw();
  target->Clear(palette.light ? kLightBackdrop : kDarkBackdrop);
  const float margin = static_cast<float>(kMarginDip);
  const float originX = transition ? static_cast<float>(window.left) : margin;
  const float originY = transition ? static_cast<float>(window.top) : margin;
  target->SetTransform(D2D1::Matrix3x2F::Translation(originX, originY));
  if (settings) {
    target->SetTransform(D2D1::Matrix3x2F::Identity());
    Preferences prefs;
    prefs.theme = std::wstring(theme);
    SettingsWindow settingsWindow;
    settingsWindow.Init(nullptr, &prefs, nullptr, nullptr, {});
    std::vector<CalendarInfo> calendars;
    for (const CalendarInfo& calendar : SampleCalendars()) {
      if (!calendar.isTaskList) calendars.push_back(calendar);
    }
    settingsWindow.PaintForSnapshot(target.Get(), palette, std::move(calendars));
  } else if (app) {
    DrawApp(target.Get(), fonts, palette, layout, appLayout, model, appModel, progress,
            /*acrylic=*/true);
  } else {
    DrawPopup(target.Get(), fonts, palette, layout, model, /*acrylic=*/true);
  }
  if (Failed(target->EndDraw(), L"ID2D1RenderTarget::EndDraw")) return false;

  std::error_code ec;
  if (!out.parent_path().empty()) std::filesystem::create_directories(out.parent_path(), ec);

  ComPtr<IWICStream> stream;
  if (Failed(wic->CreateStream(&stream), L"IWICImagingFactory::CreateStream")) return false;
  if (Failed(stream->InitializeFromFilename(out.c_str(), GENERIC_WRITE),
             L"IWICStream::InitializeFromFilename")) {
    return false;
  }

  ComPtr<IWICBitmapEncoder> encoder;
  if (Failed(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder),
             L"IWICImagingFactory::CreateEncoder")) {
    return false;
  }
  if (Failed(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache),
             L"IWICBitmapEncoder::Initialize")) {
    return false;
  }

  ComPtr<IWICBitmapFrameEncode> frame;
  if (Failed(encoder->CreateNewFrame(&frame, nullptr), L"IWICBitmapEncoder::CreateNewFrame")) {
    return false;
  }
  if (Failed(frame->Initialize(nullptr), L"IWICBitmapFrameEncode::Initialize")) return false;
  if (Failed(frame->SetSize(width, height), L"IWICBitmapFrameEncode::SetSize")) return false;

  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  if (Failed(frame->SetPixelFormat(&format), L"IWICBitmapFrameEncode::SetPixelFormat")) {
    return false;
  }
  if (Failed(frame->WriteSource(bitmap.Get(), nullptr), L"IWICBitmapFrameEncode::WriteSource")) {
    return false;
  }
  if (Failed(frame->Commit(), L"IWICBitmapFrameEncode::Commit")) return false;
  if (Failed(encoder->Commit(), L"IWICBitmapEncoder::Commit")) return false;

  LogInfo(L"snapshot: wrote {}x{} to {}", width, height, out.wstring());
  return true;
}

}  // namespace agenda
