#include "ui/paint.h"

#include "core/hr.h"

using Microsoft::WRL::ComPtr;

namespace panel {
namespace {

bool HasFamily(IDWriteFactory* factory, const wchar_t* name) {
  ComPtr<IDWriteFontCollection> collection;
  if (FAILED(factory->GetSystemFontCollection(&collection))) return false;
  UINT32 index = 0;
  BOOL exists = FALSE;
  return SUCCEEDED(collection->FindFamilyName(name, &index, &exists)) && exists;
}

// Windows 11 ships the optical sizes of Segoe UI Variable and Segoe Fluent Icons; Windows 10
// has neither, hence the fallbacks. The code points are the same in both icon fonts.
const wchar_t* TextFamily(IDWriteFactory* factory) {
  return HasFamily(factory, L"Segoe UI Variable Text") ? L"Segoe UI Variable Text" : L"Segoe UI";
}

const wchar_t* IconFamily(IDWriteFactory* factory) {
  return HasFamily(factory, L"Segoe Fluent Icons") ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
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
  const wchar_t* text = TextFamily(factory.Get());
  const wchar_t* icons = IconFamily(factory.Get());
  if (!MakeFormat(factory.Get(), text, 13.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD, title) ||
      !MakeFormat(factory.Get(), text, 13.0f, DWRITE_FONT_WEIGHT_NORMAL, body) ||
      !MakeFormat(factory.Get(), text, 11.0f, DWRITE_FONT_WEIGHT_NORMAL, caption) ||
      !MakeFormat(factory.Get(), text, 11.0f, DWRITE_FONT_WEIGHT_NORMAL, note) ||
      !MakeFormat(factory.Get(), icons, 16.0f, DWRITE_FONT_WEIGHT_NORMAL, icon) ||
      !MakeFormat(factory.Get(), icons, 12.0f, DWRITE_FONT_WEIGHT_NORMAL, iconSmall)) {
    return false;
  }
  // A notice is a sentence, not a label: it wraps instead of losing its end to an ellipsis.
  note->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  return true;
}

void FillRound(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, float radius,
               ID2D1Brush* brush) {
  target->FillRoundedRectangle(D2D1_ROUNDED_RECT{rect, radius, radius}, brush);
}

void StrokeRound(ID2D1RenderTarget* target, const D2D1_RECT_F& rect, float radius,
                 ID2D1Brush* brush, float width) {
  // Half the stroke inset so the line lands inside the shape instead of straddling its edge.
  const float half = width / 2.0f;
  target->DrawRoundedRectangle(
      D2D1_ROUNDED_RECT{Inset(rect, half, half), radius - half, radius - half}, brush, width);
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

void DrawGlyph(ID2D1RenderTarget* target, IDWriteTextFormat* format, wchar_t glyph,
               D2D1_POINT_2F center, ID2D1Brush* brush) {
  if (format == nullptr || glyph == 0) return;
  // A box twice the glyph's size, centred both ways: the icon fonts sit their glyphs in the
  // middle of the em square, so this lands them on the point.
  const float half = format->GetFontSize();
  format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  target->DrawText(&glyph, 1, format,
                   D2D1_RECT_F{center.x - half, center.y - half, center.x + half, center.y + half},
                   brush);
}

}  // namespace panel
