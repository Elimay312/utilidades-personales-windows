#include "projectfile/Proyecto.h"

#include <algorithm>

#include "model/Utf.h"

namespace Proyecto {
namespace {

constexpr wchar_t kBom = 0xFEFF;
// Los tres guiones que la gente usa de verdad. El largo es el que escribe CLAUDE.md, el
// medio lo pone Word solo, y el corto lo escribe cualquiera que tenga prisa.
constexpr wchar_t kEmDash = 0x2014;
constexpr wchar_t kEnDash = 0x2013;

bool Blank(wchar_t c) { return c == L' ' || c == L'\t'; }

std::wstring_view Trim(std::wstring_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && Blank(text[begin])) ++begin;
    std::size_t end = text.size();
    while (end > begin && Blank(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

// Solo ASCII, y a propósito: lo único que se compara con esto son palabras clave que
// nosotros mismos definimos —«prioridad», «novedades»— y que van en ASCII por la misma razón
// que los slugs de model/Types.h. Plegar tildes aquí sería resolver un problema que no hay.
std::wstring LowerAscii(std::wstring_view text) {
    std::wstring out(text);
    for (wchar_t& c : out) {
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
    }
    return out;
}

// Parte por saltos de línea y se come el retorno de carro que deja un archivo en CRLF. Las
// vistas apuntan al texto de entrada; quien se quede con una tiene que copiarla.
std::vector<std::wstring_view> Lines(std::wstring_view text) {
    std::vector<std::wstring_view> out;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= text.size(); ++i) {
        if (i == text.size() || text[i] == L'\n') {
            std::wstring_view line = text.substr(start, i - start);
            if (!line.empty() && line.back() == L'\r') line.remove_suffix(1);
            out.push_back(line);
            start = i + 1;
        }
    }
    // Un archivo que termina en salto de línea deja una última vista vacía que no es una
    // línea del documento, es el final. Sin quitarla, cada ida y vuelta añadiría una.
    if (!text.empty() && !out.empty() && out.back().empty()) out.pop_back();
    return out;
}

std::wstring Join(const std::vector<std::wstring_view>& lines, std::size_t from, std::size_t to) {
    std::wstring out;
    for (std::size_t i = from; i < to && i < lines.size(); ++i) {
        if (i > from) out.push_back(L'\n');
        out.append(lines[i]);
    }
    return out;
}

// Quita los renglones en blanco de arriba y de abajo. Lo de dentro no se toca: un párrafo
// separado de otro por una línea vacía sigue siendo dos párrafos.
std::wstring TrimBlankEdges(std::wstring_view text) {
    const std::vector<std::wstring_view> lines = Lines(text);
    std::size_t begin = 0;
    while (begin < lines.size() && Trim(lines[begin]).empty()) ++begin;
    std::size_t end = lines.size();
    while (end > begin && Trim(lines[end - 1]).empty()) --end;
    return Join(lines, begin, end);
}

bool IsFence(std::wstring_view line) { return Trim(line) == L"---"; }

// Una viñeta de lista, no una raya horizontal. «---» y «***» solos son rayas, y tratarlos
// como novedades metería una novedad vacía por cada separador del archivo.
bool IsBullet(std::wstring_view trimmed) {
    if (trimmed.empty()) return false;
    if (trimmed[0] != L'-' && trimmed[0] != L'*') return false;
    if (trimmed.size() == 1) return true;
    if (trimmed.find_first_not_of(trimmed[0]) == std::wstring_view::npos) return false;
    return trimmed[1] == L' ' || trimmed[1] == L'\t';
}

std::wstring_view AfterMarker(std::wstring_view trimmed) {
    return Trim(trimmed.substr(trimmed.size() > 1 ? 2 : 1));
}

// «## Novedades», con cualquier número de almohadillas y en cualquier caja.
bool IsNovedadesHeading(std::wstring_view line) {
    const std::wstring_view t = Trim(line);
    std::size_t hashes = 0;
    while (hashes < t.size() && t[hashes] == L'#') ++hashes;
    if (hashes == 0) return false;
    return LowerAscii(Trim(t.substr(hashes))) == L"novedades";
}

// Comillas de YAML alrededor del valor. Se quitan al leer y no se vuelven a poner al
// escribir: nuestro lector parte por el PRIMER dos puntos y se queda con el resto, así que
// no hay ningún valor que las necesite.
std::wstring_view Unquote(std::wstring_view value) {
    if (value.size() >= 2 && (value.front() == L'"' || value.front() == L'\'') &&
        value.back() == value.front()) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool Digits(std::wstring_view text, std::size_t from, std::size_t count) {
    if (from + count > text.size()) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (text[from + i] < L'0' || text[from + i] > L'9') return false;
    }
    return true;
}

// 'YYYY-MM-DD' al principio. Se mira la FORMA y no se valida la fecha: un 31 de febrero
// escrito a mano sigue siendo la nota de alguien, y tirarla por no existir ese día sería
// borrar el dato para proteger el formato.
bool LooksLikeDay(std::wstring_view text) {
    if (text.size() < 10) return false;
    return Digits(text, 0, 4) && text[4] == L'-' && Digits(text, 5, 2) && text[7] == L'-' &&
           Digits(text, 8, 2);
}

Entry ReadEntry(std::wstring_view trimmed) {
    const std::wstring_view rest = AfterMarker(trimmed);
    Entry entry;
    if (!LooksLikeDay(rest)) {
        entry.text = std::wstring(rest);
        return entry;
    }

    entry.day = Model::ToUtf8(rest.substr(0, 10));
    std::wstring_view tail = Trim(rest.substr(10));
    // El separador es opcional: hay quien escribe la fecha y el texto sin nada en medio.
    if (!tail.empty() &&
        (tail[0] == kEmDash || tail[0] == kEnDash || tail[0] == L'-' || tail[0] == L':')) {
        tail = Trim(tail.substr(1));
    }
    entry.text = std::wstring(tail);
    return entry;
}

void ReadFrontLine(std::wstring_view line, File& file) {
    const std::wstring_view t = Trim(line);
    if (t.empty()) return;

    const std::size_t colon = t.find(L':');
    if (colon == std::wstring_view::npos) {
        // Ni siquiera tiene forma de clave. Se guarda tal cual y vuelve a salir tal cual.
        file.unknown.emplace_back(t);
        return;
    }

    const std::wstring key = LowerAscii(Trim(t.substr(0, colon)));
    const std::wstring_view value = Unquote(Trim(t.substr(colon + 1)));

    if (key == L"prioridad") {
        file.priorityRaw = value;
        file.priority = Model::PriorityFromSlug(Model::ToUtf8(value));
    } else if (key == L"estado") {
        file.stateRaw = value;
        file.state = Model::StateFromSlug(Model::ToUtf8(value));
    } else if (key == L"siguiente_paso") {
        file.nextStep = value;
    } else if (key == L"actualizado") {
        file.updatedRaw = value;
        file.updated = Model::ParseIso8601(Model::ToUtf8(value));
    } else {
        file.unknown.emplace_back(t);
    }
}

// Un valor de frontmatter no puede llevar saltos de línea dentro: partiría el bloque en dos
// y el archivo dejaría de tener frontmatter. El campo viene de un Ui::Field de una línea,
// así que esto solo se nota con lo que llegue de fuera.
std::wstring OneLine(std::wstring_view text) {
    std::wstring out(text);
    for (wchar_t& c : out) {
        if (c == L'\n' || c == L'\r') c = L' ';
    }
    return std::wstring(Trim(out));
}

}  // namespace

// --------------------------------------------------------------------------- Leer --

File Parse(std::wstring_view text) {
    File file;
    if (!text.empty() && text.front() == kBom) text.remove_prefix(1);
    file.crlf = text.find(L"\r\n") != std::wstring_view::npos;

    const std::vector<std::wstring_view> lines = Lines(text);

    std::size_t bodyStart = 0;
    std::size_t open = 0;
    while (open < lines.size() && Trim(lines[open]).empty()) ++open;

    if (open < lines.size() && IsFence(lines[open])) {
        std::size_t close = open + 1;
        while (close < lines.size() && !IsFence(lines[close])) ++close;
        // Un cercado que se abre y no se cierra NO es un frontmatter. Consumirlo hasta el
        // final del archivo se comería el documento entero, y lo que de verdad hay delante
        // suele ser una raya horizontal de Markdown.
        if (close < lines.size()) {
            file.hadFrontmatter = true;
            for (std::size_t k = open + 1; k < close; ++k) ReadFrontLine(lines[k], file);
            bodyStart = close + 1;
        }
    }

    std::size_t heading = lines.size();
    for (std::size_t i = bodyStart; i < lines.size(); ++i) {
        if (IsNovedadesHeading(lines[i])) {
            heading = i;
            break;
        }
    }

    file.before = TrimBlankEdges(Join(lines, bodyStart, heading));
    if (heading == lines.size()) return file;

    file.hadNovedades = true;
    std::size_t k = heading + 1;
    for (; k < lines.size(); ++k) {
        const std::wstring_view t = Trim(lines[k]);
        // Los blancos de dentro de la lista se saltan; el primer renglón que no es ni blanco
        // ni viñeta cierra la sección, y todo lo que quede es de después.
        if (t.empty()) continue;
        if (!IsBullet(t)) break;
        file.novedades.push_back(ReadEntry(t));
    }
    file.after = TrimBlankEdges(Join(lines, k, lines.size()));
    return file;
}

// ------------------------------------------------------------------------ Escribir --

std::wstring Render(const File& file) {
    std::vector<std::wstring> blocks;

    std::wstring front;
    const auto add = [&front](std::wstring_view key, std::wstring_view value) {
        if (value.empty()) return;
        front.append(key);
        front.append(L": ");
        front.append(value);
        front.push_back(L'\n');
    };

    // El valor reconocido manda; si no reconocimos el que venía, se devuelve tal cual. Es lo
    // que hace que leer y volver a escribir un archivo con una prioridad inventada no le
    // borre esa línea a nadie.
    add(L"prioridad",
        file.priority ? Model::ToWide(Model::SlugOf(*file.priority)) : file.priorityRaw);
    add(L"estado", file.state ? Model::ToWide(Model::SlugOf(*file.state)) : file.stateRaw);
    add(L"siguiente_paso", OneLine(file.nextStep));
    add(L"actualizado",
        file.updated ? Model::ToWide(Model::FormatDay(*file.updated)) : file.updatedRaw);
    for (const std::wstring& line : file.unknown) {
        front.append(line);
        front.push_back(L'\n');
    }
    if (!front.empty()) {
        front.pop_back();
        blocks.push_back(L"---\n" + front + L"\n---");
    }

    if (!file.before.empty()) blocks.push_back(file.before);

    if (file.hadNovedades || !file.novedades.empty()) {
        std::wstring section = L"## Novedades";
        if (!file.novedades.empty()) section.push_back(L'\n');
        for (const Entry& entry : file.novedades) {
            section.append(L"\n- ");
            if (!entry.day.empty()) {
                section.append(Model::ToWide(entry.day));
                section.push_back(L' ');
                section.push_back(kEmDash);
                section.push_back(L' ');
            }
            section.append(OneLine(entry.text));
        }
        blocks.push_back(std::move(section));
    }

    if (!file.after.empty()) blocks.push_back(file.after);
    if (blocks.empty()) return std::wstring();

    std::wstring out;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (i > 0) out.append(L"\n\n");
        out.append(blocks[i]);
    }
    out.push_back(L'\n');

    // Todo se compone con LF y se convierte al final. Repartir el salto de línea por las
    // veinte concatenaciones de arriba es la forma de que una se quede con el otro.
    if (!file.crlf) return out;

    std::wstring crlf;
    crlf.reserve(out.size() + out.size() / 8);
    for (const wchar_t c : out) {
        if (c == L'\n') crlf.push_back(L'\r');
        crlf.push_back(c);
    }
    return crlf;
}

// ------------------------------------------------------------------------ Fusionar --

File Merge(const File& remote, const Model::Local& local,
           const std::vector<Model::Novedad>& novedades, Model::Instant now) {
    File out = remote;
    out.priority = local.priority;
    out.state = local.state;
    out.nextStep = local.nextStep;
    out.updated = now;
    out.hadFrontmatter = true;
    out.hadNovedades = true;

    // Unión, no sustitución. Una novedad escrita desde otro equipo está en el archivo y no
    // en esta caché; quedarnos solo con las nuestras la borraría, y ese es exactamente el
    // fallo silencioso que la prioridad 1 de CLAUDE.md existe para impedir.
    for (const Model::Novedad& mine : novedades) {
        const bool already =
            std::any_of(out.novedades.begin(), out.novedades.end(), [&mine](const Entry& e) {
                return e.day == mine.day && e.text == mine.text;
            });
        if (!already) out.novedades.push_back(Entry{mine.day, mine.text});
    }

    // Las fechadas primero y de la más nueva a la más vieja; las que vinieron a mano sin
    // fecha se quedan al final y en su orden. stable_sort y no sort: dos novedades del mismo
    // día tienen que salir siempre igual, o cada escritura movería renglones y el diff del
    // commit contaría cambios que no ha hecho nadie.
    std::stable_sort(out.novedades.begin(), out.novedades.end(),
                     [](const Entry& a, const Entry& b) {
                         if (a.day.empty() != b.day.empty()) return !a.day.empty();
                         return a.day > b.day;
                     });
    return out;
}

Import Adopt(const File& file, const Model::Local& current) {
    Import out;
    // Se parte de lo que ya hay: lo que el archivo no trae no se toca. Importar no puede
    // vaciar un siguiente paso que estaba escrito aquí solo porque allí no hubiera ninguno.
    out.local = current;
    if (file.priority) out.local.priority = *file.priority;
    if (file.state) out.local.state = *file.state;
    if (!file.nextStep.empty()) out.local.nextStep = file.nextStep;
    if (file.updated) out.local.updatedAt = *file.updated;

    const std::string fallback = file.updated ? Model::FormatDay(*file.updated) : std::string();
    for (const Entry& entry : file.novedades) {
        if (entry.text.empty()) continue;
        Model::Novedad novedad;
        novedad.repoId = current.repoId;
        novedad.day = entry.day.empty() ? fallback : entry.day;
        novedad.text = entry.text;
        // Su fecha es su fecha. Poner aquí el reloj de ahora haría que importar un archivo
        // de hace tres meses dijera que todo se escribió hoy.
        novedad.createdAt =
            Model::ParseIso8601(novedad.day).value_or(file.updated.value_or(Model::Instant{}));
        out.novedades.push_back(std::move(novedad));
    }
    return out;
}

std::vector<std::wstring> AsNovedades(std::wstring_view markdown) {
    std::vector<std::wstring> out;
    for (const std::wstring_view line : Lines(markdown)) {
        const std::wstring_view t = Trim(line);
        if (!IsBullet(t)) continue;
        std::wstring text(AfterMarker(t));
        if (!text.empty()) out.push_back(std::move(text));
    }
    if (!out.empty()) return out;

    // Sin viñetas, el archivo entero como una sola novedad y con los renglones pegados por
    // un espacio. Una novedad es un renglón de una lista: con saltos de línea dentro rompe
    // la pantalla y rompe también el PROYECTO.md al que acabaría volviendo.
    std::wstring whole;
    for (const std::wstring_view line : Lines(markdown)) {
        const std::wstring_view t = Trim(line);
        if (t.empty()) continue;
        if (!whole.empty()) whole.push_back(L' ');
        whole.append(t);
    }
    if (!whole.empty()) out.push_back(std::move(whole));
    return out;
}

}  // namespace Proyecto
