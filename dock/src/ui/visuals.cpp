#include "ui/visuals.h"

#include <DispatcherQueue.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi.h>
#include <windows.ui.composition.interop.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>

#include "core/log.h"
#include "ui/text.h"

namespace wuc = winrt::Windows::UI::Composition;
using Microsoft::WRL::ComPtr;

namespace dock {
namespace {

// Windows.UI.Composition exige una DispatcherQueue en el hilo antes del Compositor, y el
// controller hay que conservarlo vivo o el compositor se queda sin cola. Uno por proceso,
// creado en el hilo de UI la primera vez que un dock lo pide.
wuc::Compositor SharedCompositor() {
  static winrt::Windows::System::DispatcherQueueController controller{nullptr};
  static wuc::Compositor compositor{nullptr};
  if (!compositor) {
    // Con DQTYPE_THREAD_CURRENT la documentación exige DQTAT_COM_NONE.
    DispatcherQueueOptions options{sizeof(options), DQTYPE_THREAD_CURRENT, DQTAT_COM_NONE};
    CreateDispatcherQueueController(
        options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(winrt::put_abi(controller)));
    compositor = wuc::Compositor();
  }
  return compositor;
}

// El device que sube píxeles a superficies. WARP y no el adaptador de verdad: no renderiza
// ni un fotograma, solo sube píxeles, y HARDWARE mapeaba el driver de usuario entero con su
// pool de shaders (medido en C#: 79,6 frente a 34,9 MB privados, 70 frente a 25 hilos). Se
// crea la primera vez que hace falta una superficie.
wuc::CompositionGraphicsDevice Graphics() {
  static wuc::CompositionGraphicsDevice graphics{nullptr};
  if (graphics) return graphics;
  ComPtr<ID3D11Device> d3d;
  // BGRA_SUPPORT: sin él Direct2D no puede usar el device.
  winrt::check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                                         nullptr, 0, D3D11_SDK_VERSION, &d3d, nullptr, nullptr));
  ComPtr<IDXGIDevice> dxgi;
  winrt::check_hresult(d3d.As(&dxgi));
  ComPtr<ID2D1Device> d2d;
  winrt::check_hresult(D2D1CreateDevice(dxgi.Get(), nullptr, &d2d));
  winrt::check_hresult(SharedCompositor().as<ABI::Windows::UI::Composition::ICompositorInterop>()->CreateGraphicsDevice(
      d2d.Get(), reinterpret_cast<ABI::Windows::UI::Composition::ICompositionGraphicsDevice**>(winrt::put_abi(graphics))));
  return graphics;
}

// Dibuja en una superficie nueva de w x h. BeginDraw puede devolver un hueco dentro de un
// atlas compartido: se dibuja en el offset que indica y se limpia antes, o se arrastran los
// píxeles del inquilino anterior.
template <class Paint>
wuc::CompositionDrawingSurface Surface(float width, float height, Paint paint) {
  auto surface = Graphics().CreateDrawingSurface(
      {width, height}, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
      winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
  auto interop = surface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
  ComPtr<ID2D1DeviceContext> context;
  POINT offset{};
  winrt::check_hresult(interop->BeginDraw(nullptr, IID_PPV_ARGS(&context), &offset));
  // Nace con el ppp del escritorio (120 aquí) y DrawBitmap trabaja en DIP: sin esto el
  // dibujo sale un 25% más grande que la superficie, recortado.
  context->SetDpi(96, 96);
  context->Clear(D2D1::ColorF(0, 0, 0, 0));
  paint(context.Get(), offset);
  interop->EndDraw();
  return surface;
}

std::map<std::pair<std::wstring, int>, wuc::CompositionSurfaceBrush>& IconCache() {
  static std::map<std::pair<std::wstring, int>, wuc::CompositionSurfaceBrush> cache;
  return cache;
}

// Superficie del icono a su tamaño máximo de dibujo (icono × magnificación), no a 256: el de
// C# subía 256x256 por icono y por pantalla, 256 KB cada una. Se comparte entre docks.
// La de la caché si ya está; si no, se sube desde los píxeles, o nada si no los hay (el icono
// de esa app aún no ha llegado del proceso hijo).
wuc::CompositionSurfaceBrush IconBrush(const wuc::Compositor& compositor, const std::wstring& key, int px,
                                       const IconBitmap* pixels) {
  auto& cache = IconCache();
  if (auto found = cache.find({key, px}); found != cache.end()) return found->second;
  if (!pixels) return nullptr;
  const IconBitmap& icon = *pixels;
  auto surface = Surface(static_cast<float>(px), static_cast<float>(px), [&](ID2D1DeviceContext* context, POINT at) {
    ComPtr<ID2D1Bitmap1> bitmap;
    const D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(context->CreateBitmap(D2D1::SizeU(icon.width, icon.height), icon.bgra.data(), icon.width * 4, props,
                                     &bitmap)))
      return;
    const float fit = static_cast<float>(px) / std::max(icon.width, icon.height);
    const float w = icon.width * fit, h = icon.height * fit;
    const D2D1_RECT_F dest = D2D1::RectF(at.x + (px - w) / 2, at.y + (px - h) / 2, at.x + (px + w) / 2, at.y + (px + h) / 2);
    // MULTI_SAMPLE_LINEAR, no HIGH_QUALITY_CUBIC: el cúbico va por efectos y WARP compila sus
    // sombreadores en la CPU. Medido con diez iconos en tres pantallas: cúbico 32,7 MB y 21
    // hilos, este 25,0 MB y 18, y sus píxeles casi idénticos (27 de 30800 difieren en más de
    // 30/765). LINEAR daba 20,8 MB pero con dientes: 1056 píxeles distintos.
    // ponytail: escalar en WARP cuesta ~4 MB; extraer ya al tamaño de cada pantalla en el
    // proceso hijo se lo ahorraría, si la memoria aprieta.
    context->DrawBitmap(bitmap.Get(), &dest, 1.0f, D2D1_INTERPOLATION_MODE_MULTI_SAMPLE_LINEAR);
    // El ID2D1Bitmap se suelta al salir de aquí (ComPtr): en C# esperaba al finalizador.
  });
  auto brush = compositor.CreateSurfaceBrush(surface);
  // Por defecto Stretch es None y el visual enseñaría un recorte del centro.
  brush.Stretch(wuc::CompositionStretch::Uniform);
  cache.emplace(std::make_pair(key, px), brush);
  return brush;
}

wuc::CompositionBrush AcrylicBrush(const wuc::Compositor& compositor) {
  // El material NO se le pide a DWM (DWMWA_SYSTEMBACKDROP_TYPE): la ventana ocupa todo el
  // ancho del monitor y un backdrop de DWM pintaría el rectángulo entero. HostBackdrop
  // muestrea el escritorio ya desenfocado y se aplica solo donde se quiere.
  try {
    return compositor.CreateHostBackdropBrush();
  } catch (const winrt::hresult_error& e) {
    LogError(L"[acrilico] no disponible ({:#010x}); color sólido", static_cast<unsigned>(e.code().value));
    return compositor.CreateColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(200, 32, 32, 40));
  }
}

// --- Expresiones --------------------------------------------------------------------
//
// Tienen un límite de longitud que se alcanza antes de lo que parece: cada icono repetía
// cuatro veces G(−c/R) y dos la transferencia del ancho total. Así que los subtérminos
// compartidos (G0, TW, Origin) se calculan UNA vez como animaciones del property set y los
// iconos solo los referencian. El orden importa: TW usa G0 y Origin usa TW.

// Decimales fijos: nada de exponentes ("1e-05") ni de coma decimal, que el lenguaje de
// expresiones no entiende. std::format no usa la configuración regional.
std::wstring F(float value) { return std::format(L"{:.6f}", value); }

std::wstring G(const std::wstring& t) {
  const std::wstring clamped = L"Clamp(" + t + L",-1,1)";
  return L"(" + clamped + L"*0.5 + Sin(3.14159265*" + clamped + L")*0.15915494)";
}

std::wstring GAt(const Curve& curve, float u) { return G(L"((" + F(u) + L" - P.C)*" + F(1 / curve.Radius()) + L")"); }

// T(u) − Origin: la transferencia sin centrar.
std::wstring Transfer(const Curve& curve, float u) {
  return L"(" + F(u) + L" + " + F(curve.MaxGrowth()) + L"*P.Amount*(" + GAt(curve, u) + L" - P.G0))";
}

// El borde izquierdo proyectado. I.Bounce e I.Shift son propiedades del propio visual:
// rebote y arrastre suman dentro de la misma expresión en vez de pelearse con ella por
// Offset. Shift va DESPUÉS de Transfer, en píxeles de pantalla: dentro arrastraría el
// término G entero y rompería el límite de longitud.
std::wstring IconOffset(const Curve& curve, int i, float top) {
  return L"Vector3(P.Origin + " + Transfer(curve, curve.RestLeft(i)) + L" + I.Shift, " + F(top) + L" - I.Bounce, 0)";
}

// Escala = ancho proyectado / ancho en reposo. Al restar los bordes el anclaje G0 se cancela
// solo y la expresión sale más corta.
std::wstring IconScale(const Curve& curve, int i) {
  const float a = curve.RestLeft(i), b = curve.RestRight(i);
  const std::wstring s = L"((" + F(b - a) + L" + " + F(curve.MaxGrowth()) + L"*P.Amount*(" + GAt(curve, b) + L" - " +
                         GAt(curve, a) + L"))*" + F(1 / curve.At(i).content) + L")";
  return L"Vector3(" + s + L", " + s + L", 1)";
}

// Centro del elemento menos medio ancho: para la etiqueta (y en F5 el puntito). Lleva el
// mismo Shift que el icono, o se quedaría atrás al arrastrarlo.
std::wstring ItemCenter(const Curve& curve, int i, float width, float top) {
  return L"Vector3(P.Origin + (" + Transfer(curve, curve.RestLeft(i)) + L" + " + Transfer(curve, curve.RestRight(i)) +
         L")*0.5 - " + F(width / 2) + L" + I.Shift, " + F(top) + L", 0)";
}

void Animate(const wuc::Compositor& compositor, const wuc::CompositionObject& target, const std::wstring& property,
             const std::wstring& expression, const wuc::CompositionPropertySet& props,
             const wuc::CompositionObject& self = nullptr) {
  auto animation = compositor.CreateExpressionAnimation(expression);
  animation.SetReferenceParameter(L"P", props);
  if (self) animation.SetReferenceParameter(L"I", self);
  target.StartAnimation(property, animation);
}

}  // namespace

Visuals::Visuals(HWND hwnd) : compositor_(SharedCompositor()) {
  compositor_.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>()->CreateDesktopWindowTarget(
      hwnd, TRUE, reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(target_)));
  root_ = compositor_.CreateContainerVisual();
  root_.RelativeSizeAdjustment({1, 1});
  target_.Root(root_);

  // La ÚNICA entrada de la animación: el cursor en reposo y la intensidad del hover.
  props_ = compositor_.CreatePropertySet();
  props_.InsertScalar(L"C", 0);
  props_.InsertScalar(L"Amount", 0);
}

bool Visuals::HasIcon(const std::wstring& key, int px) { return IconCache().contains({key, px}); }

void Visuals::KeepOnlyIcons(const std::set<std::pair<std::wstring, int>>& used) {
  std::erase_if(IconCache(), [&](const auto& entry) { return !used.contains(entry.first); });
}

void Visuals::Build(const Curve& curve, const std::vector<DockItem>& items, const IconSet& icons, float windowWidth,
                    float windowHeight, float padding, float iconSize, float scale) {
  root_.Children().RemoveAll();
  menu_ = nullptr;  // se iba con el árbol
  menuHot_ = nullptr;
  addZone_ = nullptr;
  addZoneHot_ = false;
  dropTarget_ = -1;
  labels_.clear();
  dots_.clear();
  items_.clear();
  labelShown_ = -1;

  props_.InsertScalar(L"G0", 0);
  props_.InsertScalar(L"TW", curve.RestWidth());
  props_.InsertScalar(L"Origin", 0);
  Animate(compositor_, props_, L"G0", G(L"((0 - P.C)*" + F(1 / curve.Radius()) + L")"), props_);
  Animate(compositor_, props_, L"TW", Transfer(curve, curve.RestWidth()), props_);
  Animate(compositor_, props_, L"Origin", L"((" + F(windowWidth) + L" - P.TW)*0.5)", props_);

  // La barra: acrílico debajo y un tinte blanco translúcido encima (sin él, sobre un fondo
  // oscuro el desenfoque queda casi negro y los iconos no se leen). Crece con la fila, como
  // en macOS: la ventana es fija y del tamaño máximo, la barra sigue a los iconos.
  const float barHeight = iconSize + padding * 2;
  const float barTop = windowHeight - barHeight;
  auto bar = compositor_.CreateContainerVisual();
  Animate(compositor_, bar, L"Offset", L"Vector3(P.Origin - " + F(padding) + L", " + F(barTop) + L", 0)", props_);
  Animate(compositor_, bar, L"Size", L"Vector2(P.TW + " + F(padding * 2) + L", " + F(barHeight) + L")", props_);
  // Esquinas recortadas en el compositor, con antialias; la geometría lleva su propio Size.
  auto corners = compositor_.CreateRoundedRectangleGeometry();
  corners.CornerRadius({barHeight * 0.28f, barHeight * 0.28f});
  Animate(compositor_, corners, L"Size", L"Vector2(P.TW + " + F(padding * 2) + L", " + F(barHeight) + L")", props_);
  bar.Clip(compositor_.CreateGeometricClip(corners));
  auto material = compositor_.CreateSpriteVisual();
  material.RelativeSizeAdjustment({1, 1});
  material.Brush(AcrylicBrush(compositor_));
  bar.Children().InsertAtBottom(material);
  auto tint = compositor_.CreateSpriteVisual();
  tint.RelativeSizeAdjustment({1, 1});
  tint.Brush(compositor_.CreateColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(48, 255, 255, 255)));
  bar.Children().InsertAtTop(tint);
  root_.Children().InsertAtBottom(bar);
  hiddenOffset_ = barHeight;
  padding_ = padding;
  barTop_ = barTop;

  const int iconPx = static_cast<int>(std::ceil(iconSize * curve.MaxScale()));
  const float iconTop = windowHeight - padding - iconSize;
  const float dotSize = std::max(4.0f, padding * 0.4f);
  const float dotTop = windowHeight - padding + (padding - dotSize) * 0.5f;
  for (int i = 0; i < curve.Count() && i < static_cast<int>(items.size()); i++) {
    const DockItem& item = items[i];
    auto visual = compositor_.CreateSpriteVisual();
    float top = iconTop;
    if (item.separator) {
      // Una raya fina y discreta, más corta que los iconos.
      visual.Brush(compositor_.CreateColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(60, 255, 255, 255)));
      visual.Size({curve.At(i).content, iconSize * 0.55f});
      top = windowHeight - padding - iconSize * 0.55f;
    } else {
      const auto found = icons.find(item.iconKey);
      if (auto brush = IconBrush(compositor_, item.iconKey, iconPx, found != icons.end() ? &found->second : nullptr))
        visual.Brush(brush);
      visual.Size({curve.At(i).content, iconSize});
    }
    // CenterPoint en el borde INFERIOR izquierdo: crece hacia arriba y a la derecha.
    visual.CenterPoint({0, visual.Size().y, 0});
    visual.Properties().InsertScalar(L"Bounce", 0);
    visual.Properties().InsertScalar(L"Shift", 0);
    Animate(compositor_, visual, L"Offset", IconOffset(curve, i, top), props_, visual);
    Animate(compositor_, visual, L"Scale", IconScale(curve, i), props_);
    root_.Children().InsertAtTop(visual);
    items_.push_back(visual);

    if (item.separator) {
      dots_.push_back(nullptr);
      labels_.push_back(nullptr);
      continue;
    }
    // El puntito de "abierta": nace con el estado que ya se conocía, para que reconstruir
    // (una app sin anclar que se abre) no los apague todos un instante.
    auto dot = compositor_.CreateSpriteVisual();
    dot.Size({dotSize, dotSize});
    dot.Brush(compositor_.CreateColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(235, 255, 255, 255)));
    dot.Opacity(i < static_cast<int>(running_.size()) && running_[i] ? 1.0f : 0.0f);
    auto round = compositor_.CreateRoundedRectangleGeometry();
    round.Size({dotSize, dotSize});
    round.CornerRadius({dotSize / 2, dotSize / 2});
    dot.Clip(compositor_.CreateGeometricClip(round));
    // Mismo Shift que el icono: al arrastrarlo, el punto no se queda huérfano en el hueco.
    Animate(compositor_, dot, L"Offset", ItemCenter(curve, i, dotSize, dotTop), props_, visual);
    root_.Children().InsertAtTop(dot);
    dots_.push_back(dot);

    if (item.name.empty()) {
      labels_.push_back(nullptr);
      continue;
    }
    // Una etiqueta por elemento y no una compartida que se redibuje: así la coloca una
    // expresión, como todo lo demás, y sigue al icono al magnificarse o arrastrarse.
    const TextSize size = MeasureLabel(item.name, scale);
    auto label = compositor_.CreateSpriteVisual();
    label.Size({size.width, size.height});
    label.Opacity(0);
    label.Brush(compositor_.CreateSurfaceBrush(Surface(size.width, size.height, [&](ID2D1DeviceContext* context, POINT at) {
      DrawLabel(context, item.name, scale, size, at);
    })));
    // Justo encima de donde llega el icono del todo magnificado: el aire que reserva la
    // ventana (kLabelRoom en DockWindow).
    const float labelTop = windowHeight - padding - iconSize * curve.MaxScale() - size.height - padding * 0.4f;
    Animate(compositor_, label, L"Offset", ItemCenter(curve, i, size.width, labelTop), props_, visual);
    root_.Children().InsertAtTop(label);
    labels_.push_back(label);
  }
}

void Visuals::SetCursor(float rest) { props_.InsertScalar(L"C", rest); }

void Visuals::Bounce(int index, float height, bool forever) {
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  auto jump = compositor_.CreateScalarKeyFrameAnimation();
  jump.InsertKeyFrame(0.0f, 0);
  jump.InsertKeyFrame(0.28f, height);
  jump.InsertKeyFrame(0.52f, 0);
  jump.InsertKeyFrame(0.74f, height * 0.42f);
  jump.InsertKeyFrame(1.0f, 0);
  jump.Duration(std::chrono::milliseconds(680));
  if (forever) jump.IterationBehavior(wuc::AnimationIterationBehavior::Forever);
  // Sobre la propiedad del propio icono, que la expresión de Offset ya resta: el rebote no
  // se pelea con la animación que es dueña de la posición.
  items_[index].Properties().StartAnimation(L"Bounce", jump);
}

void Visuals::StopBounce(int index) {
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  items_[index].Properties().StopAnimation(L"Bounce");
  items_[index].Properties().InsertScalar(L"Bounce", 0);
}

void Visuals::SetLifted(int index, bool lifted) {
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  auto visual = items_[index];
  // El orden Z es el de inserción: sin volver a meterlo arriba, el icono cogido pasaba por
  // debajo de los vecinos y desaparecía.
  if (lifted) {
    root_.Children().Remove(visual);
    root_.Children().InsertAtTop(visual);
  }
  auto fade = compositor_.CreateScalarKeyFrameAnimation();
  fade.InsertKeyFrame(1, lifted ? 0.85f : 1.0f);
  fade.Duration(std::chrono::milliseconds(120));
  visual.StartAnimation(L"Opacity", fade);
}

void Visuals::SetShift(int index, float x) {
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  // Un muelle en marcha es dueño de Shift: hay que pararlo antes de escribirla a mano.
  items_[index].Properties().StopAnimation(L"Shift");
  items_[index].Properties().InsertScalar(L"Shift", x);
}

void Visuals::SpringShift(int index, float x) {
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  auto spring = compositor_.CreateSpringScalarAnimation();
  spring.DampingRatio(0.9f);
  spring.Period(std::chrono::milliseconds(55));
  spring.FinalValue(x);
  items_[index].Properties().StartAnimation(L"Shift", spring);
}

void Visuals::Puff(int index) {
  if (index < 0 || index >= static_cast<int>(items_.size())) return;
  auto fade = compositor_.CreateScalarKeyFrameAnimation();
  fade.InsertKeyFrame(1, 0);
  fade.Duration(std::chrono::milliseconds(180));
  items_[index].StartAnimation(L"Opacity", fade);
  if (index < static_cast<int>(dots_.size()) && dots_[index]) dots_[index].StartAnimation(L"Opacity", fade);
}

void Visuals::SetDropTarget(int index, float height) {
  if (index == dropTarget_) return;
  auto lift = [&](int i, float to) {
    if (i < 0 || i >= static_cast<int>(items_.size())) return;
    // Bounce, la del rebote: es la misma idea de "este es el que recibe" y la expresión de
    // Offset ya la resta. Con muelle, que distingue "se ha levantado" de "ha parpadeado".
    auto spring = compositor_.CreateSpringScalarAnimation();
    spring.DampingRatio(0.7f);
    spring.Period(std::chrono::milliseconds(50));
    spring.FinalValue(to);
    items_[i].Properties().StartAnimation(L"Bounce", spring);
  };
  lift(dropTarget_, 0);
  dropTarget_ = index;
  lift(index, height);
}

namespace {
// El "+" mide un 58% del alto de la barra, separado de ella un 35% de su lado, y resaltado
// crece un 18% desde el centro.
constexpr float kZoneSide = 0.58f;
constexpr float kZoneGap = 0.35f;
constexpr float kZoneHot = 1.18f;
}  // namespace

float Visuals::AddZoneReach() const {
  const float size = hiddenOffset_ * kZoneSide;
  return size * kZoneGap + size * (1 + kZoneHot) / 2;
}

void Visuals::SetAddZone(bool visible) {
  if (!visible) {
    if (addZone_) root_.Children().Remove(addZone_);
    addZone_ = nullptr;
    addZoneHot_ = false;
    return;
  }
  if (addZone_) return;
  const float size = hiddenOffset_ * kZoneSide;
  const float top = barTop_ + (hiddenOffset_ - size) / 2;
  addZone_ = compositor_.CreateContainerVisual();
  addZone_.Size({size, size});
  addZone_.CenterPoint({size / 2, size / 2, 0});
  // Pegado al borde derecho de la barra, y siguiéndolo cuando la lupa la ensancha.
  Animate(compositor_, addZone_, L"Offset", L"Vector3(P.Origin + P.TW + " + F(padding_ + size * kZoneGap) + L", " + F(top) + L", 0)",
          props_);
  auto round = compositor_.CreateRoundedRectangleGeometry();
  round.Size({size, size});
  round.CornerRadius({size * 0.28f, size * 0.28f});
  addZone_.Clip(compositor_.CreateGeometricClip(round));
  // El material de la barra y no un chip blanco: un "+" blanco sobre blanco translúcido era
  // invisible encima de una página web en blanco, que es donde se probó primero en C#.
  auto material = compositor_.CreateSpriteVisual();
  material.RelativeSizeAdjustment({1, 1});
  material.Brush(AcrylicBrush(compositor_));
  addZone_.Children().InsertAtBottom(material);
  auto tint = compositor_.CreateSpriteVisual();
  tint.RelativeSizeAdjustment({1, 1});
  tint.Brush(compositor_.CreateColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(48, 255, 255, 255)));
  addZone_.Children().InsertAtTop(tint);
  const float thick = std::max(2.0f, size * 0.1f);
  const float arm = size * 0.46f;
  const auto ink = compositor_.CreateColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255));
  for (const bool across : {true, false}) {
    auto bar = compositor_.CreateSpriteVisual();
    bar.Size(across ? winrt::Windows::Foundation::Numerics::float2{arm, thick}
                    : winrt::Windows::Foundation::Numerics::float2{thick, arm});
    bar.Offset({(size - bar.Size().x) / 2, (size - bar.Size().y) / 2, 0});
    bar.Brush(ink);
    addZone_.Children().InsertAtTop(bar);
  }
  addZone_.Opacity(0);
  auto fade = compositor_.CreateScalarKeyFrameAnimation();
  fade.InsertKeyFrame(1, 1);
  fade.Duration(std::chrono::milliseconds(140));
  addZone_.StartAnimation(L"Opacity", fade);
  root_.Children().InsertAtTop(addZone_);
}

void Visuals::SetAddZoneHot(bool hot) {
  if (!addZone_ || hot == addZoneHot_) return;
  addZoneHot_ = hot;
  auto grow = compositor_.CreateSpringScalarAnimation();
  grow.DampingRatio(0.7f);
  grow.Period(std::chrono::milliseconds(50));
  grow.FinalValue(hot ? kZoneHot : 1.0f);
  addZone_.StartAnimation(L"Scale.X", grow);
  addZone_.StartAnimation(L"Scale.Y", grow);
}

namespace {
constexpr float kRowHeight = 30;
constexpr float kMenuPadX = 12;
constexpr float kMenuPadY = 5;
constexpr float kCloseWidth = 30;
}  // namespace

void Visuals::OpenMenu(const std::vector<std::wstring>& items, float anchorX, float bottom, float left, float right,
                       float scale, bool closable) {
  CloseMenu();
  if (items.empty()) return;
  menuRows_ = items.size();
  menuScale_ = scale;
  menuClosable_ = closable;

  float width = 0;
  for (const auto& item : items) width = std::max(width, MeasureLabel(item, scale).width);
  // Con ✕ se le reserva su hueco, o se comería el final del título, que es lo que distingue
  // una ventana de otra.
  width += (kMenuPadX * 2 + (closable ? kCloseWidth : 0)) * scale;
  const float height = items.size() * kRowHeight * scale + kMenuPadY * 2 * scale;
  menuSize_ = {std::ceil(width), std::ceil(height)};
  const float x = std::clamp(anchorX - menuSize_.x / 2, left, std::max(left, right - menuSize_.x));
  menuOrigin_ = {x, bottom - menuSize_.y};

  menu_ = compositor_.CreateContainerVisual();
  menu_.Size(menuSize_);
  menu_.Offset({menuOrigin_.x, menuOrigin_.y, 0});
  auto round = compositor_.CreateRoundedRectangleGeometry();
  round.Size(menuSize_);
  round.CornerRadius({10 * scale, 10 * scale});
  menu_.Clip(compositor_.CreateGeometricClip(round));

  auto chip = compositor_.CreateSpriteVisual();
  chip.RelativeSizeAdjustment({1, 1});
  chip.Brush(compositor_.CreateColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(242, 32, 32, 38)));
  menu_.Children().InsertAtBottom(chip);

  menuHot_ = compositor_.CreateSpriteVisual();
  menuHot_.Size({menuSize_.x - kMenuPadY * 2 * scale, kRowHeight * scale});
  menuHot_.Brush(compositor_.CreateColorBrush(winrt::Windows::UI::ColorHelper::FromArgb(46, 255, 255, 255)));
  menuHot_.Opacity(0);
  auto hotRound = compositor_.CreateRoundedRectangleGeometry();
  hotRound.Size(menuHot_.Size());
  hotRound.CornerRadius({6 * scale, 6 * scale});
  menuHot_.Clip(compositor_.CreateGeometricClip(hotRound));
  menu_.Children().InsertAtTop(menuHot_);

  // Todas las filas en UNA superficie.
  auto text = compositor_.CreateSpriteVisual();
  text.Size(menuSize_);
  text.Brush(compositor_.CreateSurfaceBrush(Surface(menuSize_.x, menuSize_.y, [&](ID2D1DeviceContext* context, POINT at) {
    for (size_t i = 0; i < items.size(); i++) {
      const float top = at.y + kMenuPadY * scale + i * kRowHeight * scale;
      DrawRow(context, items[i], scale, at.x + kMenuPadX * scale, top, kRowHeight * scale);
      if (closable)
        DrawRow(context, L"✕", scale, at.x + menuSize_.x - (kMenuPadY + kCloseWidth * 0.62f) * scale, top,
                kRowHeight * scale);
    }
  })));
  menu_.Children().InsertAtTop(text);
  root_.Children().InsertAtTop(menu_);
  menuHotIndex_ = -1;
}

void Visuals::CloseMenu() {
  if (!menu_) return;
  root_.Children().Remove(menu_);
  menu_ = nullptr;
  menuHot_ = nullptr;
  menuHotIndex_ = -1;
}

RECT Visuals::MenuRect() const {
  return RECT{static_cast<LONG>(std::floor(menuOrigin_.x)), static_cast<LONG>(std::floor(menuOrigin_.y)),
              static_cast<LONG>(std::ceil(menuOrigin_.x + menuSize_.x)), static_cast<LONG>(std::ceil(menuOrigin_.y + menuSize_.y))};
}

int Visuals::MenuHitTest(float x, float y) const {
  if (!menu_) return -1;
  const float local = y - menuOrigin_.y;
  if (x < menuOrigin_.x || x > menuOrigin_.x + menuSize_.x || local < 0 || local > menuSize_.y) return -1;
  const int row = static_cast<int>((local - kMenuPadY * menuScale_) / (kRowHeight * menuScale_));
  return row >= 0 && row < static_cast<int>(menuRows_) ? row : -1;
}

int Visuals::MenuHitTestClose(float x, float y) const {
  if (!menuClosable_) return -1;
  const int row = MenuHitTest(x, y);
  const float right = menuOrigin_.x + menuSize_.x - kMenuPadY * menuScale_;
  return row >= 0 && x >= right - kCloseWidth * menuScale_ && x <= right ? row : -1;
}

void Visuals::MenuSetHot(int index) {
  if (!menuHot_ || index == menuHotIndex_) return;
  menuHotIndex_ = index;
  menuHot_.Opacity(index < 0 ? 0.0f : 1.0f);
  if (index >= 0) menuHot_.Offset({kMenuPadY * menuScale_, kMenuPadY * menuScale_ + index * kRowHeight * menuScale_, 0});
}

void Visuals::SetRunning(const std::vector<bool>& running) {
  for (size_t i = 0; i < dots_.size() && i < running.size(); i++) {
    const bool was = i < running_.size() && running_[i];
    if (!dots_[i] || was == running[i]) continue;
    auto fade = compositor_.CreateScalarKeyFrameAnimation();
    fade.InsertKeyFrame(1, running[i] ? 1.0f : 0.0f);
    fade.Duration(std::chrono::milliseconds(180));
    dots_[i].StartAnimation(L"Opacity", fade);
  }
  running_ = running;
}

void Visuals::SetHover(bool hovering) {
  auto spring = compositor_.CreateSpringScalarAnimation();
  spring.DampingRatio(0.85f);
  spring.Period(std::chrono::milliseconds(40));
  spring.FinalValue(hovering ? 1.0f : 0.0f);
  props_.StartAnimation(L"Amount", spring);
}

void Visuals::SetLabel(int index) {
  if (index == labelShown_) return;
  auto fade = [&](int i, float target) {
    if (i < 0 || i >= static_cast<int>(labels_.size()) || !labels_[i]) return;
    auto animation = compositor_.CreateScalarKeyFrameAnimation();
    animation.InsertKeyFrame(1, target);
    animation.Duration(std::chrono::milliseconds(target > 0 ? 140 : 90));
    labels_[i].StartAnimation(L"Opacity", animation);
  };
  fade(labelShown_, 0);
  labelShown_ = index;
  fade(index, 1);
}

void Visuals::Slide(bool hidden, bool instant) {
  const float target = hidden ? hiddenOffset_ : 0.0f;
  if (instant) {
    root_.StopAnimation(L"Offset.Y");
    root_.Offset({0, target, 0});
    return;
  }
  // Amortiguado crítico (1,0) y periodo de 70 ms: llega al 99,9% en ~100 ms, sin rebote.
  auto spring = compositor_.CreateSpringScalarAnimation();
  spring.DampingRatio(1.0f);
  spring.Period(std::chrono::milliseconds(70));
  spring.FinalValue(target);
  root_.StartAnimation(L"Offset.Y", spring);
}

}  // namespace dock
