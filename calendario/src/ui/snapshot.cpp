#include "ui/snapshot.h"

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <system_error>

#include "core/dates.h"
#include "core/hr.h"
#include "core/log.h"
#include "ui/layout.h"
#include "ui/paint.h"
#include "ui/popup_view.h"
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

}  // namespace

bool RenderSnapshot(std::wstring_view view, std::wstring_view theme, D2D1_SIZE_F panel,
                    std::wstring_view text, const std::filesystem::path& out) {
  if (view != L"popup") {
    LogError(L"--render-snapshot only knows 'popup', got '{}'", view);
    return false;
  }

  const Theme palette =
      theme == L"light" ? LightTheme() : (theme == L"dark" ? DarkTheme() : SystemTheme());

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
  if (!text.empty()) {
    model.input.Insert(text);
    model.preview = nlp::ParseInput(text, nlp::Now{kSnapshotToday, kSnapshotMinute});
  }

  const UINT width = static_cast<UINT>(layout.width) + 2 * kMarginDip;
  const UINT height = static_cast<UINT>(layout.height) + 2 * kMarginDip;

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
  target->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(kMarginDip),
                                                     static_cast<float>(kMarginDip)));
  DrawPopup(target.Get(), fonts, palette, layout, model, /*acrylic=*/true);
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
