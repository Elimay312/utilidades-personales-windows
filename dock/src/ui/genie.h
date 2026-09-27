#pragma once

#include <windows.h>
#include <dwmapi.h>

#include <chrono>
#include <optional>
#include <vector>

#include "model/genie_curve.h"
#include "ui/vsync.h"

namespace dock {

// El genio: la ventana se derrite hacia su icono del dock.
//
// Con miniaturas DWM y no con una captura: cada una de las 40 franjas es una miniatura de la
// ventana de verdad (rcSource = su franja) y en cada fotograma se le cambia el rectángulo de
// destino. No copia píxeles: el de C# capturaba la ventana entera 3-4 veces por genio (8-11 MB
// cada vez). Y la miniatura de una ventana ya minimizada sigue enseñando su contenido (medido
// en F1, 10 de 10), así que minimizar no espera a la animación: se minimiza ya, y el genio
// juega encima.
//
// Va en una ventana propia, transparente al ratón, que cubre de la ventana al icono y solo
// vive lo que dura la animación.
class Genie {
 public:
  // Arranca el genio de `window` hacia `to`, en px de pantalla. false si no se pudo montar:
  // entonces la ventana se minimiza sin animación, como siempre. El objeto se destruye solo al
  // acabar: nadie tiene que esperarlo ni recogerlo.
  //
  // `known`: dónde estaba la ventana (sus bordes visibles) antes de minimizarse. Cuando llega
  // el aviso ya es un icono en -32000 (medido: iconic=true, 160x28), así que hay que saberlo de
  // antes; si no cuadra con el tamaño de la miniatura (se maximizó sin que nadie avisara), se
  // deduce de GetWindowPlacement.
  static bool Play(HWND window, std::optional<RECT> known, RECT to);

 private:
  Genie(HWND window, RECT to);
  ~Genie();
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
  bool Start(std::optional<RECT> known);
  RECT Origin(std::optional<RECT> known) const;
  void Step();
  void Finish();

  HWND window_;
  RECT to_;
  RECT area_{};  // lo que cubre la ventana del genio, en pantalla
  std::optional<GenieCurve> curve_;
  HWND overlay_ = nullptr;
  std::vector<HTHUMBNAIL> slices_;
  SIZE source_{};
  std::chrono::steady_clock::time_point start_, lastFrame_;
  int frames_ = 0, late_ = 0;  // para la traza: fotogramas y los que llegaron tarde (> 25 ms)
  double worst_ = 0;
  FrameClock clock_;
};

}  // namespace dock
