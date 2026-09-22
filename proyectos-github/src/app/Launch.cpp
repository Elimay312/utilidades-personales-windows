#include "app/Launch.h"

#include <algorithm>

namespace App {

namespace {

// Un nombre de repositorio de GitHub no pasa de 100 caracteres, y con el dueño delante y su
// barra, de 140 largos. 200 deja margen y sigue siendo un techo.
constexpr std::size_t kMaxName = 200;

constexpr wchar_t kScheme[] = L"brujula://";
constexpr wchar_t kHost[] = L"repo/";

bool IsAscii(wchar_t c) { return c < 128; }

wchar_t Lower(wchar_t c) { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c; }

bool StartsWithNoCase(const std::wstring& text, const wchar_t* prefix) {
    std::size_t i = 0;
    for (; prefix[i] != 0; ++i) {
        if (i >= text.size() || Lower(text[i]) != Lower(prefix[i])) return false;
    }
    return true;
}

int HexValue(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}

// Deshace los %XX. Solo ASCII: un %C3%BA sería un byte suelto de UTF-8 y aquí se trabaja en
// UTF-16, así que juntarlos a pares sería inventarse una conversión. Los nombres de
// repositorio de GitHub son ASCII, y lo que no lo sea se cae en LooksLikeRepoName, que es
// donde tiene que caerse.
std::wstring Unescape(const std::wstring& text) {
    std::wstring out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'%' && i + 2 < text.size()) {
            const int hi = HexValue(text[i + 1]);
            const int lo = HexValue(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<wchar_t>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

}  // namespace

bool LooksLikeRepoName(const std::wstring& name) {
    if (name.empty() || name.size() > kMaxName) return false;
    // Ni empieza ni acaba en barra, y no lleva dos seguidas: "dueño/nombre" o "nombre", y
    // nada más. Sin esto, "//" o "/x" pasarían la comprobación de caracteres.
    if (name.front() == L'/' || name.back() == L'/') return false;
    if (name.find(L"//") != std::wstring::npos) return false;
    if (std::count(name.begin(), name.end(), L'/') > 1) return false;

    for (const wchar_t c : name) {
        if (!IsAscii(c)) return false;
        const bool alnum = (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'z') ||
                           (c >= L'A' && c <= L'Z');
        if (alnum) continue;
        if (c == L'-' || c == L'_' || c == L'.' || c == L'/') continue;
        return false;
    }
    // Ni "." ni ".." ni nada que empiece por punto: no llega a ninguna ruta, pero un nombre
    // así tampoco existe en GitHub y dejarlo pasar sería dejar pasar por dejar pasar.
    return name.front() != L'.';
}

std::wstring RepoFromUrl(const std::wstring& url) {
    if (!StartsWithNoCase(url, kScheme)) return std::wstring();
    std::wstring rest = url.substr(std::char_traits<wchar_t>::length(kScheme));
    if (!StartsWithNoCase(rest, kHost)) return std::wstring();
    rest = rest.substr(std::char_traits<wchar_t>::length(kHost));

    // El shell suele añadir una barra final al pasar la URL. Y lo que venga detrás de un ?
    // o de un # no es parte del nombre.
    const std::size_t cut = rest.find_first_of(L"?#");
    if (cut != std::wstring::npos) rest.erase(cut);
    while (!rest.empty() && rest.back() == L'/') rest.pop_back();

    const std::wstring name = Unescape(rest);
    return LooksLikeRepoName(name) ? name : std::wstring();
}

Launch ParseArguments(int count, const wchar_t* const* argv) {
    Launch launch;
    if (argv == nullptr) return launch;

    for (int i = 1; i < count; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--repo" && i + 1 < count) {
            const std::wstring name = argv[i + 1];
            if (LooksLikeRepoName(name)) launch.repo = name;
            ++i;
            continue;
        }
        // Sin --repo delante: es como llega desde el esquema de URL, que le pasa a la
        // aplicación la dirección entera como un argumento suelto.
        if (launch.repo.empty()) {
            if (std::wstring fromUrl = RepoFromUrl(arg); !fromUrl.empty()) {
                launch.repo = std::move(fromUrl);
            }
        }
    }
    return launch;
}

}  // namespace App
