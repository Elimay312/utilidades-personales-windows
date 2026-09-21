#pragma once

// Las cabeceras de C++/WinRT que consume la aplicación, en un solo sitio y en orden.
//
// No es comodidad: C++/WinRT exige incluir la cabecera de TODOS los espacios de nombres
// que se usan, incluidos los que solo aparecen como tipo de retorno. Sin
// Windows.Foundation.h, métodos como Close() o Append() quedan declarados pero sin
// definir, y el error que suelta el compilador —"no se puede usar una función que
// devuelve auto antes de que esté definida"— apunta a una cabecera del SDK y no dice cuál
// falta. Se paga una vez.

#include <winrt/Windows.Foundation.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.h>
