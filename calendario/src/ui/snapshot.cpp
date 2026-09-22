#include "ui/snapshot.h"

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <system_error>

#include "core/hr.h"
#include "core/log.h"
#include "ui/layout.h"
#include "ui/popup_view.h"

using Microsoft::WRL::ComPtr;

namespace agenda {
namespace {

// Offscreen there is no desktop to blur, so a flat neutral grey stands in for the acrylic.
// Without it the translucent panel would be judged against whatever an image viewer puts
// behind transparency, which is usually white and lies about the design.
constexpr D2D1_COLOR_F kBackdrop{0.27f, 0.27f, 0.30f, 1.0f};
constexpr int kMarginDip = 24;  // grey visible around the panel, so the corners can be judged

}  // namespace

bool RenderSnapshot(std::wstring_view view, const std::filesystem::path& out) {
  if (view != L"popup") {
    LogError(L"--render-snapshot only knows 'popup', got '{}'", view);
    return false;
  }

  const UINT width = static_cast<UINT>(kPopupWidthDip + 2 * kMarginDip);
  const UINT height = static_cast<UINT>(kPopupHeightDip + 2 * kMarginDip);

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
  target->Clear(kBackdrop);
  target->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(kMarginDip),
                                                     static_cast<float>(kMarginDip)));
  DrawPopup(target.Get(),
            D2D1_SIZE_F{static_cast<float>(kPopupWidthDip), static_cast<float>(kPopupHeightDip)},
            /*acrylic=*/true);
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
