#include "ui/paint.h"

#include "core/hr.h"

using Microsoft::WRL::ComPtr;

namespace agenda {
namespace {

// Windows 11 ships the optical sizes of Segoe UI Variable; everything the popup draws is
// between 11 and 15 DIP, which is the range the Text cut is designed for. Windows 10 has none
// of them, hence the fallback.
const wchar_t* PickFamily(IDWriteFactory* factory) {
  ComPtr<IDWriteFontCollection> collection;
  if (SUCCEEDED(factory->GetSystemFontCollection(&collection))) {
    UINT32 index = 0;
    BOOL exists = FALSE;
    if (SUCCEEDED(collection->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) &&
        exists) {
      return L"Segoe UI Variable Text";
    }
  }
  return L"Segoe UI";
}

bool MakeFormat(IDWriteFactory* factory, const wchar_t* family, float size,
                DWRITE_FONT_WEIGHT weight, ComPtr<IDWriteTextFormat>& out) {
  if (Failed(factory->CreateTextFormat(family, nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                       DWRITE_FONT_STRETCH_NORMAL, size, L"es-CO", &out),
             L"IDWriteFactory::CreateTextFormat")) {
    return false;
  }

  out->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  out->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

  ComPtr<IDWriteInlineObject> ellipsis;
  if (SUCCEEDED(factory->CreateEllipsisTrimmingSign(out.Get(), &ellipsis))) {
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    out->SetTrimming(&trimming, ellipsis.Get());
  }
  return true;
}

DWRITE_TEXT_ALIGNMENT ToDWrite(Align align) {
  switch (align) {
    case Align::Center:
      return DWRITE_TEXT_ALIGNMENT_CENTER;
    case Align::Right:
      return DWRITE_TEXT_ALIGNMENT_TRAILING;
    case Align::Left:
    default:
      return DWRITE_TEXT_ALIGNMENT_LEADING;
  }
}

}  // namespace

bool Fonts::Create() {
  if (Failed(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                 reinterpret_cast<IUnknown**>(factory.GetAddressOf())),
             L"DWriteCreateFactory")) {
    return false;
  }

  const wchar_t* family = PickFamily(factory.Get());
  return MakeFormat(factory.Get(), family, kFontLabel, DWRITE_FONT_WEIGHT_NORMAL, label) &&
         MakeFormat(factory.Get(), family, kFontDay, DWRITE_FONT_WEIGHT_NORMAL, day) &&
         MakeFormat(factory.Get(), family, kFontEvent, DWRITE_FONT_WEIGHT_NORMAL, event) &&
         MakeFormat(factory.Get(), family, kFontTitle, DWRITE_FONT_WEIGHT_SEMI_BOLD, title);
}

void FillRound(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, float radius,
               ID2D1Brush* brush) {
  target->FillRoundedRectangle(D2D1_ROUNDED_RECT{rect, radius, radius}, brush);
}

void StrokeRound(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, float radius,
                 ID2D1Brush* brush, float width) {
  // Half the stroke inset so the line lands inside the shape instead of straddling its edge.
  const float half = width / 2.0f;
  target->DrawRoundedRectangle(D2D1_ROUNDED_RECT{Inset(rect, half), radius - half, radius - half},
                               brush, width);
}

void FillCircle(ID2D1RenderTarget* target, D2D1_POINT_2F center, float radius,
                ID2D1Brush* brush) {
  target->FillEllipse(D2D1_ELLIPSE{center, radius, radius}, brush);
}

void DrawTextIn(ID2D1RenderTarget* target, IDWriteTextFormat* format, std::wstring_view text,
                const D2D1_RECT_F& rect, ID2D1Brush* brush, Align align) {
  if (format == nullptr || text.empty()) return;
  format->SetTextAlignment(ToDWrite(align));
  target->DrawText(text.data(), static_cast<UINT32>(text.size()), format, rect, brush);
}

}  // namespace agenda
