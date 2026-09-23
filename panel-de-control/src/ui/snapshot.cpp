#include "ui/snapshot.h"

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <system_error>

#include "core/hr.h"
#include "core/log.h"
#include "core/options.h"
#include "model/state.h"
#include "ui/layout.h"
#include "ui/paint.h"
#include "ui/panel_view.h"
#include "ui/theme.h"

using Microsoft::WRL::ComPtr;

namespace panel {
namespace {

// Offscreen there is no desktop to blur, so a flat grey stands in for the acrylic. Without it
// the translucent panel would be judged against whatever an image viewer shows behind
// transparency, which lies about the design. Agenda's values, so the two compare side by side.
constexpr D2D1_COLOR_F kDarkBackdrop{0.27f, 0.27f, 0.30f, 1.0f};
constexpr D2D1_COLOR_F kLightBackdrop{0.80f, 0.80f, 0.84f, 1.0f};
constexpr float kSnapshotMarginDip = 24.0f;

bool WritePng(IWICImagingFactory* wic, IWICBitmap* bitmap, UINT width, UINT height,
              const std::filesystem::path& out) {
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
             L"IWICImagingFactory::CreateEncoder") ||
      Failed(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache),
             L"IWICBitmapEncoder::Initialize")) {
    return false;
  }
  ComPtr<IWICBitmapFrameEncode> frame;
  if (Failed(encoder->CreateNewFrame(&frame, nullptr), L"IWICBitmapEncoder::CreateNewFrame") ||
      Failed(frame->Initialize(nullptr), L"IWICBitmapFrameEncode::Initialize") ||
      Failed(frame->SetSize(width, height), L"IWICBitmapFrameEncode::SetSize")) {
    return false;
  }
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
  return !Failed(frame->SetPixelFormat(&format), L"IWICBitmapFrameEncode::SetPixelFormat") &&
         !Failed(frame->WriteSource(bitmap, nullptr), L"IWICBitmapFrameEncode::WriteSource") &&
         !Failed(frame->Commit(), L"IWICBitmapFrameEncode::Commit") &&
         !Failed(encoder->Commit(), L"IWICBitmapEncoder::Commit");
}

}  // namespace

bool RenderSnapshot(std::wstring_view view, std::wstring_view theme,
                    const std::filesystem::path& out) {
  if (!KnowsSnapshotView(view)) {
    LogError(L"--render-snapshot does not know the view '{}'", view);
    return false;
  }

  const PanelState state = SampleState();
  ViewState viewState;
  viewState.open.brightness = view == L"panel-brillo";
  viewState.open.audio = view == L"panel-volumen";
  // A still has no mouse, so each view pretends one slider is being touched: that is the only
  // way the percentages ever reach a PNG.
  if (viewState.open.brightness) {
    viewState.hot = Hot::Display;
    viewState.hotDisplay = 0;
  } else {
    viewState.hot = Hot::Volume;
  }

  const PanelLayout layout = MakeLayout(viewState.open, state);
  const Theme palette = ResolveTheme(theme, /*systemAccent=*/false);
  Fonts fonts;
  if (!fonts.Create()) return false;

  const UINT width = static_cast<UINT>(std::lround(layout.width + 2.0f * kSnapshotMarginDip));
  const UINT height = static_cast<UINT>(std::lround(layout.height + 2.0f * kSnapshotMarginDip));

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
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f, 96.0f);
  if (Failed(factory->CreateWicBitmapRenderTarget(bitmap.Get(), properties, &target),
             L"CreateWicBitmapRenderTarget")) {
    return false;
  }

  target->BeginDraw();
  target->Clear(palette.light ? kLightBackdrop : kDarkBackdrop);
  target->SetTransform(D2D1::Matrix3x2F::Translation(kSnapshotMarginDip, kSnapshotMarginDip));
  DrawPanel(target.Get(), fonts, palette, layout, state, viewState, /*acrylic=*/true);
  if (Failed(target->EndDraw(), L"ID2D1RenderTarget::EndDraw")) return false;

  if (!WritePng(wic.Get(), bitmap.Get(), width, height, out)) return false;
  LogInfo(L"snapshot: wrote {}x{} to {}", width, height, out.wstring());
  return true;
}

}  // namespace panel
