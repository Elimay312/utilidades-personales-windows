#include "system/icons.h"

#include <windows.h>

#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <thread>

namespace dock {
namespace {

using Microsoft::WRL::ComPtr;

constexpr int kExtractSize = 256;
// El icono grande de Windows: el último tamaño que el shell sirve escalado y sin marco.
constexpr int kFallbackSize = 48;

// "https", "steam"...: al menos 2 caracteres antes de ':' (así "C:" no es un esquema), y
// "shell:" no cuenta porque es un elemento del shell.
std::optional<std::wstring> SchemeOf(const std::wstring& target) {
  const size_t colon = target.find(L':');
  if (colon == std::wstring::npos || colon <= 1) return std::nullopt;
  const std::wstring scheme = target.substr(0, colon);
  for (wchar_t c : scheme)
    if (!std::iswalnum(c) && c != L'+' && c != L'.' && c != L'-') return std::nullopt;
  if (_wcsicmp(scheme.c_str(), L"shell") == 0) return std::nullopt;
  return scheme;
}

std::optional<std::wstring> HandlerOf(const std::wstring& scheme) {
  wchar_t path[MAX_PATH]{};
  DWORD length = MAX_PATH;
  if (FAILED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, scheme.c_str(), nullptr, path, &length)))
    return std::nullopt;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return std::nullopt;
  return std::wstring(path);
}

std::optional<IconBitmap> ReadPixels(HBITMAP hbmp) {
  BITMAP header{};
  if (!GetObjectW(hbmp, sizeof(header), &header)) return std::nullopt;
  IconBitmap icon{header.bmWidth, header.bmHeight, {}};
  icon.bgra.resize(static_cast<size_t>(icon.width) * icon.height * 4);

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = icon.width;
  info.bmiHeader.biHeight = -icon.height;  // negativa: de arriba abajo, o sale del revés
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  HDC screen = GetDC(nullptr);
  const int rows = GetDIBits(screen, hbmp, 0, icon.height, icon.bgra.data(), &info, DIB_RGB_COLORS);
  ReleaseDC(nullptr, screen);
  if (rows == 0) return std::nullopt;

  PremultiplyIfNeeded(icon.bgra);
  return icon;
}

std::optional<IconBitmap> Image(IShellItemImageFactory* factory, int size) {
  HBITMAP hbmp = nullptr;
  // ICONONLY es obligatorio: por defecto GetImage da la MINIATURA, y un .exe con vista
  // previa saldría como esa vista. BIGGERSIZEOK evita que el shell estire con StretchBlt.
  if (FAILED(factory->GetImage(SIZE{size, size}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbmp))) return std::nullopt;
  auto icon = ReadPixels(hbmp);
  DeleteObject(hbmp);
  return icon;
}

}  // namespace

std::optional<IconBitmap> ExtractIcon(const std::wstring& target) {
  std::wstring source = target;
  if (auto scheme = SchemeOf(target))
    if (auto handler = HandlerOf(*scheme)) source = *handler;

  ComPtr<IShellItemImageFactory> factory;
  if (FAILED(SHCreateItemFromParsingName(source.c_str(), nullptr, IID_PPV_ARGS(&factory)))) return std::nullopt;

  auto icon = Image(factory.Get(), kExtractSize);
  // El shell NO agranda un icono pequeño: si el fichero solo trae 32 o 48, a 256 devuelve
  // ese dibujo diminuto centrado con el marco de miniatura. Medido en 23 entradas: los sanos
  // ocupan del 87 al 100 % del lienzo y los pequeños el 14-18 %, sin nada entre medias, así
  // que el corte en la mitad no roza ningún caso real. Pedido a 48 sí viene escalado (Alice
  // Madness Returns pasa del 14 al 83 %).
  if (icon && DrawnSide(*icon) * 2 < kExtractSize) {
    // ("small" no vale como nombre: rpcndr.h lo define como macro de char.)
    if (auto fallback = Image(factory.Get(), kFallbackSize)) icon = std::move(fallback);
  }
  return icon;
}

void PremultiplyIfNeeded(std::vector<uint8_t>& bgra) {
  bool needed = false;
  for (size_t i = 0; i + 3 < bgra.size() && !needed; i += 4)
    needed = bgra[i] > bgra[i + 3] || bgra[i + 1] > bgra[i + 3] || bgra[i + 2] > bgra[i + 3];
  if (!needed) return;
  for (size_t i = 0; i + 3 < bgra.size(); i += 4) {
    const int a = bgra[i + 3];
    if (a == 255) continue;
    for (int c = 0; c < 3; c++) bgra[i + c] = static_cast<uint8_t>(bgra[i + c] * a / 255);
  }
}

int DrawnSide(const IconBitmap& icon) {
  int minX = icon.width, minY = icon.height, maxX = -1, maxY = -1;
  for (int y = 0; y < icon.height; y++) {
    for (int x = 0; x < icon.width; x++) {
      if (icon.bgra[(static_cast<size_t>(y) * icon.width + x) * 4 + 3] < 128) continue;
      minX = std::min(minX, x);
      maxX = std::max(maxX, x);
      minY = std::min(minY, y);
      maxY = std::max(maxY, y);
    }
  }
  return maxX < 0 ? 0 : std::max(maxX - minX + 1, maxY - minY + 1);
}

// --- El proceso hijo -----------------------------------------------------------------
//
// Protocolo por tuberías, sin formato que parsear:
//   entrada: [u32 n] y n veces [u32 caracteres][UTF-16]
//   salida:  por cada clave, en el mismo orden, [u32 hay] y si hay [i32 w][i32 h][w*h*4 bytes]

namespace {

bool ReadExact(HANDLE from, void* data, size_t size) {
  auto* bytes = static_cast<uint8_t*>(data);
  while (size > 0) {
    DWORD read = 0;
    if (!ReadFile(from, bytes, static_cast<DWORD>(std::min<size_t>(size, 1 << 20)), &read, nullptr) || read == 0)
      return false;
    bytes += read;
    size -= read;
  }
  return true;
}

bool WriteExact(HANDLE to, const void* data, size_t size) {
  auto* bytes = static_cast<const uint8_t*>(data);
  while (size > 0) {
    DWORD written = 0;
    if (!WriteFile(to, bytes, static_cast<DWORD>(std::min<size_t>(size, 1 << 20)), &written, nullptr)) return false;
    bytes += written;
    size -= written;
  }
  return true;
}

}  // namespace

int RunExtractor() {
  const HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  uint32_t count = 0;
  if (!ReadExact(in, &count, sizeof(count))) return 1;
  std::vector<std::wstring> keys(count);
  for (auto& key : keys) {
    uint32_t length = 0;
    if (!ReadExact(in, &length, sizeof(length))) return 1;
    key.resize(length);
    if (!ReadExact(in, key.data(), length * sizeof(wchar_t))) return 1;
  }

  // En paralelo, un hilo STA por icono, como hacía el de C#: en serie eran ~570 ms para diez,
  // 289 de ellos solo la Calculadora. Cada hilo en STA por la trampa de ExtractIcon.
  // ponytail: un hilo por icono; con docks de cientos de iconos tocaría un pool.
  std::vector<std::optional<IconBitmap>> icons(count);
  std::vector<std::thread> threads;
  for (uint32_t i = 0; i < count; i++) {
    threads.emplace_back([&, i] {
      const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE));
      icons[i] = ExtractIcon(keys[i]);
      if (com) CoUninitialize();
    });
  }
  for (auto& thread : threads) thread.join();

  for (const auto& icon : icons) {
    const uint32_t present = icon ? 1 : 0;
    if (!WriteExact(out, &present, sizeof(present))) return 1;
    if (!icon) continue;
    if (!WriteExact(out, &icon->width, sizeof(int32_t)) || !WriteExact(out, &icon->height, sizeof(int32_t)) ||
        !WriteExact(out, icon->bgra.data(), icon->bgra.size()))
      return 1;
  }
  return 0;
}

IconSet ExtractIconsOutOfProcess(const std::vector<std::wstring>& keys) {
  IconSet result;
  if (keys.empty()) return result;

  // Solo los extremos del hijo se heredan; los nuestros no, o el hijo nunca vería el EOF.
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE childIn = nullptr, toChild = nullptr, fromChild = nullptr, childOut = nullptr;
  if (!CreatePipe(&childIn, &toChild, &inherit, 0)) return result;
  if (!CreatePipe(&fromChild, &childOut, &inherit, 0)) {
    CloseHandle(childIn);
    CloseHandle(toChild);
    return result;
  }
  SetHandleInformation(toChild, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(fromChild, HANDLE_FLAG_INHERIT, 0);

  wchar_t exe[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring command = L"\"" + std::wstring(exe) + L"\" --extraer";
  STARTUPINFOW startup{sizeof(startup)};
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = childIn;
  startup.hStdOutput = childOut;
  startup.hStdError = nullptr;
  PROCESS_INFORMATION process{};
  const bool started = CreateProcessW(exe, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                      &startup, &process);
  CloseHandle(childIn);
  CloseHandle(childOut);
  if (!started) {
    CloseHandle(toChild);
    CloseHandle(fromChild);
    return result;
  }

  const uint32_t count = static_cast<uint32_t>(keys.size());
  bool ok = WriteExact(toChild, &count, sizeof(count));
  for (const auto& key : keys) {
    const uint32_t length = static_cast<uint32_t>(key.size());
    ok = ok && WriteExact(toChild, &length, sizeof(length)) && WriteExact(toChild, key.data(), length * sizeof(wchar_t));
  }
  CloseHandle(toChild);

  for (size_t i = 0; ok && i < keys.size(); i++) {
    uint32_t present = 0;
    if (!ReadExact(fromChild, &present, sizeof(present))) break;
    if (!present) continue;
    IconBitmap icon;
    if (!ReadExact(fromChild, &icon.width, sizeof(int32_t)) || !ReadExact(fromChild, &icon.height, sizeof(int32_t)) ||
        icon.width <= 0 || icon.height <= 0 || icon.width > 1024 || icon.height > 1024)
      break;
    icon.bgra.resize(static_cast<size_t>(icon.width) * icon.height * 4);
    if (!ReadExact(fromChild, icon.bgra.data(), icon.bgra.size())) break;
    result.emplace(keys[i], std::move(icon));
  }
  CloseHandle(fromChild);
  WaitForSingleObject(process.hProcess, 5000);
  CloseHandle(process.hProcess);
  CloseHandle(process.hThread);
  return result;
}

}  // namespace dock
