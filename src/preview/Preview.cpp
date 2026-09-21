#include "preview/Preview.h"

#include <objbase.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cstring>

#include "fs/DirectoryReader.h"

using Microsoft::WRL::ComPtr;

namespace {

// De los archivos se lee la cabecera, no el archivo.
constexpr size_t kMaxTextBytes = 64 * 1024;
// No hay scroll en el panel, asi que sobran para llenar cualquier pantalla. El tope no es
// cosmetico: ImGui::TextEx se salta las lineas por encima del clip rect pero las mide igual
// para calcular el ancho, o sea O(texto) por frame.
constexpr int kMaxLines = 200;
constexpr int kMaxColumns = 2000;

// -- Texto -------------------------------------------------------------------------------

// El corte de 64 KB puede partir un caracter UTF-8 por la mitad: fuera la cola incompleta.
std::string TrimUtf8(const char* data, size_t size) {
    size_t tail = 0;
    while (tail < 3 && tail < size &&
           (static_cast<unsigned char>(data[size - 1 - tail]) & 0xC0) == 0x80)
        ++tail;

    if (tail < size) {
        const unsigned char lead = static_cast<unsigned char>(data[size - 1 - tail]);
        size_t need = 0;
        if ((lead & 0xE0) == 0xC0)
            need = 1;
        else if ((lead & 0xF0) == 0xE0)
            need = 2;
        else if ((lead & 0xF8) == 0xF0)
            need = 3;
        if (need > tail) size -= tail + 1;
    }
    return std::string(data, size);
}

std::string FromUtf16(const unsigned char* data, size_t size, bool bigEndian) {
    std::wstring text(size / 2, L'\0');
    std::memcpy(text.data(), data, (size / 2) * sizeof(wchar_t));  // no se asume alineacion
    if (bigEndian)
        for (wchar_t& unit : text)
            unit = static_cast<wchar_t>(((unit >> 8) & 0x00FF) | ((unit << 8) & 0xFF00));
    return ToUtf8(text);
}

// Recorta a kMaxLines lineas de kMaxColumns caracteres. Cuenta caracteres, no bytes: los de
// continuacion UTF-8 no suman columna, asi que nunca se parte uno por la mitad.
std::string Clip(const std::string& utf8) {
    std::string out;
    out.reserve(std::min<size_t>(utf8.size(), 16 * 1024));

    int lines = 0;
    int columns = 0;
    bool overflowed = false;  // el resto de una linea demasiado larga se tira
    for (const char c : utf8) {
        if (c == '\r') continue;  // no se dibuja, pero ensucia el ancho medido
        if (c == '\n') {
            if (++lines >= kMaxLines) break;
            out.push_back('\n');
            columns = 0;
            overflowed = false;
            continue;
        }
        if (overflowed) continue;
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80 && ++columns > kMaxColumns) {
            out += " ...";
            overflowed = true;
            continue;
        }
        out.push_back(c);
    }
    return out;
}

std::optional<std::string> ReadTextPreview(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                    nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;

    std::vector<unsigned char> buffer(kMaxTextBytes);
    DWORD read = 0;
    const BOOL ok =
        ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) return std::nullopt;

    return DecodeTextPreview(buffer.data(), read);
}

// -- Imagen ------------------------------------------------------------------------------

// EXIF 274 -> la transformada equivalente de WIC.
WICBitmapTransformOptions TransformFromExif(unsigned short orientation) {
    switch (orientation) {
    case 2: return WICBitmapTransformFlipHorizontal;
    case 3: return WICBitmapTransformRotate180;
    case 4: return WICBitmapTransformFlipVertical;
    case 5:
        return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate90 |
                                                      WICBitmapTransformFlipHorizontal);
    case 6: return WICBitmapTransformRotate90;
    case 7:
        return static_cast<WICBitmapTransformOptions>(WICBitmapTransformRotate270 |
                                                      WICBitmapTransformFlipHorizontal);
    case 8: return WICBitmapTransformRotate270;
    default: return WICBitmapTransformRotate0;
    }
}

unsigned short ReadOrientation(IWICBitmapFrameDecode* frame) {
    ComPtr<IWICMetadataQueryReader> reader;
    if (FAILED(frame->GetMetadataQueryReader(&reader))) return 1;

    // La misma etiqueta cuelga de un sitio u otro segun el contenedor.
    static const wchar_t* const kQueries[] = {L"/app1/ifd/{ushort=274}", L"/ifd/{ushort=274}"};
    for (const wchar_t* query : kQueries) {
        PROPVARIANT value;
        PropVariantInit(&value);
        const bool ok = SUCCEEDED(reader->GetMetadataByName(query, &value)) && value.vt == VT_UI2;
        const unsigned short orientation = ok ? value.uiVal : 0;
        PropVariantClear(&value);
        if (orientation >= 1 && orientation <= 8) return orientation;
    }
    return 1;
}

// false = no era una imagen, o WIC no supo con ella. No deja nada a medias en `out`.
bool DecodeImage(const std::wstring& path, int targetPx, Preview& out) {
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return false;

    // CacheOnDemand: sondear un archivo que no es imagen se queda en leer la cabecera. De
    // aqui sale el soporte de heic/avif/jxl en cuanto el codec este instalado, sin listas.
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                  WICDecodeMetadataCacheOnDemand, &decoder)))
        return false;

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) return false;

    UINT srcW = 0;
    UINT srcH = 0;
    if (FAILED(frame->GetSize(&srcW, &srcH)) || srcW == 0 || srcH == 0) return false;

    const unsigned short orientation = ReadOrientation(frame.Get());
    const bool transposes = orientation >= 5;  // 5..8 intercambian ancho y alto

    // El encaje se calcula en el espacio en el que se vera; el scaler trabaja en el de origen.
    const UINT viewW = transposes ? srcH : srcW;
    const UINT viewH = transposes ? srcW : srcH;
    UINT fitW = viewW;
    UINT fitH = viewH;
    bool downscaled = false;
    const UINT longest = std::max(viewW, viewH);
    if (targetPx > 0 && longest > static_cast<UINT>(targetPx)) {
        const double factor = static_cast<double>(targetPx) / static_cast<double>(longest);
        fitW = std::max(1u, static_cast<UINT>(viewW * factor));
        fitH = std::max(1u, static_cast<UINT>(viewH * factor));
        downscaled = true;
    }

    ComPtr<IWICBitmapSource> source = frame;
    if (downscaled) {
        // Escalar antes de rotar: rotar a resolucion completa costaria la imagen entera en
        // RAM. Con JPEG el scaler tira por debajo del escalado por DCT del decodificador,
        // asi que ni siquiera se decodifica entera.
        ComPtr<IWICBitmapScaler> scaler;
        if (FAILED(factory->CreateBitmapScaler(&scaler))) return false;
        if (FAILED(scaler->Initialize(frame.Get(), transposes ? fitH : fitW,
                                      transposes ? fitW : fitH, WICBitmapInterpolationModeFant)))
            return false;
        source = scaler;
    }

    if (orientation > 1) {
        ComPtr<IWICBitmapFlipRotator> rotator;
        if (FAILED(factory->CreateBitmapFlipRotator(&rotator))) return false;
        if (FAILED(rotator->Initialize(source.Get(), TransformFromExif(orientation)))) return false;
        source = rotator;
    }

    // BGRA de alfa recto, no premultiplicado: el blend del backend DX11 es
    // SRC_ALPHA/INV_SRC_ALPHA y con premultiplicado los PNG transparentes saldrian con halo.
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter))) return false;
    if (FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppBGRA,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom)))
        return false;

    UINT width = 0;
    UINT height = 0;
    if (FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0) return false;

    const UINT stride = width * 4;
    std::vector<unsigned char> pixels(static_cast<size_t>(stride) * height);
    if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels.size()),
                                     pixels.data())))
        return false;

    out.kind = Preview::Kind::Image;
    out.width = static_cast<int>(width);
    out.height = static_cast<int>(height);
    out.downscaled = downscaled;
    out.pixels = std::move(pixels);
    return true;
}

// -- Metadatos ---------------------------------------------------------------------------

std::wstring FormatTime(const FILETIME& utc) {
    FILETIME local{};
    SYSTEMTIME time{};
    if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &time))
        return L"-";

    wchar_t date[64];
    if (!GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr, date,
                         ARRAYSIZE(date), nullptr))
        return L"-";

    wchar_t clock[64];
    if (!GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &time, nullptr, clock,
                         ARRAYSIZE(clock)))
        return date;
    return std::wstring(date) + L" " + clock;
}

std::wstring FormatAttributes(DWORD attributes) {
    static const struct {
        DWORD bit;
        wchar_t letter;
    } kFlags[] = {
        {FILE_ATTRIBUTE_READONLY, L'R'},   {FILE_ATTRIBUTE_HIDDEN, L'H'},
        {FILE_ATTRIBUTE_SYSTEM, L'S'},     {FILE_ATTRIBUTE_ARCHIVE, L'A'},
        {FILE_ATTRIBUTE_COMPRESSED, L'C'}, {FILE_ATTRIBUTE_ENCRYPTED, L'E'},
        {FILE_ATTRIBUTE_REPARSE_POINT, L'L'},
    };

    std::wstring out;
    for (const auto& flag : kFlags)
        if (attributes & flag.bit) out.push_back(flag.letter);
    return out.empty() ? L"-" : out;
}

std::wstring TypeName(const std::wstring& path) {
    SHFILEINFOW info{};
    // USEFILEATTRIBUTES: basta la extension, no se toca el archivo (ni el de red lento).
    if (!SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &info, sizeof(info),
                        SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES))
        return L"-";
    return info.szTypeName;
}

std::string DescribeFile(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
        return FormatWin32Error(GetLastError());

    const unsigned long long size = (static_cast<unsigned long long>(info.nFileSizeHigh) << 32) |
                                    static_cast<unsigned long long>(info.nFileSizeLow);

    wchar_t bytes[32];
    if (!StrFormatByteSizeW(static_cast<LONGLONG>(size), bytes, ARRAYSIZE(bytes)))
        bytes[0] = L'\0';

    // ñ en vez del caracter suelto: el resto del arbol es ASCII puro y no hay razon
    // para que este archivo dependa de como se guarde.
    std::wstring text;
    text += L"Tipo          " + TypeName(path) + L"\n";
    text += L"Tamaño        " + std::wstring(bytes) + L"  (" + std::to_wstring(size) +
            L" bytes)\n";
    text += L"Modificado    " + FormatTime(info.ftLastWriteTime) + L"\n";
    text += L"Atributos     " + FormatAttributes(info.dwFileAttributes) + L"\n";
    return ToUtf8(text);
}

}  // namespace

size_t Preview::Bytes() const {
    // Los pixeles ya se han soltado cuando esto llega a la cache: lo que ocupa es la textura.
    return static_cast<size_t>(width) * static_cast<size_t>(height) * 4 + text.capacity();
}

std::optional<std::string> DecodeTextPreview(const unsigned char* data, size_t size) {
    if (size == 0) return std::nullopt;

    // Con BOM no hay que adivinar nada.
    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF)
        return Clip(TrimUtf8(reinterpret_cast<const char*>(data) + 3, size - 3));
    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE)
        return Clip(FromUtf16(data + 2, size - 2, false));
    if (size >= 2 && data[0] == 0xFE && data[1] == 0xFF)
        return Clip(FromUtf16(data + 2, size - 2, true));

    // Sin BOM: un NUL en los primeros 4 KB delata un binario...
    if (std::memchr(data, 0, std::min<size_t>(size, 4096))) return std::nullopt;

    // ...y lo que quede tiene que ser UTF-8 valido de arriba abajo.
    // ponytail: esto manda a la vista de metadatos los textos heredados en Latin-1 sin BOM.
    // Salida si aparece alguno: probar CP_ACP antes de rendirse.
    const std::string trimmed = TrimUtf8(reinterpret_cast<const char*>(data), size);
    if (trimmed.empty()) return std::nullopt;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, trimmed.c_str(),
                            static_cast<int>(trimmed.size()), nullptr, 0) == 0)
        return std::nullopt;
    return Clip(trimmed);
}

Preview LoadPreview(std::wstring path, FILETIME modified, int targetPx) {
    Preview preview;
    preview.path = std::move(path);
    preview.modified = modified;
    preview.targetPx = targetPx;

    // ponytail: sin tabla de extensiones. Se prueba WIC y luego se mira el contenido, que
    // cubre lo mismo sin lista que mantener y acierta con un .jpg mal nombrado. A cambio se
    // sondea con WIC cada archivo que no es imagen: leer su cabecera, en un hilo de trabajo.
    if (DecodeImage(preview.path, targetPx, preview)) return preview;

    // Fase 8: aqui va IShellItemImageFactory (videos, PDF, Office). Justo en este hueco: lo
    // que Windows sepa miniaturizar, antes de rendirse a los metadatos.

    if (std::optional<std::string> text = ReadTextPreview(preview.path)) {
        preview.kind = Preview::Kind::Text;
        preview.text = std::move(*text);
        return preview;
    }

    preview.kind = Preview::Kind::Info;
    preview.text = DescribeFile(preview.path);
    return preview;
}
